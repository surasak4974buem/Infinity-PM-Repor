#define RXD2 16
#define TXD2 17

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial.println("ESP32 Serial2 Loopback Test Ready...");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    Serial2.write(c);   // ส่งไปที่ Serial2
  }
  if (Serial2.available()) {
    char c = Serial2.read();
    Serial.write(c);    // อ่านกลับมาแล้วส่งไปที่ Serial Monitor
  }
}
