#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// --- TFT Display ---
TFT_eSPI tft = TFT_eSPI(135, 240);

// --- Sensor Serial ---
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

// --- BLE UUIDs ---
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcd1234-1234-1234-1234-abcdef123456"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

// Callback สำหรับเชื่อมต่อ/ตัดการเชื่อมต่อ
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) { deviceConnected = false; }
};

void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor Reader with TFT + BLE");

  // --- Sensor Serial ---
  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // --- TFT init ---
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Oxygen:", 10, 20, 2); 
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Flow:", 10, 55, 2);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp:", 10, 90, 2);

  // --- BLE init ---
  BLEDevice::init("TTGO_BLE");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_READ
                    );

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  Serial.println("BLE ready, waiting for connection...");
}

void loop() {
  if (SensorSerial.available() >= 12) {
    if (SensorSerial.read() == 0x16) {
      byte data[12];
      data[0] = 0x16;
      SensorSerial.readBytes(&data[1], 11);

      byte checksum = 0;
      for (int i = 0; i < 11; i++) checksum += data[i];

      if (((checksum + data[11]) & 0xFF) == 0) {
        // --- Parse sensor data ---
        float concentration = (data[3] * 256 + data[4]) / 10.0;
        float flow = (data[5] * 256 + data[6]) / 10.0;
        float temperature = (data[7] * 256 + data[8]) / 10.0;

        Serial.printf("O2: %.1f %% | Flow: %.1f L/min | Temp: %.1f C\n",
                      concentration, flow, temperature);

        // --- Update TFT ---
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.fillRect(95, 20, 120, 18, TFT_BLACK); 
        tft.drawString(String(concentration, 1) + " %", 100, 20, 2);

        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.fillRect(95, 55, 120, 18, TFT_BLACK);
        tft.drawString(String(flow, 1) + " L/m", 100, 55, 2);

        tft.setTextColor(TFT_ORANGE, TFT_BLACK);
        tft.fillRect(95, 90, 120, 18, TFT_BLACK);
        tft.drawString(String(temperature, 1) + " C", 100, 90, 2);

        // --- ส่งข้อมูลทาง BLE ---
        if (deviceConnected) {
          String bleMsg = "O2:" + String(concentration,1) + 
                          "% | Flow:" + String(flow,1) + 
                          "L/m | Temp:" + String(temperature,1) + "C";
          pCharacteristic->setValue(bleMsg.c_str());
          pCharacteristic->notify();
        }
      } else {
        Serial.println("Checksum error!");
        tft.fillRect(0, 115, 240, 20, TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Checksum Error!", 10, 115, 2);
      }
    }
  }
}
