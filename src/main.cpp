#include <Arduino.h>
#include "app/app.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Geek Terminal Boot ===");

  if (!App::begin()) {
    Serial.println("[FATAL] App init failed, halt");
    while (true) delay(1000);
  }
  Serial.println("[BOOT] Geek Terminal ready");
}

void loop() {
  App::loop();
  delay(5);
}