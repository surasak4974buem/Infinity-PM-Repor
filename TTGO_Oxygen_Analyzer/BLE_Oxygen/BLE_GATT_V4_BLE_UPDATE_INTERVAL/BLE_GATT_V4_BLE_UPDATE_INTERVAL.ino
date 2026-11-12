#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>

// --- TFT Display ---
TFT_eSPI tft = TFT_eSPI(135, 240);

// --- Sensor Serial ---
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

// --- BLE UUIDs ---
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcd1234-1234-1234-1234-abcdef123456"

// --- BLE Variables ---
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// --- Timing Variables ---
unsigned long lastBLEUpdate = 0;
unsigned long lastSensorRead = 0;
const unsigned long BLE_UPDATE_INTERVAL = 500;    // ส่ง BLE ทุก 500ms
const unsigned long SENSOR_READ_INTERVAL = 100;   // อ่าน sensor ทุก 100ms

// --- Sensor Data Variables ---
float lastConcentration = -1;
float lastFlow = -1;
float lastTemperature = -1;
bool hasNewData = false;

// Callback สำหรับเชื่อมต่อ/ตัดการเชื่อมต่อ
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("🔗 Device Connected to BLE");
      
      // แสดงสถานะบน TFT
      tft.fillRect(50, 115, 180, 18, TFT_BLACK);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("BLE: Connected", 50, 115, 2);
    }
    
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("❌ Device Disconnected from BLE");
      
      // แสดงสถานะบน TFT
      tft.fillRect(50, 115, 180, 18, TFT_BLACK);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("BLE: Waiting...", 50, 115, 2);
    }
};

// Callback สำหรับ Characteristic
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      Serial.println("📝 Data written to characteristic");
    }
    
    void onRead(BLECharacteristic *pCharacteristic) {
      Serial.println("📖 Data read from characteristic");
    }
    
    void onNotify(BLECharacteristic *pCharacteristic) {
      Serial.println("🔔 Notification sent");
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("🚀 TTGO ESP32 - OCS-3F Sensor Reader with Real-time BLE");
  Serial.println("===============================================");
  
  // --- Sensor Serial ---
  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  Serial.println("📡 Sensor Serial initialized");
  
  // --- TFT init ---
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  // หัวข้อ
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("TTGO OCS-3F Monitor", 10, 5, 2);
  
  // Labels
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Oxygen:", 10, 30, 2); 
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Flow:", 10, 55, 2);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp:", 10, 80, 2);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("BLE:", 10, 105, 2);
  
  // เริ่มต้นแสดงค่า
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("--.- %", 100, 30, 2);
  tft.drawString("--.- L/m", 100, 55, 2);
  tft.drawString("--.- C", 100, 80, 2);
  tft.drawString("Starting...", 50, 105, 2);
  
  Serial.println("🖥️  TFT Display initialized");
  
  // --- BLE init ---
  BLEDevice::init("TTGO_OCS3F");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );
  
  pCharacteristic->setCallbacks(new MyCallbacks());
  
  // เพิ่ม Descriptor สำหรับ Notification
  BLE2902* pBLE2902 = new BLE2902();
  pBLE2902->setNotifications(true);
  pCharacteristic->addDescriptor(pBLE2902);
  
  // ตั้งค่าเริ่มต้น
  pCharacteristic->setValue("Waiting for sensor data...");
  
  pService->start();
  
  // Setup advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
  
  Serial.println("📶 BLE Server started");
  Serial.println("🔍 Device name: TTGO_OCS3F");
  Serial.println("🆔 Service UUID: " + String(SERVICE_UUID));
  Serial.println("🆔 Characteristic UUID: " + String(CHARACTERISTIC_UUID));
  Serial.println("⏳ Waiting for BLE connection and sensor data...");
  Serial.println("===============================================");
}

