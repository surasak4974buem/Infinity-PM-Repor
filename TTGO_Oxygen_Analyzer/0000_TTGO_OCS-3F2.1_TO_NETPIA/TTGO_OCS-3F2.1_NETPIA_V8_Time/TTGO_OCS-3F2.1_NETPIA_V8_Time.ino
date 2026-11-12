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
const int daylightOffset_sec = 0;       // ไทยไม่ใช้ daylight saving time

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
unsigned long lastTimeUpdate = 0; // << เพิ่ม: สำหรับหน่วงเวลาอัปเดตหน้าจอ

// --- ตัวแปรสำหรับเก็บค่าเซ็นเซอร์และสถานะ ---
float lastConcentration = 0;
float lastFlow = 0;
float lastTemperature = 0;
bool dataReady = false;
bool netpieConnected = false;
String lastReceivedTimestamp = "N/A"; // << เพิ่ม: เก็บเวลาที่รับข้อมูลล่าสุด
String lastSentTimestamp = "N/A";     // << เพิ่ม: เก็บเวลาที่ส่งข้อมูลล่าสุด

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
String getCurrentTimeOnly();      // << เพิ่ม: ฟังก์ชันดึงเวลา
void updateTimeDisplay();         // << เพิ่ม: ฟังก์ชันอัปเดตเวลาบนจอ

void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor Reader with NETPIE");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  connectToWiFi();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Getting time from NTP server...");
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

  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  drawInterface();
}

void loop() {
  if (!client.connected()) {
    reconnectNetpie();
  }
  client.loop();

  readSensorData();

  // << แก้ไข: อัปเดตเวลาปัจจุบันบนหน้าจอทุก 1 วินาที >>
  if (millis() - lastTimeUpdate >= 1000) {
    updateTimeDisplay();
    lastTimeUpdate = millis();
  }

  if (millis() - lastSendTime >= SEND_INTERVAL && dataReady && netpieConnected) {
    sendDataToNetpie();
    lastSendTime = millis();
  }

  if (WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
  }
}

// << เพิ่ม: ฟังก์ชันสำหรับดึงเวลาปัจจุบันในรูปแบบ HH:MM:SS >>
String getCurrentTimeOnly() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Error";
  }
  char timeString[10];
  strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
  return String(timeString);
}


// << เพิ่ม: ฟังก์ชันสำหรับอัปเดตเวลาปัจจุบันบนหน้าจอ >>
void updateTimeDisplay() {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.fillRect(90, 8, 140, 16, TFT_BLACK); // เคลียร์พื้นที่เก่า
  tft.drawString(getCurrentTimeOnly(), 95, 8, 2);
}

// << แก้ไข: ฟังก์ชันวาดหน้าจอใหม่ทั้งหมด >>
void drawInterface() {
  tft.fillScreen(TFT_BLACK);

  // --- แถวแสดงเวลาปัจจุบัน ---
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Time:", 10, 8, 2);

  // --- แถวแสดงค่าเซ็นเซอร์ ---
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("O2:", 10, 33, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Flow:", 10, 58, 2);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Temp:", 10, 83, 2);
  
  // --- แถวแสดงสถานะการทำงาน ---
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Last Rx:", 10, 115, 1); // รับค่าล่าสุด
  tft.drawString("Last Tx:", 125, 115, 1); // ส่งค่าล่าสุด
}

