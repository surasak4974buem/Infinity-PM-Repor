#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// --- (ส่วนตั้งค่าต่างๆ เหมือนเดิม) ---
const char* ssid = "someone2G";
const char* password = "ab123654";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;
const char* MQTT_SERVER = "broker.netpie.io";
const int MQTT_PORT = 1883;
const char* CLIENT_ID = "d8914184-d6e2-413d-ba70-9ff7b3d41dbd";
const char* TOKEN = "zJxHJBYF3c6iXmPFuffAdBXJY73mTuHJ";
const char* SECRET = "QWJyamMCyqgJ72NiEKNp7SFAkvCq3yvU";
const float CONCENTRATION_OFFSET = 1.00;
const float FLOW_OFFSET = -0.90;
const float TEMPERATURE_OFFSET = 0.00;
// --- (จบส่วนตั้งค่า) ---

TFT_eSPI tft = TFT_eSPI(135, 240);
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 30000;
unsigned long lastStatusUpdateTime = 0;

float lastConcentration = 0;
float lastFlow = 0;
float lastTemperature = 0;
bool dataReady = false;
String lastReceivedTimestamp = "--:--:--";
String lastSentTimestamp = "--:--:--";

bool wifiStatus = false;
bool netpieStatus = false;
bool lastSendStatus = false;

// --- Function Prototypes ---
void connectToWiFi();
void reconnectNetpie();
void callback(char* topic, byte* payload, unsigned int length);
void drawInterface();
void readSensorData();
void updateSensorDisplay();
void sendDataToNetpie();
String getCurrentDateTimeString();
String getCurrentTimeOnly();
void updateDateTimeAndStatus();

void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor with NETPIE v3 (Layout Fixed)");
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  connectToWiFi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  drawInterface();
}

void loop() {
  wifiStatus = (WiFi.status() == WL_CONNECTED);
  if (!wifiStatus) {
    netpieStatus = false; 
    connectToWiFi();
  } else {
    if (!client.connected()) {
      reconnectNetpie();
    }
    client.loop();
  }
  readSensorData();
  if (millis() - lastStatusUpdateTime >= 1000) {
    updateDateTimeAndStatus();
    lastStatusUpdateTime = millis();
  }
  if (millis() - lastSendTime >= SEND_INTERVAL && dataReady && netpieStatus) {
    sendDataToNetpie();
    lastSendTime = millis();
  }
}

// << แก้ไข: จัด Layout การวาดหน้าจอเริ่มต้นใหม่ทั้งหมด >>
void drawInterface() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM); // ตั้งจุดอ้างอิงข้อความเป็นมุมบนซ้าย (Top-Left)

  // --- แถวแสดงค่าเซ็นเซอร์ (วาดเฉพาะ Label) ---
  tft.setTextColor(TFT_GREEN);
  tft.drawString("O2:", 10, 30, 2);
  tft.setTextColor(TFT_CYAN);
  tft.drawString("Flow:", 10, 55, 2);
  tft.setTextColor(TFT_ORANGE);
  tft.drawString("Temp:", 10, 80, 2);
  
  // --- แถวแสดงสถานะการทำงาน (วาด Label และค่าเริ่มต้น) ---
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Rx:", 10, 105, 2);
  tft.drawString(lastReceivedTimestamp, 45, 105, 2);
  tft.drawString("Tx:", 130, 105, 2);
  tft.drawString(lastSentTimestamp, 165, 105, 2);

  // --- แถวสถานะการเชื่อมต่อ (วาดเฉพาะ Label) ---
  tft.drawString("WF:", 10, 120, 2);
  tft.drawString("NP:", 85, 120, 2);
  tft.drawString("TX:", 160, 120, 2);
}

// << แก้ไข: จัดตำแหน่งการแสดงผลเวลาและสถานะ >>
void updateDateTimeAndStatus() {
  // --- 1. อัปเดตวันที่และเวลา (จัดกึ่งกลาง) ---
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM); // ตั้งจุดอ้างอิงเป็นกลางบน (Top-Center)
  tft.drawString(getCurrentDateTimeString(), tft.width() / 2, 5, 2);
  tft.setTextDatum(TL_DATUM); // ตั้งกลับเป็นค่าปกติ (Top-Left)

  // --- 2. อัปเดตสถานะ WiFi ---
  tft.fillRect(45, 120, 38, 16, TFT_BLACK); // เคลียร์พื้นที่เก่า
  if (wifiStatus) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("OK", 45, 120, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("...", 45, 120, 2);
  }

  // --- 3. อัปเดตสถานะ NETPIE ---
  tft.fillRect(120, 120, 38, 16, TFT_BLACK); // เคลียร์พื้นที่เก่า
  if (netpieStatus) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("OK", 120, 120, 2);
  } else {
    tft.setTextColor(wifiStatus ? TFT_YELLOW : TFT_RED);
    tft.drawString("...", 120, 120, 2);
  }
  
  // --- 4. อัปเดตสถานะการส่งข้อมูล ---
  tft.fillRect(195, 120, 45, 16, TFT_BLACK); // เคลียร์พื้นที่เก่า
  if (lastSendStatus) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("OK", 195, 120, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("FAIL", 195, 120, 2);
  }
}

