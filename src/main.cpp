#include <Arduino.h>

extern "C" void app_main(void) {
  initArduino();

  Serial.begin(115200);

  while (true) {
    delay(2000);
    Serial.println("Hello");
    Serial.println(millis());
  }
}
