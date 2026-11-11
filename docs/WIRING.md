# Wiring reference

These connections are derived from the supplied sketches. Check your exact module pin labels before powering the circuit.

## Arduino Uno to motor driver

| Arduino Uno pin | Motor driver pin |
|---|---|
| D5 | ENA |
| D7 | IN1 |
| D8 | IN2 |
| D6 | ENB |
| D9 | IN3 |
| D10 | IN4 |

The motor driver outputs connect to the two DC motors. Motor power must come from a suitable motor supply, not from the Uno 5 V pin. If your driver has ENA/ENB jumpers, configure them appropriately for PWM speed control.

## HC-SR04 to Arduino Uno

| HC-SR04 pin | Arduino Uno |
|---|---|
| TRIG | D11 |
| ECHO | D12 |
| VCC | Sensor-rated supply (commonly 5 V) |
| GND | GND |

## ESP32-CAM sensors and flash LED

| Module signal | ESP32-CAM pin |
|---|---|
| DHT11 DATA | GPIO13 |
| MQ-2 DO | GPIO14 |
| Flash LED control | GPIO4 |

Power DHT11 according to its module specification. **Never connect a 5 V logic output directly to an ESP32-CAM GPIO.** Many MQ-2 modules are powered at 5 V and may output 5 V on DO; use a suitable logic-level shifter or resistor divider before GPIO14. Verify the actual output voltage with a meter.

## ESP32-CAM to Arduino Uno serial link

| ESP32-CAM | Arduino Uno |
|---|---|
| GPIO1 / U0T / TX | D0 / RX |
| GND | GND |

The sketches use 9600 baud. The ESP32-CAM sends commands to the Uno; the provided code does not require the Uno TX line to be connected back to the ESP32. Disconnect the TX-to-RX link while uploading sketches if it interferes with programming.

## Ground and power

- Connect the grounds of the ESP32-CAM supply, Arduino Uno, motor driver logic, and sensor supply together where the circuits interface.
- Use a stable regulated supply suitable for the ESP32-CAM's peak current. Do not power motors from the ESP32-CAM or Uno logic regulator.
- Do not connect a 7.4 V battery directly to the ESP32-CAM 5 V or 3.3 V pins.
- Confirm motor-driver voltage/current ratings and battery polarity before connecting motors.
- Test with the wheels raised and have a physical way to disconnect motor power.
