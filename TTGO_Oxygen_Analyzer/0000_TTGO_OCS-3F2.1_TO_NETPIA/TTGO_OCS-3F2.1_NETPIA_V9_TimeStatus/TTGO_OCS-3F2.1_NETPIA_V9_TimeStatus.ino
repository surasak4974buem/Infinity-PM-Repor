#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// --- ส่วนตั้งค่า WiFi ---
const char* ssid = "someone2G";         // เปลี่ยนเป็น WiFi ของคุณ
const char* password = "ab123654";      // เปลี่ยนเป็นรหัสผ่าน WiFi

// --- ส่วนตั้งค่า NTP สำหรับเวลาไทย ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;    // GMT+7 สำหรับประเทศไทย
const int daylightOffset_sec = 0;

// --- ส่วนตั้งค่า NETPIE ---
const char* MQTT_SERVER = "broker.netpie.io";
const int MQTT_PORT = 1883;
const char* CLIENT_ID = "d8914184-d6e2-413d-ba70-9ff7b3d41dbd";
const char* TOKEN = "zJxHJBYF3c6iXmPFuffAdBXJY73mTuHJ";
const char* SECRET = "QWJyamMCyqgJ72NiEKNp7SFAkvCq3yvU";

// --- ส่วนตั้งค่าการชดเชยค่า (Offset Calibration) ---
const float CONCENTRATION_OFFSET = +2.0;
const float FLOW_OFFSET = +0.6;
const float TEMPERATURE_OFFSET = 0.0;

// --- ส่วนตั้งค่าจอ TFT ---
TFT_eSPI tft = TFT_eSPI(135, 240);

// --- ส่วนตั้งค่าเซ็นเซอร์ ---
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

// --- MQTT Client ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- ตัวแปรสำหรับจับเวลา ---
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 30000;
unsigned long lastStatusUpdateTime = 0;

// --- ตัวแปรสำหรับเก็บค่าเซ็นเซอร์และสถานะ ---
float lastConcentration = 0;
float lastFlow = 0;
float lastTemperature = 0;
bool dataReady = false;
String lastReceivedTimestamp = "N/A";
String lastSentTimestamp = "N/A";

// << เพิ่ม: ตัวแปรสำหรับเก็บสถานะต่างๆ >>
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
void updateDateTimeAndStatus(); // << แก้ไข: รวมฟังก์ชันอัปเดตเวลาและสถานะ

void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor with NETPIE v2");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  connectToWiFi();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Getting time from NTP server...");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    Serial.println("Time synchronized!");
  } else {
    Serial.println("Failed to obtain time");
  }
  
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  drawInterface();
}

void loop() {
  // --- ตรวจสอบการเชื่อมต่อ WiFi ---
  wifiStatus = (WiFi.status() == WL_CONNECTED);

  if (!wifiStatus) {
    // ถ้า WiFi หลุด ให้เปลี่ยนสถานะ NETPIE เป็นหลุดด้วย
    netpieStatus = false; 
    connectToWiFi(); // พยายามเชื่อมต่อ WiFi ใหม่
  } else {
    if (!client.connected()) {
      reconnectNetpie();
    }
    client.loop();
  }

  readSensorData();

  // --- อัปเดตเวลาและสถานะบนหน้าจอทุก 1 วินาที ---
  if (millis() - lastStatusUpdateTime >= 1000) {
    updateDateTimeAndStatus();
    lastStatusUpdateTime = millis();
  }

  // --- ส่งข้อมูลทุก 30 วินาที ---
  if (millis() - lastSendTime >= SEND_INTERVAL && dataReady && netpieStatus) {
    sendDataToNetpie();
    lastSendTime = millis();
  }
}


// << แก้ไข: ฟังก์ชันวาดหน้าจอเริ่มต้นใหม่ทั้งหมด >>
void drawInterface() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM); // ตั้งจุดอ้างอิงข้อความเป็นมุมบนซ้าย (Top-Left)

  // --- แถวแสดงค่าเซ็นเซอร์ ---
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("O2:", 10, 30, 2);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Flow:", 10, 55, 2);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp:", 10, 80, 2);
  
  // --- แถวแสดงสถานะการทำงาน ---
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Rx:", 10, 105, 2);
  tft.drawString("Tx:", 125, 105, 2);

  // --- แถวสถานะการเชื่อมต่อ ---
  tft.drawString("WF:", 10, 120, 2);
  tft.drawString("NP:", 85, 120, 2);
  tft.drawString("TX:", 160, 120, 2);
}

