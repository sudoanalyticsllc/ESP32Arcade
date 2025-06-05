# ESP32Arcade

This Arduino sketch turns an ESP32 into a Wi-Fi AP with a login page, user management, and an embedded web-based NES emulator.

## Required Libraries

Install these from Arduino Library Manager (Sketch → Include Library → Manage Libraries…):

- **ESPAsyncWebServer**  
- **AsyncTCP**  
- **ArduinoJson**  
- **LittleFS_esp32** (or **LittleFS** if your board package already bundles it)  
- **mbedtls** (comes with ESP32 core; no separate install needed)

## How to Upload

1. Open `ESP32Arcade.ino` in Arduino IDE.
2. Select Board: **ESP32 Dev Module** (e.g. from Tools → Board → ESP32 Arduino).
3. Set Upload Speed to 921600 (optional, but faster).
4. Compile & Upload.
5. After reboot, ESP32 will broadcast SSID `ESP32_Arcade_AP` (password `ChangeMe1!`).  
   - Default admin login:  
     - **Username:** Admin  
     - **Password:** ChangeMe1!  
   - On first login, you will be forced to change that password.
