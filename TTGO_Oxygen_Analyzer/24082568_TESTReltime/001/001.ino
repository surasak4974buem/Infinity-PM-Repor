#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>

// --- ส่วนตั้งค่าเซ็นเซอร์ OCS-3F ---
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
#define SENSOR_BAUD_RATE 9600
HardwareSerial O2_Sensor(2); // ใช้ Hardware Serial Port 2 ของ ESP32

// --- ส่วนตั้งค่าจอ TFT ---
TFT_eSPI tft = TFT_eSPI(135, 240); 

// --- ตัวแปรสำหรับเก็บค่า ---
byte data_buffer[9];
float oxygen_concentration = 0.0;
float flow_rate = 0.0;

void setup() {
  // เริ่มการทำงานของ Serial Monitor สำหรับ Debug
  Serial.begin(115200);
  Serial.println("O2 Sensor with TTGO Display");

  // เริ่มการทำงานของ Serial สำหรับ Sensor
  O2_Sensor.begin(SENSOR_BAUD_RATE, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // --- เริ่มต้นการทำงานของหน้าจอ ---
  tft.init();
  tft.setRotation(1); // ตั้งค่าการหมุนจอ (อาจจะเป็น 0, 1, 2, 3 แล้วแต่การติดตั้ง)
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);

  // --- วาด UI พื้นฐานบนหน้าจอ ---
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setCursor(10, 20);
  tft.println("Oxygen Sensor");
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 60);
  tft.print("O2:");
  
  tft.setCursor(10, 95);
  tft.print("Flow:");
}

void loop() {
  // ตรวจสอบและอ่านข้อมูลจากเซ็นเซอร์
  if (readSensor()) {
    // ถ้าอ่านข้อมูลสำเร็จ ให้อัปเดตหน้าจอ
    updateDisplay();
  }
}

bool readSensor() {
  // ตรวจสอบว่ามีข้อมูลเข้ามาอย่างน้อย 9 bytes (ขนาดของ 1 packet) หรือไม่
  if (O2_Sensor.available() >= 9) {
    // มองหา Byte เริ่มต้นของ Packet (0xFF)
    if (O2_Sensor.read() == 0xFF) {
      // อ่านข้อมูลที่เหลืออีก 8 bytes เก็บใน buffer
      O2_Sensor.readBytes(data_buffer, 8);

      // Byte ที่ 0 ของ buffer คือ Header (ควรจะเป็น 0xD7 สำหรับรุ่น OCS-3F)
      if (data_buffer[0] == 0xD7) {
        // --- คำนวณ Checksum ---
        // ผลรวมของ byte ที่ 1 ถึง 6 (ใน buffer)
        int sum = 0;
        for (int i = 1; i <= 6; i++) {
          sum += data_buffer[i];
        }
        byte checksum = (byte)(~sum); // ~ คือ bitwise NOT
        
        // --- ตรวจสอบ Checksum ---
        // Byte ที่ 7 ของ buffer คือ checksum ที่ส่งมาจากเซ็นเซอร์
        if (checksum == data_buffer[7]) {
          // ถ้าข้อมูลถูกต้อง ให้ทำการแปลงค่า
          // ค่าความเข้มข้นออกซิเจน (2 bytes)
          oxygen_concentration = (float)((data_buffer[1] << 8) | data_buffer[2]) / 10.0f;
          
          // ค่าอัตราการไหล (2 bytes)
          flow_rate = (float)((data_buffer[3] << 8) | data_buffer[4]) / 100.0f;

          return true; // คืนค่าว่าอ่านข้อมูลสำเร็จ
        }
      }
    }
  }
  return false; // คืนค่าว่าอ่านข้อมูลไม่สำเร็จ
}

void updateDisplay() {
  // --- อัปเดตค่า Oxygen ---
  tft.setTextSize(3);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  // เคลียร์พื้นที่เก่าก่อนวาดใหม่ (วาดสี่เหลี่ยมสีดำทับ)
  tft.fillRect(80, 55, 150, 30, TFT_BLACK); 
  tft.setCursor(80, 60);
  tft.print(oxygen_concentration, 1); // แสดงทศนิยม 1 ตำแหน่ง
  tft.print(" %");

  // --- อัปเดตค่า Flow ---
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.fillRect(80, 90, 150, 30, TFT_BLACK);
  tft.setCursor(80, 95);
  tft.print(flow_rate, 2); // แสดงทศนิยม 2 ตำแหน่ง
  tft.print(" L/m");
}
