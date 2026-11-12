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

// --- Sensor Timing Variables ---
unsigned long lastSensorReceived = 0;
unsigned long sensorInterval = 0;
unsigned long expectedNextSensor = 0;
bool sensorActive = false;

// --- Data Variables ---
float currentConcentration = -1;
float currentFlow = -1;
float currentTemperature = -1;
int sensorDataCount = 0;

// --- Status Tracking ---
String lastBLEMessage = "";
unsigned long lastBLESent = 0;

// Function declarations
void sendCurrentSensorData(bool forceUpdate = false);
void updateTFTDisplay(float concentration, float flow, float temperature);
void showNextSensorCountdown();

// ฟังก์ชันส่งข้อมูล BLE
void sendCurrentSensorData(bool forceUpdate) {
  if (!deviceConnected || currentConcentration < 0) return;
  
  // สร้าง JSON พร้อมข้อมูลเพิ่มเติม
  String bleMsg = "{\"O2\":" + String(currentConcentration, 1) + 
                  ",\"Flow\":" + String(currentFlow, 1) + 
                  ",\"Temp\":" + String(currentTemperature, 1) + 
                  ",\"count\":" + String(sensorDataCount) + 
                  ",\"interval\":" + String(sensorInterval) + 
                  ",\"timestamp\":" + String(millis()) + "}";
  
  // ส่งเฉพาะเมื่อข้อมูลเปลี่ยนหรือ force
  if (forceUpdate || bleMsg != lastBLEMessage) {
    pCharacteristic->setValue(bleMsg.c_str());
    pCharacteristic->notify();
    
    lastBLEMessage = bleMsg;
    lastBLESent = millis();
    
    Serial.println("📱 BLE Data sent: " + bleMsg);
    
    // แสดง indicator บน TFT
    tft.fillRect(200, 105, 30, 18, TFT_BLACK);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString("TX", 200, 105, 1);
  }
}