void updateTFTDisplay(float concentration, float flow, float temperature) {
  // Update Oxygen
  tft.fillRect(100, 30, 120, 18, TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(String(concentration, 1) + " %", 100, 30, 2);
  
  // Update Flow
  tft.fillRect(100, 55, 120, 18, TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString(String(flow, 1) + " L/m", 100, 55, 2);
  
  // Update Temperature
  tft.fillRect(100, 80, 120, 18, TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString(String(temperature, 1) + " C", 100, 80, 2);
}

void sendBLEData(float concentration, float flow, float temperature) {
  if (deviceConnected) {
    // สร้าง JSON string
    String bleMsg = "{\"O2\":" + String(concentration, 1) + 
                    ",\"Flow\":" + String(flow, 1) + 
                    ",\"Temp\":" + String(temperature, 1) + 
                    ",\"timestamp\":" + String(millis()) + "}";
    
    // ส่งข้อมูล
    pCharacteristic->setValue(bleMsg.c_str());
    pCharacteristic->notify();
    
    Serial.println("📱 BLE Data sent: " + bleMsg);
    
    // แสดงสถานะการส่ง
    tft.fillRect(180, 105, 50, 18, TFT_BLACK);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString("SENT", 180, 105, 1);
  }
}

void loop() {
  unsigned long currentTime = millis();
  
  // จัดการการเชื่อมต่อ BLE
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // รอให้ BLE stack เสถียร
    pServer->startAdvertising();
    Serial.println("🔄 Start advertising again");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    Serial.println("✅ BLE Client connected successfully");
  }
  
  // อ่านข้อมูลจาก sensor
  if (currentTime - lastSensorRead >= SENSOR_READ_INTERVAL) {
    if (SensorSerial.available() >= 12) {
      if (SensorSerial.read() == 0x16) {
        byte data[12];
        data[0] = 0x16;
        SensorSerial.readBytes(&data[1], 11);
        
        // ตรวจสอบ checksum
        byte checksum = 0;
        for (int i = 0; i < 11; i++) checksum += data[i];
        
        if (((checksum + data[11]) & 0xFF) == 0) {
          // Parse sensor data
          float concentration = (data[3] * 256 + data[4]) / 10.0;
          float flow = (data[5] * 256 + data[6]) / 10.0;
          float temperature = (data[7] * 256 + data[8]) / 10.0;
          
          // ตรวจสอบว่าข้อมูลเปลี่ยนหรือไม่
          if (concentration != lastConcentration || 
              flow != lastFlow || 
              temperature != lastTemperature) {
            
            hasNewData = true;
            lastConcentration = concentration;
            lastFlow = flow;
            lastTemperature = temperature;
            
            // แสดงผลบน Serial
            Serial.printf("🔬 O2: %.1f%% | Flow: %.1f L/min | Temp: %.1f°C\n",
                          concentration, flow, temperature);
            
            // อัปเดต TFT Display
            updateTFTDisplay(concentration, flow, temperature);
            
            // ลบสถานะ error (ถ้ามี)
            tft.fillRect(0, 130, 240, 20, TFT_BLACK);
          }
        } else {
          Serial.println("❌ Checksum error!");
          tft.fillRect(0, 130, 240, 20, TFT_BLACK);
          tft.setTextColor(TFT_RED, TFT_BLACK);
          tft.drawString("Checksum Error!", 10, 130, 2);
        }
      }
    }
    lastSensorRead = currentTime;
  }
  
  // ส่งข้อมูลทาง BLE เมื่อมีข้อมูลใหม่หรือเวลาผ่านไป
  if (hasNewData && deviceConnected && 
      (currentTime - lastBLEUpdate >= BLE_UPDATE_INTERVAL)) {
    
    sendBLEData(lastConcentration, lastFlow, lastTemperature);
    hasNewData = false;
    lastBLEUpdate = currentTime;
  }
  
  // ส่งข้อมูลทุก ๆ 2 วินาที แม้ไม่มีการเปลี่ยนแปลง (heartbeat)
  if (deviceConnected && (currentTime - lastBLEUpdate >= 2000)) {
    if (lastConcentration >= 0) { // มีข้อมูล sensor
      sendBLEData(lastConcentration, lastFlow, lastTemperature);
      lastBLEUpdate = currentTime;
    }
  }
  
  // ลบสถานะ SENT หลัง 1 วินาที
  static unsigned long lastSentClear = 0;
  if (currentTime - lastSentClear >= 1000) {
    tft.fillRect(180, 105, 50, 18, TFT_BLACK);
    lastSentClear = currentTime;
  }
  
  delay(50); // ลด CPU usage
}
