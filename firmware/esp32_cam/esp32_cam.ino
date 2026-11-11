
#include <WiFi.h>
#include <WebServer.h>
#include "DHT.h"
#include "config.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "esp_http_server.h"

// ---------------- SENSOR PINS ----------------

#define DHTPIN 13
#define DHTTYPE DHT11

#define MQ2_DO_PIN 14
#define LED_PIN 4

// ---------------- WIFI ACCESS POINT ----------------

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

WebServer server(80);
DHT dht(DHTPIN, DHTTYPE);

// UART0 TX GPIO 1 sends motor commands to Arduino
#define MOTOR_SERIAL Serial

// ---------------- CAMERA PINS: AI THINKER ----------------

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ---------------- SENSOR VARIABLES ----------------

float temperature = 0;
float humidity = 0;

bool dhtValid = false;
bool gasDetected = false;
bool flashOn = false;

unsigned long lastDHTRead = 0;

httpd_handle_t cameraHttpd = NULL;
bool cameraReady = false;

// ---------------- CAMERA STREAM ----------------
// RHYX/GC2145 cameras commonly need RGB565 capture followed by
// software JPEG conversion. This stream endpoint runs on port 81.

static esp_err_t streamHandler(httpd_req_t *req) {
  if (!cameraReady) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req,
      "Camera initialization failed. Check sensor compatibility and ribbon cable.",
      HTTPD_RESP_USE_STRLEN);
  }

  esp_err_t res = httpd_resp_set_type(
    req, "multipart/x-mixed-replace;boundary=frame");
  if (res != ESP_OK) return res;

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

  char partBuf[128];

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
      break;
    }

    uint8_t *jpgBuf = NULL;
    size_t jpgLen = 0;
    bool converted = false;

    // If a sensor/driver provides JPEG already, send it directly;
    // otherwise convert the RGB565 frame for the browser.
    if (fb->format == PIXFORMAT_JPEG) {
      jpgBuf = fb->buf;
      jpgLen = fb->len;
      converted = true;
    } else {
      converted = frame2jpg(fb, 60, &jpgBuf, &jpgLen);
    }

    if (!converted || jpgBuf == NULL || jpgLen == 0) {
      bool sourceWasJpeg = (fb->format == PIXFORMAT_JPEG);
      if (!sourceWasJpeg && jpgBuf) free(jpgBuf);
      esp_camera_fb_return(fb);
      res = ESP_FAIL;
      break;
    }

    int hlen = snprintf(partBuf, sizeof(partBuf),
      "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
      (unsigned int)jpgLen);

    res = httpd_resp_send_chunk(req, partBuf, hlen);
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)jpgBuf, jpgLen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, "\r\n", 2);
    }

    bool wasJpeg = (fb->format == PIXFORMAT_JPEG);
    if (!wasJpeg && jpgBuf) free(jpgBuf);
    esp_camera_fb_return(fb);

    if (res != ESP_OK) break;
  }

  return res;
}

// ---------------- START CAMERA ----------------

bool startCamera() {
  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  // RHYX/GC2145 trial configuration: raw RGB565, then software JPEG.
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QQVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t camErr = esp_camera_init(&config);
  if (camErr != ESP_OK) {
    cameraReady = false;
    // Keep the main dashboard online even if the sensor cannot initialize.
    return false;
  }

  cameraReady = true;

  httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();
  streamConfig.server_port = 81;
  streamConfig.ctrl_port = 32769;
  streamConfig.max_uri_handlers = 4;
  streamConfig.stack_size = 8192;

  httpd_uri_t streamUri = {};
  streamUri.uri = "/stream";
  streamUri.method = HTTP_GET;
  streamUri.handler = streamHandler;
  streamUri.user_ctx = NULL;

  if (httpd_start(&cameraHttpd, &streamConfig) != ESP_OK) {
    cameraReady = false;
    return false;
  }

  if (httpd_register_uri_handler(cameraHttpd, &streamUri) != ESP_OK) {
    httpd_stop(cameraHttpd);
    cameraHttpd = NULL;
    cameraReady = false;
    return false;
  }

  return true;
}

// ---------------- WEBPAGE ----------------

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>

