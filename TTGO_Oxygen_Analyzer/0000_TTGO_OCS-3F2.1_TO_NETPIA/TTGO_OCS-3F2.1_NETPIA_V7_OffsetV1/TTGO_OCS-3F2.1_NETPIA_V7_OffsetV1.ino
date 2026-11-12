#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// --- ส่วนตั้งค่า WiFi ---
const char* ssid = "someone2G";      // เปลี่ยนเป็น WiFi ของคุณ
const char* password = "ab123654";      // เปลี่ยนเป็นรหัสผ่าน WiFi

// --- ส่วนตั้งค่า NTP สำหรับเวลาไทย ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;  // GMT+7 สำหรับประเทศไทย
const int daylightOffset_sec = 0;      // ไทยไม่ใช้ daylight saving time

// --- ส่วนตั้งค่า NETPIE ---
const char* MQTT_SERVER = "broker.netpie.io";
const int MQTT_PORT = 1883;
const char* CLIENT_ID = "d8914184-d6e2-413d-ba70-9ff7b3d41dbd";
const char* TOKEN = "zJxHJBYF3c6iXmPFuffAdBXJY73mTuHJ";
const char* SECRET = "QWJyamMCyqgJ72NiEKNp7SFAkvCq3yvU";

// --- ส่วนตั้งค่าการชดเชยค่า (Offset Calibration) --- // << เพิ่มส่วนนี้
// หลักการ: Offset = (ค่าจากเครื่องมือมาตรฐาน) - (ค่าที่เซ็นเซอร์อ่านได้)
const float CONCENTRATION_OFFSET = +2.0; // ตัวอย่าง: 1.5, -0.8
const float FLOW_OFFSET = +0.6;          // ตัวอย่าง: 0.2, -0.1
const float TEMPERATURE_OFFSET = 0.0;   // ตัวอย่าง: -2.1, 0.5

// --- ส่วนตั้งค่าจอ TFT ---
TFT_eSPI tft = TFT_eSPI(135, 240);

// --- ส่วนตั้งค่าเซ็นเซอร์ ---
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

// --- MQTT Client ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- ตัวแปรสำหรับจับเวลาการส่งข้อมูล ---
unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 30000; // ส่งข้อมูลทุก 30 วินาที

// --- ตัวแปรสำหรับเก็บค่าเซ็นเซอร์ล่าสุด ---
float lastConcentration = 0;
float lastFlow = 0;
float lastTemperature = 0;
bool dataReady = false;
bool netpieConnected = false;

// --- Function Prototypes ---
void connectToWiFi();
void reconnectWiFi();
void reconnectNetpie();
void callback(char* topic, byte* payload, unsigned int length);
void drawInterface();
void updateConnectionStatus();
void readSensorData();
void updateDisplay();
String getCurrentTimestamp();
String getCurrentDateTimeISO();
String getServerTimestamp();
void sendDataToNetpie();


void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor Reader with NETPIE");

  // --- เริ่มต้นการทำงานของจอ TFT ---
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // --- เชื่อมต่อ WiFi ---
  connectToWiFi();

  // --- ตั้งค่า NTP สำหรับเวลาไทย ---
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Getting time from NTP server...");

  // รอให้ได้เวลาจาก NTP server
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }

  if (getLocalTime(&timeinfo)) {
    Serial.println("\nTime synchronized!");
    Serial.println(&timeinfo, "Current time: %Y-%m-%d %H:%M:%S");
  } else {
    Serial.println("\nFailed to obtain time");
  }
  
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);

  // --- เริ่มการทำงานของ Serial Port 2 เพื่อสื่อสารกับเซ็นเซอร์ ---
  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // --- วาดหัวข้อต่างๆ ลงบนจอ ---
  drawInterface();
}

void loop() {
  // --- รักษาการเชื่อมต่อ MQTT ---
  if (!client.connected()) {
    reconnectNetpie();
  }
  client.loop();

  // --- อ่านข้อมูลจากเซ็นเซอร์ ---
  readSensorData();

  // --- ส่งข้อมูลผ่าน NETPIE ทุก 30 วินาที ---
  if (millis() - lastSendTime >= SEND_INTERVAL && dataReady && netpieConnected) {
    sendDataToNetpie();
    lastSendTime = millis();
  }

  // --- ตรวจสอบการเชื่อมต่อ WiFi ---
  if (WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
  }
}

void connectToWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Connecting WiFi...", 10, 60, 2);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    tft.drawString("." + String(attempts + 1), 10 + (attempts * 10), 90, 2);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.println("IP address: " + WiFi.localIP().toString());

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("WiFi Connected!", 10, 60, 2);
    tft.drawString("IP: " + WiFi.localIP().toString(), 10, 90, 1);
    delay(2000);

    drawInterface();
  } else {
    Serial.println("WiFi connection failed!");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("WiFi Failed!", 10, 60, 2);
    delay(5000);
    ESP.restart();
  }
}

