#include "CloudOTA.h"

const int LED_PIN = 8;

void setup() {
  Serial.begin(115200);
  setupCloud(LED_PIN);
}

void loop() {
  loopCloud(LED_PIN);
}