// Callback สำหรับเชื่อมต่อ/ตัดการเชื่อมต่อ
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("🔗 BLE Client Connected");
      
      // แสดงสถานะบน TFT
      tft.fillRect(50, 105, 180, 18, TFT_BLACK);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("BLE: Connected", 50, 105, 2);
      
      // ส่งข้อมูลล่าสุดทันที (ถ้ามี)
      if (currentConcentration >= 0) {
        sendCurrentSensorData(true); // force send
      }
    }
    
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("❌ BLE Client Disconnected");
      
      // แสดงสถานะบน TFT
      tft.fillRect(50, 105, 180, 18, TFT_BLACK);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("BLE: Waiting...", 50, 105, 2);
    }
};

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
  
  // แสดงจำนวนข้อมูลที่ได้รับ
  tft.fillRect(10, 130, 100, 15, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Count: " + String(sensorDataCount), 10, 130, 1);
  
  // แสดง sensor interval
  if (sensorInterval > 0) {
    tft.fillRect(120, 130, 100, 15, TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Int: " + String(sensorInterval/1000) + "s", 120, 130, 1);
  }
}

void showNextSensorCountdown() {
  if (!sensorActive || expectedNextSensor == 0) return;
  
  unsigned long currentTime = millis();
  if (currentTime < expectedNextSensor) {
    long remainingTime = (expectedNextSensor - currentTime) / 1000;
    if (remainingTime > 0 && remainingTime <= 60) {
      tft.fillRect(10, 145, 220, 15, TFT_BLACK);
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("Next sensor data in: " + String(remainingTime) + "s", 10, 145, 1);
    }
  } else {
    // เกินเวลาที่คาดหวัง
    tft.fillRect(10, 145, 220, 15, TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Waiting for sensor...", 10, 145, 1);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("🚀 TTGO ESP32 - OCS-3F Real-time Sync Mode");
  Serial.println("🔬 Optimized for ~46s sensor intervals");
  Serial.println("⚡ Power-efficient BLE transmission");
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
  tft.drawString("TTGO OCS-3F Sync Mode", 5, 5, 2);
  
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
  BLEDevice::init("TTGO_OCS3F_Sync");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  
  // เพิ่ม Descriptor สำหรับ Notification
  BLE2902* pBLE2902 = new BLE2902();
  pBLE2902->setNotifications(true);
  pCharacteristic->addDescriptor(pBLE2902);
  
  pCharacteristic->setValue("Waiting for sensor data...");
  pService->start();
  
  // Setup advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  
  Serial.println("📶 BLE Server started - TTGO_OCS3F_Sync");
  Serial.println("🔍 Waiting for sensor pattern detection...");
  Serial.println("===============================================");
}

void loop() {
  unsigned long currentTime = millis();
  
  // จัดการการเชื่อมต่อ BLE
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("🔄 BLE Advertising restarted");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    Serial.println("✅ BLE Connection established");
  }
  
  // อ่านข้อมูลจาก sensor - เฉพาะเมื่อมีข้อมูลใหม่
  if (SensorSerial.available() >= 12) {
    if (SensorSerial.read() == 0x16) {
      byte data[12];
      data[0] = 0x16;
      SensorSerial.readBytes(&data[1], 11);
      
      // ตรวจสอบ checksum
      byte checksum = 0;
      for (int i = 0; i < 11; i++) checksum += data[i];
      
      if (((checksum + data[11]) & 0xFF) == 0) {
        // คำนวณ sensor interval
        if (lastSensorReceived > 0) {
          sensorInterval = currentTime - lastSensorReceived;
          expectedNextSensor = currentTime + sensorInterval;
          sensorActive = true;
          
          Serial.printf("⏱️  Sensor interval: %.1fs\n", sensorInterval/1000.0);
        }
        
        lastSensorReceived = currentTime;
        sensorDataCount++;
        
        // Parse sensor data
        float concentration = (data[3] * 256 + data[4]) / 10.0;
        float flow = (data[5] * 256 + data[6]) / 10.0;
        float temperature = (data[7] * 256 + data[8]) / 10.0;
        
        currentConcentration = concentration;
        currentFlow = flow;
        currentTemperature = temperature;
        
        // แสดงผลบน Serial
        Serial.printf("🔬 [%d] O2: %.1f%% | Flow: %.1f L/min | Temp: %.1f°C\n",
                      sensorDataCount, concentration, flow, temperature);
        
        // อัปเดต TFT Display
        updateTFTDisplay(concentration, flow, temperature);
        
        // ส่งข้อมูล BLE ทันทีที่ได้รับข้อมูลใหม่
        sendCurrentSensorData();
        
        // ลบข้อความ error
        tft.fillRect(0, 160, 240, 20, TFT_BLACK);
        
      } else {
        Serial.println("❌ Sensor checksum error!");
        tft.fillRect(0, 160, 240, 15, TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Sensor Error!", 10, 160, 1);
      }
    }
  }
  
  // แสดง countdown สำหรับข้อมูลถัดไป
  static unsigned long lastCountdownUpdate = 0;
  if (currentTime - lastCountdownUpdate >= 1000) {
    showNextSensorCountdown();
    lastCountdownUpdate = currentTime;
  }
  
  // ลบ TX indicator หลัง 2 วินาที
  if (lastBLESent > 0 && currentTime - lastBLESent >= 2000) {
    tft.fillRect(200, 105, 30, 18, TFT_BLACK);
    lastBLESent = 0;
  }
  
  // ประหยัดพลังงาน - ใช้ delay ยาวขึ้นเมื่อไม่มี sensor data คาดหวัง
  if (sensorActive && expectedNextSensor > currentTime) {
    long timeToNext = expectedNextSensor - currentTime;
    if (timeToNext > 10000) { // ถ้าอีกนานกว่า 10 วินาที
      delay(1000); // delay ยาว
    } else if (timeToNext > 5000) { // ถ้าอีกนานกว่า 5 วินาที  
      delay(500);
    } else {
      delay(100); // ใกล้เวลาแล้ว ใช้ delay สั้น
    }
  } else {
    delay(100); // ยังไม่รู้รูปแบบ sensor
  }
}