<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32-CAM Rover</title>

    <style>
        * {
            box-sizing: border-box;
        }

        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background: #10151d;
            color: #f5f7fa;
            margin: 0;
            padding: 20px;
        }

        h1 {
            color: #4de1c1;
        }

        .card {
            background: #1b2533;
            max-width: 440px;
            margin: 18px auto;
            padding: 18px;
            border-radius: 16px;
        }

        .sensor {
            display: flex;
            justify-content: space-around;
            flex-wrap: wrap;
            gap: 12px;
        }

        .value {
            font-size: 21px;
            margin: 12px 0;
        }

        .small {
            color: #b4c0cf;
            font-size: 13px;
        }

        .camera {
            width: 100%;
            height: auto;
            border-radius: 12px;
            background: #080b10;
            min-height: 150px;
        }

        button {
            min-width: 85px;
            min-height: 52px;
            margin: 5px;
            padding: 10px;
            font-size: 16px;
            font-weight: bold;
            border: 0;
            border-radius: 10px;
            color: white;
            background: #34445a;
            cursor: pointer;
        }

        button:active {
            transform: scale(.96);
        }

        .mode {
            background: #087f8c;
        }

        .stop {
            background: #d8394f;
        }

        .move {
            background: #34445a;
        }

        .flash {
            background: #d49b25;
            color: #10151d;
        }

        .status {
            padding: 10px;
            border-radius: 8px;
            background: #29384a;
            margin: 10px 0;
        }

        /* ---------- Boot animation ---------- */
        #boot {
            position: fixed;
            inset: 0;
            z-index: 9999;
            background: #10151d;
            opacity: 1;
            transition: opacity .25s ease;
            cursor: pointer;
        }

        #boot.done {
            opacity: 0;
            pointer-events: none;
        }

        #bootCanvas {
            position: absolute;
            inset: 0;
            width: 100%;
            height: 100%;
            display: block;
        }

        #bootCaption {
            position: absolute;
            left: 0;
            right: 0;
            bottom: 12vh;
            text-align: center;
            color: #4de1c1;
            font-size: 15px;
            letter-spacing: .12em;
        }

        #bootBar {
            position: absolute;
            left: 50%;
            bottom: 9vh;
            width: min(220px, 56vw);
            height: 3px;
            margin-left: calc(min(220px, 56vw) / -2);
            background: #29384a;
            border-radius: 2px;
            overflow: hidden;
        }

        #bootFill {
            height: 100%;
            width: 0;
            background: #4de1c1;
        }
    </style>
</head>

