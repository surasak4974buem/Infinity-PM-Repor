#include "BluetoothSerial.h"
BluetoothSerial SerialBT;

int counter = 0;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("TTGO_OCS3F_Sync"); // Classic Bluetooth
  Serial.println("Bluetooth Ready...");
}

void loop() {
  static unsigned long lastSend = 0;
  if (SerialBT.hasClient() && millis() - lastSend > 2000) { // รอมือถือ connect
    lastSend = millis();
    counter++;
    SerialBT.println(counter);            // ส่งตัวเลข
    Serial.println(counter);              // Debug Serial Monitor
  }
}
