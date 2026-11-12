#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>

// --- ส่วนตั้งค่าจอ TFT ---
TFT_eSPI tft = TFT_eSPI(135, 240); // สร้าง object สำหรับจอแสดงผล

// --- ส่วนตั้งค่าเซ็นเซอร์ ---
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2); // ใช้ Hardware Serial Port 2

void setup() {
  // เริ่มการทำงานของ Serial Monitor สำหรับดีบัก
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor Reader with TFT Display");

  // เริ่มการทำงานของ Serial Port 2 เพื่อสื่อสารกับเซ็นเซอร์
  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // --- เริ่มต้นการทำงานของจอ TFT ---
  tft.init();
  tft.setRotation(1); // ตั้งค่าการหมุนจอ (1 หรือ 3 สำหรับแนวนอน)
  tft.fillScreen(TFT_BLACK); // เคลียร์จอเป็นสีดำ
  tft.setTextSize(2); // ตั้งค่าขนาดตัวอักษร

  // --- วาดหัวข้อต่างๆ ลงบนจอ (ทำครั้งเดียว) ---
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Oxygen:", 10, 20, 2); // (text, x, y, font_number)

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Flow:", 10, 55, 2);
  
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp:", 10, 90, 2);
}

void loop() {
  // ตรวจสอบว่ามีข้อมูลจากเซ็นเซอร์เข้ามาหรือไม่
  if (SensorSerial.available() >= 12) {
    if (SensorSerial.read() == 0x16) { // ตรวจสอบ Start Byte
      byte data[12];
      data[0] = 0x16;
      SensorSerial.readBytes(&data[1], 11);

      // ตรวจสอบ Checksum
      byte checksum = 0;
      for (int i = 0; i < 11; i++) {
        checksum += data[i];
      }
      
      if (((checksum + data[11]) & 0xFF) == 0) {
        // --- Checksum ถูกต้อง ---
        
        // แปลงค่าข้อมูล
        float concentration = (data[3] * 256 + data[4]) / 10.0;
        float flow = (data[5] * 256 + data[6]) / 10.0;
        float temperature = (data[7] * 256 + data[8]) / 10.0;

        // แสดงผลบน Serial Monitor (เผื่อไว้ดีบัก)
        Serial.printf("O2: %.1f %% | Flow: %.1f L/min | Temp: %.1f C\n", concentration, flow, temperature);

        // --- อัปเดตค่าบนหน้าจอ TFT ---
        // ตั้งค่าสีและขนาดตัวอักษรสำหรับ "ค่า" ที่จะแสดง
        tft.setTextSize(3);

        // 1. แสดงค่า Oxygen
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        // เคลียร์พื้นที่เก่าก่อนวาดใหม่ เพื่อป้องกันตัวเลขซ้อนกัน
        tft.fillRect(110, 15, 120, 28, TFT_BLACK); 
        tft.drawString(String(concentration, 1) + " %", 115, 20, 2);

        // 2. แสดงค่า Flow
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.fillRect(110, 50, 120, 28, TFT_BLACK);
        tft.drawString(String(flow, 1) + " L/m", 115, 55, 2);

        // 3. แสดงค่า Temperature
        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.fillRect(110, 85, 120, 28, TFT_BLACK);
        tft.drawString(String(temperature, 1) + " C", 115, 90, 2);

      } else {
        Serial.println("Checksum error!");
        tft.fillRect(0, 115, 240, 20, TFT_BLACK); // เคลียร์แถบสถานะ
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Checksum Error!", 10, 115, 2);
      }
    }
  }
}
