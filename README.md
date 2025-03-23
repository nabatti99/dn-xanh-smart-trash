# All Embedded System of Smart Recycle Bin

The system contain 3 main parts:
1. **ESP32 Main**: The main central ESP32 that controls one physical trash bin.
2. **ESP32 Front**: Integrated with a camera to detect the type of trash and control ESP32 Main.
3. **ESP32 Camera**: A camera module that captures images of the trash and sends them to the ESP32 Front for classification.