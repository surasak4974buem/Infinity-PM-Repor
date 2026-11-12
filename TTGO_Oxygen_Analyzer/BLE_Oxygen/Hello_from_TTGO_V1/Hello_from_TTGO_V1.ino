#include "BluetoothSerial.h"
BluetoothSerial SerialBT;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("TTGO_OCS3F_Sync");  // ใช้ชื่อเดียวกับที่เจอ
  Serial.println("Bluetooth Ready. Waiting for connection...");
}

void loop() {
  // ส่งข้อความออกไปทุก 2 วิ
  static unsigned long t0 = 0;
  if (millis() - t0 > 2000) {
    t0 = millis();
    SerialBT.println("Hello from TTGO!");  // ส่งให้มือถือ
  }

  // ถ้า App ส่งข้อความเข้ามา → แสดงใน Serial Monitor
  if (SerialBT.available()) {
    String msg = SerialBT.readStringUntil('\n');
    Serial.print("Received from Phone: ");
    Serial.println(msg);
  }
}
