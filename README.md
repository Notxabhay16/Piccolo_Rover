# ESP32-CAM Rover

A Wi-Fi-controlled rover built around an **AI-Thinker ESP32-CAM** and an **Arduino Uno**. The ESP32-CAM hosts a local web dashboard for driving, camera streaming, flashlight control, and sensor monitoring. The Uno controls the motors and performs basic ultrasonic obstacle avoidance in automatic mode.

## Features

- ESP32-CAM access-point dashboard (default address: `http://192.168.4.1`)
- Live camera stream on the ESP32's separate HTTP stream server (port `81`)
- Manual driving commands: forward, backward, left, right, stop
- Automatic obstacle avoidance using an HC-SR04 ultrasonic sensor
- DHT11 temperature and humidity readings
- MQ-2 digital gas-threshold status
- ESP32-CAM flash LED control
- Animated dashboard startup screen

## Repository layout

```text
.
├── firmware/
│   ├── arduino_uno/
│   │   └── arduino_uno.ino
│   └── esp32_cam/
│       ├── esp32_cam.ino
│       └── config.h.example
├── web/
│   └── index.html
├── docs/
│   └── WIRING.md
├── .gitignore
├── LICENSE
└── README.md
```

## Hardware

- AI-Thinker ESP32-CAM module
- Arduino Uno
- L298N-style dual H-bridge motor driver (the sketch uses ENA, ENB, IN1–IN4)
- Two DC motors and rover chassis
- HC-SR04 ultrasonic distance sensor
- DHT11 temperature/humidity sensor
- MQ-2 gas sensor module
- Suitable regulated power supplies and common ground wiring

See [docs/WIRING.md](docs/WIRING.md) before connecting the boards.

## Software requirements

- Arduino IDE 2.x
- ESP32 board support package by Espressif
- **DHT sensor library** by Adafruit
- **Adafruit Unified Sensor** library (dependency of the Adafruit DHT library)

The ESP32 camera, Wi-Fi, web-server, and image-conversion headers are provided by the ESP32 Arduino core; do not install a separate `esp_camera` library for a normal Espressif board-package installation.

## Setup and upload

### 1. Prepare the ESP32-CAM configuration

1. Open `firmware/esp32_cam/`.
2. Copy `config.h.example` and name the copy `config.h`.
3. Edit `config.h` and set your own access-point name and password. Use a password of at least 8 characters.
4. `config.h` is ignored by Git. Do not commit it.

### 2. Upload the Arduino Uno sketch

1. Open `firmware/arduino_uno/arduino_uno.ino` in Arduino IDE.
2. Select **Arduino Uno** and the correct port.
3. Upload the sketch.
4. The Uno uses 9600 baud serial communication to receive commands from the ESP32-CAM.

### 3. Upload the ESP32-CAM sketch

1. Install the ESP32 board package and the two libraries listed above.
2. In Arduino IDE, select **AI Thinker ESP32-CAM** (or the matching AI-Thinker board profile available in your installed core).
3. Open `firmware/esp32_cam/esp32_cam.ino`.
4. Connect the ESP32-CAM to a suitable USB-to-serial programmer, enter download mode as required by the board, and upload.
5. After uploading, disconnect GPIO0 from GND and reset the ESP32-CAM to run normally.
6. Connect a phone or computer to the Wi-Fi network configured in `config.h`.
7. Open `http://192.168.4.1` in a browser.

> **Important:** The ESP32-CAM sketch embeds its dashboard HTML directly in `handleRoot()`. The separate `web/index.html` is the supplied standalone dashboard/design copy and is not automatically uploaded or served by the firmware. Keep the embedded page and standalone copy in sync if you edit both.

## Controls and API

| Endpoint | Purpose |
|---|---|
| `GET /` | Dashboard |
| `GET /command?value=m` | Manual mode |
| `GET /command?value=a` | Automatic mode |
| `GET /command?value=f` | Forward |
| `GET /command?value=b` | Backward |
| `GET /command?value=l` | Left |
| `GET /command?value=r` | Right |
| `GET /command?value=s` | Stop |
| `GET /sensor` | JSON sensor readings |
| `GET /flash?state=on` | Turn flash LED on |
| `GET /flash?state=off` | Turn flash LED off |
| `http://192.168.4.1:81/stream` | Camera MJPEG stream |

The dashboard polls `/sensor` approximately every two seconds. The MQ-2 reading is a digital threshold indication from the module's DO pin, not a calibrated gas concentration in ppm.

## Notes and limitations

- The rover should be tested with its wheels lifted before placing it on the ground.
- Select **Manual** mode before issuing manual movement commands. **Stop** sends the stop command to the Uno.
- Automatic mode uses the HC-SR04 distance reading. A failed/timeout reading causes the Uno sketch to stop the motors.
- Camera initialization may fail for unsupported camera modules or incorrect board/pin configuration; the dashboard is designed to remain available if camera startup fails.
- The current ESP32 sketch uses UART0 (`Serial`, GPIO1 TX) to send commands to the Uno. This shares pins used by many USB-to-serial upload/debug arrangements; disconnect the Uno serial connection while uploading/debugging if it interferes.
- The code assumes the ESP32-CAM camera pin mapping for the AI-Thinker board.
- The supplied sketches are a project starting point, not a certified safety system. Do not use the rover around people, animals, stairs, roads, or hazardous environments without additional safeguards.

## Troubleshooting

**Dashboard Wi-Fi appears but the page does not load**
- Confirm that the device is connected to the rover's access point.
- Open `http://192.168.4.1` explicitly.
- Check that the ESP32-CAM has a stable regulated supply and that the serial wiring is not interfering with boot.

**Dashboard loads but camera is blank**
- Check the camera module and AI-Thinker board selection.
- Confirm the camera starts successfully; the stream endpoint is on port `81` at `/stream`.
- The dashboard can load even when camera initialization fails.

**Motors do not respond**
- Confirm common ground between ESP32-CAM, Uno, motor driver, and the relevant power-supply grounds.
- Check the Uno motor-driver pin wiring against `docs/WIRING.md`.
- Verify the serial connection from ESP32 GPIO1 (TX) to Uno RX (D0) and the shared 9600 baud rate.
- Disconnect the ESP32 TX line from Uno RX during uploads if it causes port or boot issues.

**DHT11 or MQ-2 values are missing**
- Check the sensor pin assignments in `esp32_cam.ino`.
- DHT11 data is connected to GPIO13; MQ-2 digital output is connected to GPIO14.
- Ensure the ESP32 input pins never receive 5 V.

## License

This project is distributed under the MIT License. See [LICENSE](LICENSE).
