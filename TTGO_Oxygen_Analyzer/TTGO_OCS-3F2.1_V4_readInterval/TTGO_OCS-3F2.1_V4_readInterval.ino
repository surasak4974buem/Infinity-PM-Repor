#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>

// --- ส่วนตั้งค่าจอ TFT ---
TFT_eSPI tft = TFT_eSPI(135, 240);

// --- ส่วนตั้งค่าเซ็นเซอร์ ---
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

// =================================================================
// ★★ แก้ไขตรงนี้เพื่อกำหนดเวลาในการอ่านค่า (หน่วยเป็นมิลลิวินาที) ★★
// 1 วินาที = 1000
// 1 นาที = 60000
const long readInterval = 1000; // ตอนนี้ตั้งไว้ที่ 1 นาที
// =================================================================

// ตัวแปรสำหรับจับเวลา
unsigned long previousMillis = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor Reader with Custom Interval");
  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // --- เริ่มต้นการทำงานของจอ TFT ---
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  // --- วาดหัวข้อต่างๆ ลงบนจอ ---
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Oxygen:", 10, 20, 2); 
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Flow:", 10, 55, 2);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp:", 10, 90, 2);
}

void loop() {
  // --- ส่วนของการจับเวลา ---
  // ตรวจสอบว่าถึงเวลาที่กำหนดหรือยัง
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= readInterval) {
    // ถ้าถึงเวลาแล้ว ให้บันทึกเวลาปัจจุบันไว้สำหรับการนับรอบถัดไป
    previousMillis = currentMillis;

    // --- เริ่มกระบวนการอ่านค่าจากเซ็นเซอร์ (เหมือนเดิม) ---
    readAndDisplaySensorData();
  }
}

// ฟังก์ชันสำหรับอ่านและแสดงผลข้อมูล (แยกออกมาเพื่อความเรียบร้อย)
void readAndDisplaySensorData() {
  // รอจนกว่าจะมีข้อมูลเข้ามาครบ 12 bytes
  // เราอาจต้องรอสักพักถ้าข้อมูลยังมาไม่ครบ
  unsigned long startTime = millis();
  while (SensorSerial.available() < 12) {
    // ถ้าเกิน 2 วินาทีแล้วข้อมูลยังไม่มา ให้ยกเลิก
    if (millis() - startTime > 2000) {
      Serial.println("Timeout: No data from sensor.");
      return; 
    }
  }

  if (SensorSerial.read() == 0x16) { 
    byte data[12];
    data[0] = 0x16;
    SensorSerial.readBytes(&data[1], 11);

    byte checksum = 0;
    for (int i = 0; i < 11; i++) {
      checksum += data[i];
    }
    
    if (((checksum + data[11]) & 0xFF) == 0) {
      float concentration = (data[3] * 256 + data[4]) / 10.0;
      float flow = (data[5] * 256 + data[6]) / 10.0;
      float temperature = (data[7] * 256 + data[8]) / 10.0;

      Serial.printf("O2: %.1f %% | Flow: %.1f L/min | Temp: %.1f C\n", concentration, flow, temperature);

      // --- อัปเดตค่าบนหน้าจอ TFT ---
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.fillRect(95, 20, 120, 18, TFT_BLACK); 
      tft.drawString(String(concentration, 1) + " %", 100, 20, 2);

      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.fillRect(95, 55, 120, 18, TFT_BLACK);
      tft.drawString(String(flow, 1) + " L/m", 100, 55, 2);

      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.fillRect(95, 90, 120, 18, TFT_BLACK);
      tft.drawString(String(temperature, 1) + " C", 100, 90, 2);

    } else {
      Serial.println("Checksum error!");
      tft.fillRect(0, 115, 240, 20, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("Checksum Error!", 10, 115, 2);
    }
  }
  // ล้าง buffer ที่อาจมีข้อมูลขยะค้างอยู่
  while(SensorSerial.available()) {
    SensorSerial.read();
  }
}
