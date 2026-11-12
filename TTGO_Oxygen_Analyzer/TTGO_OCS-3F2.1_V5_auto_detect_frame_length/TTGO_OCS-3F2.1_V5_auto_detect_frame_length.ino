#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>

// --- TFT Setup ---
TFT_eSPI tft = TFT_eSPI(135, 240);

// --- Sensor Setup ---
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

// --- ตัวแปรเก็บค่าล่าสุด (global) ---
float latestO2 = 0.0;
float latestFlow = 0.0;
float latestTemp = 0.0;

// --- ตั้งเวลาอ่านค่า (ms) ---
const long readInterval = 1000;
unsigned long previousMillis = 0;

// --- Frame buffer สำหรับ auto detect length ---
#define MAX_FRAME 20
byte frameBuffer[MAX_FRAME];
int frameIndex = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor Reader with TFT");

  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // --- TFT Init ---
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // --- แสดงหัวข้อบน TFT ---
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Oxygen:", 10, 20, 2); 

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Flow:", 10, 55, 2);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp:", 10, 90, 2);
}

void loop() {
  // --- อ่านค่าตาม interval ---
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= readInterval) {
    previousMillis = currentMillis;
    readSensorSerial();
    displayOnTFT();
  }
}

// --- ฟังก์ชันอ่านค่าจาก Sensor ---
void readSensorSerial() {
  while (SensorSerial.available()) {
    byte b = SensorSerial.read();

    if (frameIndex == 0 && b != 0x16) continue; // start byte

    frameBuffer[frameIndex++] = b;

    if (frameIndex >= MAX_FRAME) {
      for (int i = 1; i < MAX_FRAME; i++) frameBuffer[i-1] = frameBuffer[i];
      frameIndex = MAX_FRAME - 1;
    }

    if (frameIndex >= 3) { // byte[2] = payload length
      int expectedLen = 3 + frameBuffer[2];
      if (frameIndex >= expectedLen + 1) { // +1 checksum
        byte checksum = 0;
        for (int i = 0; i < expectedLen; i++) checksum += frameBuffer[i];

        if (((checksum + frameBuffer[expectedLen]) & 0xFF) == 0) {
          latestO2 = (frameBuffer[3]*256 + frameBuffer[4])/10.0;
          latestFlow = (frameBuffer[5]*256 + frameBuffer[6])/10.0;
          latestTemp = (frameBuffer[7]*256 + frameBuffer[8])/10.0;
          Serial.printf("O2: %.1f %% | Flow: %.1f L/min | Temp: %.1f C\n", latestO2, latestFlow, latestTemp);
        } else {
          Serial.println("Checksum error!");
        }
        frameIndex = 0; // reset buffer
      }
    }
  }
}

// --- ฟังก์ชันแสดงผลบน TFT ---
void displayOnTFT() {
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillRect(95, 20, 120, 18, TFT_BLACK); 
  tft.drawString(String(latestO2, 1) + " %", 100, 20, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.fillRect(95, 55, 120, 18, TFT_BLACK);
  tft.drawString(String(latestFlow, 1) + " L/m", 100, 55, 2);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.fillRect(95, 90, 120, 18, TFT_BLACK);
  tft.drawString(String(latestTemp, 1) + " C", 100, 90, 2);
}