// << แก้ไข: ฟังก์ชันอัปเดตเวลา, วันที่ และสถานะทั้งหมด >>
void updateDateTimeAndStatus() {
  // --- 1. อัปเดตวันที่และเวลา ---
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.fillRect(5, 5, 230, 20, TFT_BLACK); // เคลียร์พื้นที่เก่า
  tft.drawString(getCurrentDateTimeString(), 10, 5, 2);

  // --- 2. อัปเดตสถานะ WiFi ---
  tft.fillRect(45, 120, 35, 16, TFT_BLACK);
  if (wifiStatus) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("OK", 45, 120, 2);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("...", 45, 120, 2);
  }

  // --- 3. อัปเดตสถานะ NETPIE ---
  tft.fillRect(120, 120, 35, 16, TFT_BLACK);
  if (netpieStatus) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("OK", 120, 120, 2);
  } else {
    // ถ้า WiFi ต่ออยู่ แต่ NETPIE ยังไม่ต่อ ให้เป็นสีเหลือง
    tft.setTextColor(wifiStatus ? TFT_YELLOW : TFT_RED, TFT_BLACK);
    tft.drawString("...", 120, 120, 2);
  }
  
  // --- 4. อัปเดตสถานะการส่งข้อมูล ---
  tft.fillRect(195, 120, 45, 16, TFT_BLACK);
  if (lastSendStatus) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("OK", 195, 120, 2);
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("FAIL", 195, 120, 2);
  }
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

        // อัปเดตเวลาที่รับข้อมูล และแสดงบนจอ
        lastReceivedTimestamp = getCurrentTimeOnly();
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.fillRect(45, 105, 75, 16, TFT_BLACK); // เคลียร์พื้นที่
        tft.drawString(lastReceivedTimestamp, 45, 105, 2);
        
        updateSensorDisplay();
      }
    }
  }
}

// << แยกฟังก์ชันอัปเดตค่า Sensor ออกมา >>
void updateSensorDisplay() {
  // ใช้ setTextDatum(TR_DATUM) เพื่อจัดข้อความชิดขวา ทำให้ตัวเลขไม่กระโดด
  tft.setTextDatum(TR_DATUM);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillRect(80, 30, 150, 20, TFT_BLACK);
  tft.drawString(String(lastConcentration, 2) + " %", 230, 30, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.fillRect(80, 55, 150, 20, TFT_BLACK);
  tft.drawString(String(lastFlow, 2) + " L/m", 230, 55, 2);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.fillRect(80, 80, 150, 20, TFT_BLACK);
  tft.drawString(String(lastTemperature, 2) + " C", 230, 80, 2);
  
  tft.setTextDatum(TL_DATUM); // ตั้งค่ากลับเป็นปกติ
}

void sendDataToNetpie() {
  if (!client.connected()) {
    lastSendStatus = false;
    return;
  }
  // (ส่วนของการสร้าง JSON payload เหมือนเดิม)
  String sendTime = getCurrentDateTimeString();
  char concStr[10], flowStr[10], tempStr[10];
  dtostrf(lastConcentration, 4, 2, concStr);
  dtostrf(lastFlow, 4, 2, flowStr);
  dtostrf(lastTemperature, 4, 2, tempStr);
  StaticJsonDocument<500> shadowDoc;
  shadowDoc["data"]["oxygen_concentration"] = concStr;
  shadowDoc["data"]["flow_rate"] = flowStr;
  shadowDoc["data"]["temperature"] = tempStr;
  shadowDoc["data"]["send_time"] = sendTime;
  String shadowPayload;
  serializeJson(shadowDoc, shadowPayload);
  String shadowTopic = "@shadow/data/update";
  
  // << แก้ไข: ตรวจสอบผลการส่งแล้วอัปเดตสถานะ >>
  if (client.publish(shadowTopic.c_str(), shadowPayload.c_str())) {
    Serial.println("✅ Shadow sent successfully");
    lastSendStatus = true;
    lastSentTimestamp = getCurrentTimeOnly();
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillRect(160, 105, 75, 16, TFT_BLACK);
    tft.drawString(lastSentTimestamp, 160, 105, 2);
  } else {
    Serial.println("❌ Shadow send failed!");
    lastSendStatus = false;
  }
  updateDateTimeAndStatus(); // อัปเดตสถานะ TX บนจอทันที
}


// --- ฟังก์ชัน Helper และฟังก์ชันที่ไม่ต้องแก้ไข ---

void connectToWiFi() {
  wifiStatus = false;
  updateDateTimeAndStatus(); // อัปเดตจอให้แสดงสถานะ ...
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting WiFi...", 10, 50, 2);

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiStatus = true;
    Serial.println("\nWiFi connected!");
  } else {
    Serial.println("\nWiFi connection failed!");
    wifiStatus = false;
  }
  drawInterface(); // วาดหน้าจอใหม่หลังเชื่อมต่อเสร็จ
}

void reconnectNetpie() {
  netpieStatus = false;
  updateDateTimeAndStatus(); // อัปเดตจอให้แสดงสถานะ ...
  
  if (client.connect(CLIENT_ID, TOKEN, SECRET)) {
    Serial.println("NETPIE connected");
    netpieStatus = true;
    client.subscribe("@shadow/data/update");
  } else {
    Serial.print("NETPIE failed, rc=");
    Serial.println(client.state());
    netpieStatus = false;
    delay(2000);
  }
  updateDateTimeAndStatus(); // อัปเดตสถานะ NP บนจอทันที
}

// << เพิ่ม: ฟังก์ชันดึง วันที่/เดือน/ปี และ เวลา >>
String getCurrentDateTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "No Time Sync";
  }
  // รูปแบบ DD/MM/YY HH:MM:SS
  char timeString[20];
  strftime(timeString, sizeof(timeString), "%d/%m/%y %H:%M:%S", &timeinfo);
  return String(timeString);
}

String getCurrentTimeOnly() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { return "N/A"; }
  char timeString[10];
  strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
  return String(timeString);
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Callback function
}
