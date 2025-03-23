# Central ESP32 and Sensors in Smart Recycle Bin

This repository contains the code for the central ESP32 in the Smart Recycle Bin project. The central ESP32 is responsible for controlling the all of the sensors. It also update data to the cloud and provide data to the screen.

## Features

-   Static WiFi AP
-   Control all of the sensors
-   Update data to the cloud
-   HTTP server
-   Websocket server

## Sensors

-   Ultrasonic sensor (HC-SR04)
-   MC38 Wired Door Window Sensor Magnetic Switch
-   Breadboard
-   Jumper wires
-   10k ohm resistor
-   30-centimeter micro USB charging cable

With other components, the central ESP32 can control the sensors and update data to the cloud.

-   ESP32-CAM module
-   Monitor

## Pins setup

1. Ultrasonic sensor:

    - Trig pin: 5 (D5)
    - Echo pin: 18 (D18)

2. MC38 Wired Door Window Sensor Magnetic Switch:
    - Signal pin: D23 (D23)
    - VCC pin: 3.3V + 10k ohm resistor
    - GND pin: GND

## Prerequisites

-   [Arduino IDE](https://www.arduino.cc/en/software)
-   [ESP32 board](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/)

Libraries:

-   [Espressif ESP32](https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json/)
-   [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON/)
-   [AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
-   [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)

## Installation

1. Clone the repository
2. Open the `dn-xanh-esp32.ino` file in the Arduino IDE
3. Install the required libraries
4. Select the ESP32 board
5. Upload the code to the ESP32
