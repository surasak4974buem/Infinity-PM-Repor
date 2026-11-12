// Learn Applied Physics 
// https://catatanfisikawan.blogspot.com/
// Modified for ESP32 TTGO board

#include <Wire.h>           // ไลบรารีสำหรับการสื่อสาร I2C (ถ้าต้องการใช้)
#include <TFT_eSPI.h>      // ไลบรารีสำหรับจอแสดงผล TFT ของ TTGO
#include <SPI.h>           // ไลบรารีสำหรับการสื่อสาร SPI

TFT_eSPI tft = TFT_eSPI(135, 240); // สร้าง object สำหรับจอแสดงผล ขนาด 135x240 พิกเซล

// กำหนดขา GPIO สำหรับ ESP32 TTGO
#define RXD2 16            // ขาสำหรับรับข้อมูล Serial2
#define TXD2 17            // ขาสำหรับส่งข้อมูล Serial2

void setup() {
  Serial.begin(115200);           // เริ่มต้น Serial Monitor ที่ความเร็ว 115200 baud
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); // เริ่มต้น Serial2 สำหรับรับข้อมูลจากเซ็นเซอร์
  
  // เริ่มต้นจอแสดงผล TFT
  tft.init();                     // เริ่มต้นจอ TFT
  tft.setRotation(1);            // หมุนจอ 90 องศา (landscape)
  tft.fillScreen(TFT_BLACK);     // เคลียร์จอด้วยสีดำ
  tft.setTextColor(TFT_WHITE);   // กำหนดสีตัวอักษรเป็นสีขาว
  tft.setTextSize(3);            // กำหนดขนาดตัวอักษรเป็น 3 (ใหญ่ขึ้น)
  
  Serial.println("ESP32 TTGO Oxygen Sensor Started"); // แสดงข้อความเริ่มต้น
  tft.setCursor(0, 0);           // กำหนดตำแหน่งเคอร์เซอร์
  tft.println("O2 Sensor Ready"); // แสดงข้อความบนจอ
}

