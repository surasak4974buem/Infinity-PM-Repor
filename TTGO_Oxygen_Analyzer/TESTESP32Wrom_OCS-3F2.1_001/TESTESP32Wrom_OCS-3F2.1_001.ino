// ESP32 + OCS-3F Oxygen Sensor
// RXD2 = GPIO16, TXD2 = GPIO17

#define RXD2 16
#define TXD2 17

// เลือกโหมด: true = Query mode, false = Auto-send mode
//#define USE_QUERY_MODE false  
#define USE_QUERY_MODE true
uint8_t buffer[20];

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial.println("OCS-3F Oxygen Sensor Test Start...");
}

void loop() {
  if (USE_QUERY_MODE) {
    queryModeLoop();
  } else {
    autoSendModeLoop();
  }
}

// ---------------------- Auto-send Mode ----------------------
void autoSendModeLoop() {
  if (Serial2.available() >= 13) {   // Auto-send frame length = 13 bytes
    int len = Serial2.readBytes(buffer, 13);

    if (checkHeader(buffer) && checkChecksum(buffer, 13)) {
      printSensorData(buffer);
    }
  }
}

// ---------------------- Query Mode ----------------------
void queryModeLoop() {
  static unsigned long lastSend = 0;

  // ส่งคำสั่งทุก 1 วินาที
  if (millis() - lastSend > 1000) {
    uint8_t queryCmd[4] = {0x11, 0x01, 0x01, 0xED};
    Serial2.write(queryCmd, 4);
    Serial.println("Query command sent...");
    lastSend = millis();
  }

  // คำตอบมี 12 bytes
  if (Serial2.available() >= 12) {
    int len = Serial2.readBytes(buffer, 12);

    if (checkHeader(buffer) && checkChecksum(buffer, 12)) {
      printSensorData(buffer);
    }
  }
}

// ---------------------- Helper Functions ----------------------
bool checkHeader(uint8_t *buf) {
  return (buf[0] == 0x16 && buf[1] == 0x09 && buf[2] == 0x01);
}

bool checkChecksum(uint8_t *buf, int len) {
  uint8_t checksum = 0;
  for (int i = 0; i < len - 1; i++) checksum += buf[i];
  checksum = (~checksum) + 1;
  return (checksum == buf[len - 1]);
}

void printSensorData(uint8_t *buf) {
  int conc  = (buf[3] << 8) | buf[4];   // 0.1 %
  int flow  = (buf[5] << 8) | buf[6];   // 0.1 L/min
  int temp  = (buf[7] << 8) | buf[8];   // 0.1 °C

  Serial.print("O2 Concentration: ");
  Serial.print(conc / 10.0);
  Serial.println(" %");

  Serial.print("Flow: ");
  Serial.print(flow / 10.0);
  Serial.println(" L/min");

  Serial.print("Temperature: ");
  Serial.print(temp / 10.0);
  Serial.println(" °C");

  Serial.println("---------------------");
}