// << แก้ไข: จัดตำแหน่งการแสดงค่า Sensor ให้ชิดขวา >>
void updateSensorDisplay() {
  // ใช้ setTextDatum(TR_DATUM) เพื่อจัดข้อความชิดขวา
  // ทำให้ค่าที่แสดงผลสวยงาม ไม่ว่าจะมีกี่หลักก็ตาม
  tft.setTextDatum(TR_DATUM);

  String text;
  // --- แสดงค่า Oxygen ---
  text = String(lastConcentration, 2) + " %";
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillRect(80, 30, 155, 20, TFT_BLACK);
  tft.drawString(text, 235, 30, 2);

  // --- แสดงค่า Flow ---
  text = String(lastFlow, 2) + " L/m";
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.fillRect(80, 55, 155, 20, TFT_BLACK);
  tft.drawString(text, 235, 55, 2);

  // --- แสดงค่า Temperature ---
  text = String(lastTemperature, 2) + " C";
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.fillRect(80, 80, 155, 20, TFT_BLACK);
  tft.drawString(text, 235, 80, 2);
  
  // ตั้งค่ากลับเป็นปกติ (สำคัญมาก)
  tft.setTextDatum(TL_DATUM);
}

void readSensorData() { 
  if (SensorSerial.available() >= 12) {
    if (SensorSerial.read() == 0x16) {
      byte data[12];
      data[0] = 0x16;
      SensorSerial.readBytes(&data[1], 11);
      byte checksum = 0;
      for (int i = 0; i < 11; i++) { checksum += data[i]; }
      if (((checksum + data[11]) & 0xFF) == 0) {
        lastConcentration = ((data[3] * 256 + data[4]) / 10.0) + CONCENTRATION_OFFSET;
        lastFlow = ((data[5] * 256 + data[6]) / 10.0) + FLOW_OFFSET;
        lastTemperature = ((data[7] * 256 + data[8]) / 10.0) + TEMPERATURE_OFFSET;
        dataReady = true;

        // << แก้ไข: จัดตำแหน่งเวลา Rx >>
        lastReceivedTimestamp = getCurrentTimeOnly();
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.fillRect(45, 105, 80, 16, TFT_BLACK); // เคลียร์พื้นที่
        tft.drawString(lastReceivedTimestamp, 45, 105, 2);
        
        updateSensorDisplay();
      }
    }
  }
}

void sendDataToNetpie() {
  if (!client.connected()) {
    lastSendStatus = false;
    return;
  }
  // (ส่วนของการสร้าง JSON payload เหมือนเดิม)
  String sendTime = getCurrentDateTimeString();
  char concStr[10], flowStr[10], tempStr[10];
  dtostrf(lastConcentration, 4, 2, concStr); dtostrf(lastFlow, 4, 2, flowStr); dtostrf(lastTemperature, 4, 2, tempStr);
  StaticJsonDocument<500> shadowDoc;
  shadowDoc["data"]["oxygen_concentration"] = concStr; shadowDoc["data"]["flow_rate"] = flowStr; shadowDoc["data"]["temperature"] = tempStr; shadowDoc["data"]["send_time"] = sendTime;
  String shadowPayload;
  serializeJson(shadowDoc, shadowPayload);
  String shadowTopic = "@shadow/data/update";
  
  if (client.publish(shadowTopic.c_str(), shadowPayload.c_str())) {
    Serial.println("✅ Shadow sent successfully");
    lastSendStatus = true;

    // << แก้ไข: จัดตำแหน่งเวลา Tx >>
    lastSentTimestamp = getCurrentTimeOnly();
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillRect(165, 105, 80, 16, TFT_BLACK); // เคลียร์พื้นที่
    tft.drawString(lastSentTimestamp, 165, 105, 2);

  } else {
    Serial.println("❌ Shadow send failed!");
    lastSendStatus = false;
  }
  updateDateTimeAndStatus();
}


// --- (ฟังก์ชันที่เหลือเหมือนเดิม) ---
void connectToWiFi() {
  wifiStatus = false;
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Connecting WiFi...", tft.width() / 2, 60, 2);
  tft.setTextDatum(TL_DATUM);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); Serial.print("."); attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus = true; Serial.println("\nWiFi connected!");
  } else {
    wifiStatus = false; Serial.println("\nWiFi connection failed!");
  }
  drawInterface();
}
void reconnectNetpie() {
  netpieStatus = false;
  updateDateTimeAndStatus();
  if (client.connect(CLIENT_ID, TOKEN, SECRET)) {
    Serial.println("NETPIE connected");
    netpieStatus = true;
    client.subscribe("@shadow/data/update");
  } else {
    Serial.print("NETPIE failed, rc="); Serial.println(client.state());
    netpieStatus = false;
    delay(2000);
  }
  updateDateTimeAndStatus();
}
String getCurrentDateTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { return "No Time Sync"; }
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%d/%m/%y %H:%M:%S", &timeinfo);
  return String(timeString);
}
String getCurrentTimeOnly() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { return "--:--:--"; }
  char timeString[10];
  strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
  return String(timeString);
}
void callback(char* topic, byte* payload, unsigned int length) { /* ... */ }