void loop() {
  typedef unsigned char u8;      // กำหนดชนิดข้อมูล unsigned char เป็น u8
  typedef unsigned int u16;      // กำหนดชนิดข้อมูล unsigned int เป็น u16
  
  static int inByte;             // ตัวแปรเก็บข้อมูลที่รับเข้ามา (static เพื่อเก็บค่าระหว่าง loop)
  static u8 temp;                // ตัวแปรเก็บค่าชั่วคราวสำหรับการตรวจสอบ checksum
  static u8 i = 0, j;            // i = ตัวนับตำแหน่งใน array, j = ตัวนับสำหรับ loop
  static u8 o2[12];              // array เก็บข้อมูล 12 bytes จากเซ็นเซอร์
  static u16 o2c = 0, o2f = 0, o2t = 0; // ตัวแปรเก็บค่าความเข้มข้น O2, อัตราการไหล, อุณหภูมิ

  // เมื่อมีข้อมูลเข้ามาทาง Serial2
  if (Serial2.available()) {
    delay(100);                  // รอให้ข้อมูลครบ 100ms
    
    // อ่านข้อมูลทั้งหมดที่มีอยู่ใน buffer
    while (Serial2.available() > 0) {
      inByte = Serial2.read();   // อ่านข้อมูล 1 byte
      
      //---ส่วนรับข้อมูล---
      // ตรวจสอบว่า header 3 bytes แรกถูกต้อง (0x16, 0x09, 0x01)
      if ((o2[0] == 0x16) && (o2[1] == 0x09) && (o2[2] == 0x01)) {
        o2[i] = inByte;          // เก็บข้อมูลใน array
        i++;                     // เพิ่มตำแหน่ง array
      }
      else {
        // ถ้า header ไม่ถูกต้อง ตรวจสอบทีละ byte
        if ((o2[0] == 0x16) && (o2[1] == 0x09)) {
          if (inByte == 0x01) {  // ถ้า byte ที่ 3 ถูกต้อง
            o2[2] = inByte;      // เก็บ byte ที่ 3
            i++;                 // เพิ่มตำแหน่ง
          }
          else {
            i = 0;               // reset ตำแหน่ง
            for (j = 0; j < 12; j++) { // เคลียร์ array
              o2[j] = 0;
            }
          }
        }
        else {
          if (o2[0] == 0x16) {   // ถ้า byte แรกถูกต้อง
            if (inByte == 0x09) { // ถ้า byte ที่ 2 ถูกต้อง
              o2[1] = inByte;    // เก็บ byte ที่ 2
              i++;               // เพิ่มตำแหน่ง
            }
            else {
              i = 0;             // reset ตำแหน่ง
              for (j = 0; j < 12; j++) { // เคลียร์ array
                o2[j] = 0;
              }
            }
          }
          else {
            if (inByte == 0x16) { // ถ้าเป็น byte แรกของ header
              o2[0] = inByte;    // เก็บ byte แรก
              i = 1;             // ตั้งตำแหน่งเป็น 1
            }
            else {
              i = 0;             // reset ตำแหน่ง
              for (j = 0; j < 12; j++) { // เคลียร์ array
                o2[j] = 0;
              }
            }
          }
        }
      }
      //---จบส่วนรับข้อมูล---

      // เมื่อรับข้อมูลครบ 12 bytes
      if (i == 12) {
        temp = 0;                // เคลียร์ตัวแปร temp
        
        // คำนวณ checksum (รวมทุก byte ต้องเท่ากับ 0)
        for (j = 0; j < 12; j++) {
          temp += o2[j];         // รวมค่าทุก byte
        }
        
        if (temp == 0) {         // ถ้า checksum ถูกต้อง
          // แปลงข้อมูล 2 bytes เป็นค่าจริง
          o2c = o2[3] * 256 + o2[4]; // ความเข้มข้นออกซิเจน (bytes 3-4)
          o2f = o2[5] * 256 + o2[6]; // อัตราการไหล (bytes 5-6)
          o2t = o2[7] * 256 + o2[8]; // อุณหภูมิ (bytes 7-8)
        }

        i = 0;                   // reset ตำแหน่ง array
        for (j = 0; j < 12; j++) { // เคลียร์ array เตรียมรับข้อมูลชุดใหม่
          o2[j] = 0;
        }
      }
    }
  }

  // แสดงผลข้อมูลออกซิเจน
  Serial.print("O2 : ");        // แสดงหัวข้อ
  Serial.print(o2c/100);         // หลักร้อย
  Serial.print((o2c/10)%10);     // หลักสิบ
  Serial.print(".");             // จุดทศนิยม
  Serial.print(o2c%10);          // หลักหน่วย
  Serial.println("%");           // เครื่องหมาย % และขึ้นบรรทัดใหม่

  // แสดงผลอัตราการไหล
  Serial.print("Flow : ");       // แสดงหัวข้อ
  Serial.print((o2f/10)%10);     // หลักหน่วย
  Serial.print(".");             // จุดทศนิยม
  Serial.print(o2f%10);          // หลักทศนิยม
  Serial.println("L/min");       // หน่วย และขึ้นบรรทัดใหม่

  // แสดงผลอุณหภูมิ
  Serial.print("Temp : ");       // แสดงหัวข้อ
  Serial.print(o2t/100);         // หลักร้อย
  Serial.print((o2t/10)%10);     // หลักสิบ
  Serial.print(".");             // จุดทศนิยม
  Serial.print(o2t%10);          // หลักหน่วย
  Serial.println(" Celsius");    // หน่วย และขึ้นบรรทัดใหม่

  // แสดงผลบนจอ TFT
  tft.fillScreen(TFT_BLACK);     // เคลียร์จอ
  tft.setCursor(0, 0);           // ย้ายเคอร์เซอร์ไปตำแหน่งเริ่มต้น
  
  // แสดงค่าออกซิเจน
  tft.setTextColor(TFT_GREEN);   // สีเขียวสำหรับ O2
  tft.print("O2: ");
  tft.print(o2c/100);
  tft.print((o2c/10)%10);
  tft.print(".");
  tft.print(o2c%10);
  tft.println(" %");
  
  // แสดงอัตราการไหล
  tft.setTextColor(TFT_BLUE);    // สีน้ำเงินสำหรับ Flow
  tft.print("Flow: ");
  tft.print((o2f/10)%10);
  tft.print(".");
  tft.print(o2f%10);
  tft.println(" L");
  
  // แสดงอุณหภูมิ
  tft.setTextColor(TFT_RED);     // สีแดงสำหรับ Temperature
  tft.print("Temp: ");
  tft.print(o2t/100);
  tft.print((o2t/10)%10);
  tft.print(".");
  tft.print(o2t%10);
  tft.println(" C");

  delay(2000);                   // หน่วงเวลา 2 วินาที ก่อนวนซ้ำ
}