// << แก้ไข: ฟังก์ชันอ่านข้อมูลจาก Sensor >>
void readSensorData() { 
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
        float rawConcentration = (data[3] * 256 + data[4]) / 10.0;
        float rawFlow = (data[5] * 256 + data[6]) / 10.0;
        float rawTemperature = (data[7] * 256 + data[8]) / 10.0;

        lastConcentration = rawConcentration + CONCENTRATION_OFFSET;
        lastFlow = rawFlow + FLOW_OFFSET;
        lastTemperature = rawTemperature + TEMPERATURE_OFFSET;
        
        dataReady = true;

        // << เพิ่ม: บันทึกเวลาที่รับข้อมูลและอัปเดตจอ >>
        lastReceivedTimestamp = getCurrentTimeOnly();
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.fillRect(55, 115, 60, 10, TFT_BLACK); // เคลียร์พื้นที่เก่า
        tft.drawString(lastReceivedTimestamp, 55, 115, 1);
        
        Serial.printf("O2: %.2f %% | Flow: %.2f L/min | Temp: %.2f C | Rx Time: %s\n",
                      lastConcentration, lastFlow, lastTemperature, lastReceivedTimestamp.c_str());

        updateDisplay();

      } else {
        Serial.println("Checksum error!");
      }
    }
  }
}

// << แก้ไข: ฟังก์ชันอัปเดตค่า Sensor บนจอ >>
void updateDisplay() {
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillRect(90, 33, 140, 18, TFT_BLACK);
  tft.drawString(String(lastConcentration, 2) + " %", 95, 33, 2);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.fillRect(90, 58, 140, 18, TFT_BLACK);
  tft.drawString(String(lastFlow, 2) + " L/m", 95, 58, 2);

  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.fillRect(90, 83, 140, 18, TFT_BLACK);
  tft.drawString(String(lastTemperature, 2) + " C", 95, 83, 2);
}

// << แก้ไข: ฟังก์ชันส่งข้อมูลไป NETPIE >>
void sendDataToNetpie() {
  if (!client.connected()) {
    Serial.println("NETPIE not connected, cannot send data");
    return;
  }

  // ... (ส่วนของการสร้าง JSON payload เหมือนเดิม) ...
  String sendTime = getCurrentTimestamp();
  String serverReceiveTime = getServerTimestamp();
  char concStr[10], flowStr[10], tempStr[10];
  dtostrf(lastConcentration, 4, 2, concStr);
  dtostrf(lastFlow, 4, 2, flowStr);
  dtostrf(lastTemperature, 4, 2, tempStr);
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

  if (shadowSent) {
      Serial.println("✅ Shadow sent successfully");
      
      // << เพิ่ม: บันทึกเวลาที่ส่งข้อมูลและอัปเดตจอ >>
      lastSentTimestamp = getCurrentTimeOnly();
      tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
      tft.fillRect(170, 115, 60, 10, TFT_BLACK); // เคลียร์พื้นที่เก่า
      tft.drawString(lastSentTimestamp, 170, 115, 1);
  }
  
  // ... (ส่วนที่เหลือของฟังก์ชัน sendDataToNetpie เหมือนเดิม) ...
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
  client.publish("data/oxygen", concStr);
  client.publish("data/flow", flowStr);
  client.publish("data/temperature", tempStr);
  client.publish("data/timestamp", sendTime.c_str());
  Serial.println("=== Data Sent ===");
  Serial.println("Shadow: " + shadowPayload);
  Serial.println("Feed: " + feedPayload);
  Serial.printf("Individual values sent to: data/oxygen, data/flow, data/temperature\n");
  if (feedSent1 || feedSent2) Serial.println("✅ Feed sent successfully");
}


// --- ฟังก์ชันที่ไม่ต้องแก้ไข ---
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
  }
}

void reconnectNetpie() {
  if (WiFi.status() != WL_CONNECTED) return;
  while (!client.connected()) {
    Serial.print("Attempting NETPIE connection...");
    if (client.connect(CLIENT_ID, TOKEN, SECRET)) {
      Serial.println(" connected");
      netpieConnected = true;
      String shadowTopic = "@shadow/data/update";
      client.subscribe(shadowTopic.c_str());
    } else {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      netpieConnected = false;
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

void updateConnectionStatus() { } // ไม่ได้ใช้แล้ว

String getCurrentTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Error";
  }
  char timeString[64];
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
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Error";
  }
  time_t rawtime = mktime(&timeinfo);
  rawtime += 2;
  timeinfo = *localtime(&rawtime);
  char timeString[64];
  strftime(timeString, sizeof(timeString), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(timeString);
}