<body>

    <div id="boot" aria-hidden="true">
        <canvas id="bootCanvas"></canvas>
        <div id="bootCaption">Starting rover</div>
        <div id="bootBar">
            <div id="bootFill"></div>
        </div>
    </div>

    <h1>ESP32-CAM Rover</h1>
    <p class="small">Live monitoring and motor control</p>

    <div class="card">
        <h2>Camera Feed</h2>
        <img class="camera" src="/streamview" id="cameraFeed" alt="Camera stream unavailable">
        <p class="small">Live camera stream</p>
    </div>

    <div class="card">
        <h2>Camera Flashlight</h2>

        <button class="flash" onclick="toggleFlash()">
            <span id="flashButton">FLASHLIGHT ON</span>
        </button>

        <div class="status">
            Flashlight: <strong id="flashStatus">OFF</strong>
        </div>
    </div>

    <div class="card">
        <h2>Sensor Dashboard</h2>

        <div class="sensor">
            <div>
                <div class="small">Temperature</div>
                <div class="value"><span>28</span> °C</div>
            </div>

            <div>
                <div class="small">Humidity</div>
                <div class="value"><span>67</span> %</div>
            </div>
        </div>

        <div class="status">
            MQ-2 Gas Status: <strong id="gas">Checking...</strong>
        </div>

        <div class="small" id="sensorStatus">Reading sensors...</div>
    </div>

    <div class="card">
        <h2>Driving Mode</h2>

        <button class="mode" onclick="sendCommand('m')">MANUAL</button>
        <button class="mode" onclick="sendCommand('a')">AUTO</button>

        <div class="status">
            Current mode: <strong id="mode">Manual</strong>
        </div>

        <h3>Manual Controls</h3>

        <div>
            <button class="move" onclick="sendCommand('f')">UP</button>
        </div>

        <div>
            <button class="move" onclick="sendCommand('l')">LEFT</button>
            <button class="stop" onclick="sendCommand('s')">STOP</button>
            <button class="move" onclick="sendCommand('r')">RIGHT</button>
        </div>

        <div>
            <button class="move" onclick="sendCommand('b')">DOWN</button>
        </div>

        <p class="small">
            Select Manual before movement. Stop halts the motors.
        </p>
    </div>

    <script>
        /* Lightweight boot animation: no embedded 3D mesh, WebGL, or external libraries. */
        (function () {
            var boot = document.getElementById('boot');
            var cvs = document.getElementById('bootCanvas');
            var fill = document.getElementById('bootFill');
            var cap = document.getElementById('bootCaption');
            if (!boot || !cvs) return;

            var ctx = cvs.getContext('2d');
            var raf = 0, started = performance.now(), ended = false;
            var duration = 1450;

            function resize() {
                var dpr = Math.min(window.devicePixelRatio || 1, 1.25);
                cvs.width = Math.max(1, Math.floor(innerWidth * dpr));
                cvs.height = Math.max(1, Math.floor(innerHeight * dpr));
                ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
            }
            function finish() {
                if (ended) return;
                ended = true;
                cancelAnimationFrame(raf);
                boot.classList.add('done');
                setTimeout(function () {
                    if (boot.parentNode) boot.parentNode.removeChild(boot);
                }, 350);
            }
            if (window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches) {
                boot.remove();
                return;
            }

            function draw(now) {
                if (ended) return;
                var t = Math.min(1, (now - started) / duration);
                var w = innerWidth, h = innerHeight;
                ctx.clearRect(0, 0, w, h);

                // A compact rover icon drawn with a handful of 2D shapes.
                var cx = w / 2, cy = h * 0.43;
                var scale = Math.min(1, w / 420);
                var bob = Math.sin(t * Math.PI * 5) * 3 * (1 - t);
                ctx.save();
                ctx.translate(cx, cy + bob);
                ctx.rotate(Math.sin(t * Math.PI * 2) * 0.12);
                ctx.scale(scale, scale);
                ctx.lineWidth = 3;
                ctx.lineJoin = 'round';
                ctx.strokeStyle = '#4de1c1';
                ctx.fillStyle = '#1b2533';

                // Wheels
                [[-66, 22], [52, 22]].forEach(function (p) {
                    ctx.beginPath();
                    ctx.roundRect(p[0], p[1], 24, 38, 7);
                    ctx.fill();
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(p[0] + 6, p[1] + 8);
                    ctx.lineTo(p[0] + 18, p[1] + 30);
                    ctx.stroke();
                });

                // Chassis and camera mast
                ctx.beginPath();
                ctx.roundRect(-55, -8, 110, 36, 8);
                ctx.fill();
                ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(-22, -8);
                ctx.lineTo(-22, -30);
                ctx.lineTo(22, -30);
                ctx.lineTo(22, -8);
                ctx.closePath();
                ctx.fill();
                ctx.stroke();

                // Camera lens
                ctx.beginPath();
                ctx.arc(0, -20, 9, 0, Math.PI * 2);
                ctx.fillStyle = '#4de1c1';
                ctx.fill();
                ctx.beginPath();
                ctx.arc(0, -20, 3, 0, Math.PI * 2);
                ctx.fillStyle = '#10151d';
                ctx.fill();

                // Simple sensor lights
                ctx.fillStyle = '#4de1c1';
                ctx.fillRect(-39, 3, 9, 5);
                ctx.fillRect(30, 3, 9, 5);
                ctx.restore();

                // Small orbit dots, limited to a few lightweight strokes.
                ctx.strokeStyle = 'rgba(77,225,193,.25)';
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.ellipse(cx, cy, 105 * scale, 68 * scale, 0, 0, Math.PI * 2);
                ctx.stroke();

                fill.style.width = Math.round(t * 100) + '%';
                if (t > 0.7) cap.textContent = 'Rover online';
                if (t >= 1) {
                    finish();
                    return;
                }
                raf = requestAnimationFrame(draw);
            }

            resize();
            window.addEventListener('resize', resize, { passive: true });
            raf = requestAnimationFrame(draw);
            boot.addEventListener('click', finish, { once: true });
            setTimeout(finish, 1900);
        })();

    </script>

    <script>
        let flashOn = false;

        async function toggleFlash() {
            try {
                const response = await fetch(
                    '/flash?state=' + (flashOn ? 'off' : 'on')
                );

                if (!response.ok) throw new Error('Flash command failed');

                flashOn = !flashOn;

                document.getElementById('flashStatus').textContent =
                    flashOn ? 'ON' : 'OFF';

                document.getElementById('flashButton').textContent =
                    flashOn ? 'FLASHLIGHT OFF' : 'FLASHLIGHT ON';

            } catch (error) {
                document.getElementById('flashStatus').textContent =
                    'Command failed';
            }
        }

        async function sendCommand(command) {
            try {
                const response = await fetch(
                    '/command?value=' + encodeURIComponent(command)
                );

                if (!response.ok) throw new Error('Command failed');

                if (command === 'a') {
                    document.getElementById('mode').textContent = 'Automatic';
                } else if (command === 'm') {
                    document.getElementById('mode').textContent = 'Manual';
                }

            } catch (error) {
                document.getElementById('sensorStatus').textContent =
                    'Unable to send command';
            }
        }

        async function updateSensor() {
            try {
                const response = await fetch('/sensor');

                if (!response.ok) throw new Error('Sensor request failed');

                const data = await response.json();

                document.getElementById('temp').textContent =
                    data.dhtValid ? data.temperature : '--';

                document.getElementById('hum').textContent =
                    data.dhtValid ? data.humidity : '--';

                document.getElementById('gas').textContent =
                    data.gasDetected ? 'GAS DETECTED' : 'No gas threshold detected';

                document.getElementById('sensorStatus').textContent =
                    data.dhtValid ? 'Sensors connected' : 'DHT11 reading unavailable';

            } catch (error) {
                document.getElementById('sensorStatus').textContent =
                    'Sensor connection error';
            }
        }

        setInterval(updateSensor, 2000);
        updateSensor();
    </script>