void reconnectWiFi() {
  static unsigned long lastReconnectAttempt = 0;
  if (millis() - lastReconnectAttempt >= 30000) {
    Serial.println("Reconnecting to WiFi...");
    WiFi.reconnect();
    lastReconnectAttempt = millis();

    // แสดงสถานะบนจอ
    tft.fillRect(0, 125, 240, 15, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Reconnecting WiFi...", 10, 125, 1);
  }
}

void reconnectNetpie() {
  if (WiFi.status() != WL_CONNECTED) return;

  while (!client.connected()) {
    Serial.print("Attempting NETPIE connection...");

    // แสดงสถานะการเชื่อมต่อบนจอ
    tft.fillRect(60, 125, 150, 15, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Connecting NETPIE...", 60, 125, 1);

    if (client.connect(CLIENT_ID, TOKEN, SECRET)) {
      Serial.println(" connected");
      netpieConnected = true;

      // Subscribe to topics (ถ้าต้องการ)
      String shadowTopic = "@shadow/data/update";
      client.subscribe(shadowTopic.c_str());

      // แสดงสถานะเชื่อมต่อสำเร็จ
      tft.fillRect(60, 125, 150, 15, TFT_BLACK);
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("NETPIE Connected", 60, 125, 1);

    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");

      netpieConnected = false;

      // แสดงสถานะเชื่อมต่อล้มเหลว
      tft.fillRect(60, 125, 150, 15, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("NETPIE Failed", 60, 125, 1);

      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
}

void drawInterface() {
  tft.fillScreen(TFT_BLACK);

  // วาดหัวข้อต่างๆ
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Oxygen:", 10, 20, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Flow:", 10, 55, 2);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp:", 10, 90, 2);

  // แสดงสถานะการเชื่อมต่อ
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Status:", 10, 125, 1);

  updateConnectionStatus();
  
  // << เพิ่มการแสดงผลสถานะ Offset (ทางเลือก) >>
  if (CONCENTRATION_OFFSET != 0.0 || FLOW_OFFSET != 0.0 || TEMPERATURE_OFFSET != 0.0) {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Offset Active", 150, 125, 1);
  }
}

void updateConnectionStatus() {
  tft.fillRect(60, 125, 90, 15, TFT_BLACK); // เคลียร์พื้นที่แสดงสถานะ WiFi/NETPIE

  if (WiFi.status() == WL_CONNECTED) {
    if (netpieConnected && client.connected()) {
      tft.setTextColor(TFT_GREEN, TFT_BLACK);
      tft.drawString("WiFi+NETPIE OK", 60, 125, 1);
    } else {
      tft.setTextColor(TFT_YELLOW, TFT_BLACK);
      tft.drawString("WiFi OK, NETPIE ?", 60, 125, 1);
    }
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("WiFi Disconnected", 60, 125, 1);
  }
}

void readSensorData() { // << แก้ไขส่วนนี้
  if (SensorSerial.available() >= 12) {
    if (SensorSerial.read() == 0x16) {
      byte data[12];
      data[0] = 0x16;
      SensorSerial.readBytes(&data[1], 11);

      byte checksum = 0;
      for (int i = 0; i < 11; i++) {
        checksum += data[i];
      }

      if (((checksum + data[11]) & 0xFF) == 0) {
        // Checksum ถูกต้อง
        // --- 1. คำนวณค่าดิบจากเซ็นเซอร์ ---
        float rawConcentration = (data[3] * 256 + data[4]) / 10.0;
        float rawFlow = (data[5] * 256 + data[6]) / 10.0;
        float rawTemperature = (data[7] * 256 + data[8]) / 10.0;

        // --- 2. นำค่าชดเชย (Offset) มาใช้ ---
        lastConcentration = rawConcentration + CONCENTRATION_OFFSET;
        lastFlow = rawFlow + FLOW_OFFSET;
        lastTemperature = rawTemperature + TEMPERATURE_OFFSET;
        
        dataReady = true;

        // แสดงผลใน Serial Monitor เป็นทศนิยม 2 ตำแหน่ง
        Serial.printf("O2: %.2f %% | Flow: %.2f L/min | Temp: %.2f C | Time: %s\n",
                      lastConcentration, lastFlow, lastTemperature, getCurrentTimestamp().c_str());

        // อัปเดตค่าบนหน้าจอ TFT
        updateDisplay();

      } else {
        Serial.println("Checksum error!");
        tft.fillRect(150, 125, 90, 15, TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("Chk Error!", 150, 125, 1);
      }
    }
  }
}

void updateDisplay() {
  // แสดงค่า Oxygen ทศนิยม 2 ตำแหน่ง
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillRect(95, 20, 120, 18, TFT_BLACK);
  tft.drawString(String(lastConcentration, 2) + " %", 100, 20, 2);

  // แสดงค่า Flow ทศนิยม 2 ตำแหน่ง
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.fillRect(95, 55, 120, 18, TFT_BLACK);
  tft.drawString(String(lastFlow, 2) + " L/m", 100, 55, 2);

  // แสดงค่า Temperature ทศนิยม 2 ตำแหน่ง
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.fillRect(95, 90, 120, 18, TFT_BLACK);
  tft.drawString(String(lastTemperature, 2) + " C", 100, 90, 2);

  // อัปเดตสถานะการเชื่อมต่อ
  updateConnectionStatus();
}

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Error";
  }

  char timeString[64];
  // เปลี่ยนรูปแบบเป็น DD:MM:YYYY HH:MM:SS
  strftime(timeString, sizeof(timeString), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(timeString);
}

String getCurrentDateTimeISO() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "";
  }

  char isoString[32];
  strftime(isoString, sizeof(isoString), "%Y-%m-%dT%H:%M:%S+07:00", &timeinfo);
  return String(isoString);
}

String getServerTimestamp() {
  // สร้าง timestamp สำหรับ server ที่จะได้รับข้อมูล
  // ใช้เวลาปัจจุบัน + ประมาณ 1-2 วินาที (เวลาในการส่ง)
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Error";
  }

  // เพิ่มเวลา 2 วินาทีสำหรับ network delay
  time_t rawtime = mktime(&timeinfo);
  rawtime += 2; // เพิ่ม 2 วินาที
  timeinfo = *localtime(&rawtime);

  char timeString[64];
  strftime(timeString, sizeof(timeString), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(timeString);
}

void sendDataToNetpie() {
  if (!client.connected()) {
    Serial.println("NETPIE not connected, cannot send data");
    return;
  }

  // เวลาที่ส่งและเวลาที่ server จะได้รับ
  String sendTime = getCurrentTimestamp();
  String serverReceiveTime = getServerTimestamp();

  // ฟอร์แมตทศนิยม 2 ตำแหน่ง
  char concStr[10], flowStr[10], tempStr[10];
  dtostrf(lastConcentration, 4, 2, concStr);
  dtostrf(lastFlow, 4, 2, flowStr);
  dtostrf(lastTemperature, 4, 2, tempStr);

  // ---------- Shadow Payload ----------
  StaticJsonDocument<500> shadowDoc;
  shadowDoc["data"]["oxygen_concentration"] = concStr;
  shadowDoc["data"]["flow_rate"] = flowStr;
  shadowDoc["data"]["temperature"] = tempStr;
  shadowDoc["data"]["esp32_send_time"] = sendTime;
  shadowDoc["data"]["server_receive_time"] = serverReceiveTime;

  String shadowPayload;
  serializeJson(shadowDoc, shadowPayload);

  String shadowTopic = "@shadow/data/update";
  bool shadowSent = client.publish(shadowTopic.c_str(), shadowPayload.c_str());

  // ---------- Feed Payload ----------
  StaticJsonDocument<300> feedDoc;
  feedDoc["oxygen_concentration"] = String(concStr);
  feedDoc["flow_rate"] = String(flowStr);
  feedDoc["temperature"] = String(tempStr);
  feedDoc["timestamp"] = sendTime;
  feedDoc["data_type"] = "sensor_reading";
  feedDoc["device_id"] = "esp32_sensor_01";

  String feedPayload;
  serializeJson(feedDoc, feedPayload);

  String feedTopic1 = "data/sensor";
  String feedTopic2 = "@feed/sensor_data";

  bool feedSent1 = client.publish(feedTopic1.c_str(), feedPayload.c_str());
  bool feedSent2 = client.publish(feedTopic2.c_str(), feedPayload.c_str());

  // ---------- ส่งค่าแยกแต่ละตัว ----------
  client.publish("data/oxygen", concStr);
  client.publish("data/flow", flowStr);  
  client.publish("data/temperature", tempStr);
  client.publish("data/timestamp", sendTime.c_str());

  // ---------- Debug ----------
  Serial.println("=== Data Sent ===");
  Serial.println("Shadow: " + shadowPayload);
  Serial.println("Feed: " + feedPayload);
  Serial.printf("Individual values sent to: data/oxygen, data/flow, data/temperature\n");

  if (shadowSent) Serial.println("✅ Shadow sent successfully");
  if (feedSent1 || feedSent2) Serial.println("✅ Feed sent successfully");
}
