void setupSPIFFS() {
  SPIFFS.begin(true);
  File file = SPIFFS.open("/game.js", FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create file");
    return;
  }
  file.print(GAME_JS);
  file.close();
}