</body>

</html>
)rawliteral";

  // The camera stream runs on port 81. Use the actual AP address.
  String streamUrl = String("src=\"http://") + WiFi.softAPIP().toString() + ":81/stream\"";
  html.replace("src=\"/streamview\"", streamUrl);

  server.send(200, "text/html", html);
}

// ---------------- FLASHLIGHT CONTROL ----------------

void handleFlash() {
  if (!server.hasArg("state")) {
    server.send(400, "text/plain", "Missing state");
    return;
  }

  String state = server.arg("state");
  state.toLowerCase();

  if (state == "on") {
    digitalWrite(LED_PIN, HIGH);
    flashOn = true;
    server.send(200, "text/plain", "Flashlight ON");
  }
  else if (state == "off") {
    digitalWrite(LED_PIN, LOW);
    flashOn = false;
    server.send(200, "text/plain", "Flashlight OFF");
  }
  else {
    server.send(400, "text/plain", "Invalid state");
  }
}

// ---------------- MOTOR COMMANDS ----------------

void handleCommand() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing command");
    return;
  }

  String command = server.arg("value");
  command.toLowerCase();

  if (command == "a") {
    MOTOR_SERIAL.write('a');
  }
  else if (command == "m") {
    MOTOR_SERIAL.write('m');
  }
  else if (command == "f") {
    MOTOR_SERIAL.write('f');
  }
  else if (command == "b") {
    MOTOR_SERIAL.write('b');
  }
  else if (command == "l") {
    MOTOR_SERIAL.write('l');
  }
  else if (command == "r") {
    MOTOR_SERIAL.write('r');
  }
  else if (command == "s") {
    MOTOR_SERIAL.write('s');
  }
  else {
    server.send(400, "text/plain", "Invalid command");
    return;
  }

  server.send(200, "text/plain", "Command sent");
}

// ---------------- SENSOR API ----------------

void handleSensor() {
  // Common MQ-2 modules pull DO LOW when their
  // adjustable threshold is crossed.
  gasDetected = (digitalRead(MQ2_DO_PIN) == LOW);

  String json = "{";

  json += "\"temperature\":";
  json += String(temperature, 1);

  json += ",\"humidity\":";
  json += String(humidity, 1);

  json += ",\"dhtValid\":";
  json += (dhtValid ? "true" : "false");

  json += ",\"gasDetected\":";
  json += (gasDetected ? "true" : "false");

  json += "}";

  server.send(200, "application/json", json);
}

// ---------------- SETUP ----------------

void setup() {
  // UART0 TX GPIO 1 sends motor commands.
  // Avoid Serial.print() debug output.
  MOTOR_SERIAL.begin(9600);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  flashOn = false;

  pinMode(MQ2_DO_PIN, INPUT);

  dht.begin();

  // Start Wi-Fi first so the dashboard remains reachable even if the
  // camera sensor fails to initialize.
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192,168,4,1),
                    IPAddress(192,168,4,1),
                    IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  // Start camera and its separate streaming server on port 81.
  // Camera failure does not prevent the sensor/motor dashboard from loading.
  startCamera();

  server.on("/", handleRoot);
  server.on("/command", handleCommand);
  server.on("/sensor", handleSensor);
  server.on("/flash", handleFlash);

  server.begin();

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t) && !isnan(h)) {
    temperature = t;
    humidity = h;
    dhtValid = true;
  }

  lastDHTRead = millis();
}

// ---------------- MAIN LOOP ----------------

void loop() {
  server.handleClient();

  if (millis() - lastDHTRead >= 2000) {
    lastDHTRead = millis();

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      temperature = t;
      humidity = h;
      dhtValid = true;
    } else {
      dhtValid = false;
    }
  }
}

