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
const float CONCENTRATION_OFFSET = 0.00;
const float FLOW_OFFSET = 0.00;
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
void drawTimestampLine(); // << เพิ่ม: ฟังก์ชันสำหรับวาดแถวเวลา Rx/Tx

void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor (Center Aligned)");
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

// << แก้ไข: ฟังก์ชันนี้จะวาดแค่หน้าจอว่างๆ และเรียกฟังก์ชันอัปเดตครั้งแรก >>
void drawInterface() {
  tft.fillScreen(TFT_BLACK);
  updateSensorDisplay();
  drawTimestampLine();
  updateDateTimeAndStatus();
}


// << แก้ไข: จัด Layout การแสดงผลเวลาและสถานะทั้งหมดให้อยู่กึ่งกลาง >>
void updateDateTimeAndStatus() {
  // --- 1. อัปเดตวันที่และเวลา (จัดกึ่งกลางเหมือนเดิม) ---
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(getCurrentDateTimeString(), tft.width() / 2, 5, 2);

  // --- 2. อัปเดตสถานะการเชื่อมต่อ (จัดกึ่งกลาง) ---
  tft.setTextDatum(TL_DATUM); // ใช้ TL Datum เพื่อคำนวณตำแหน่ง
  tft.fillRect(0, 120, 240, 16, TFT_BLACK); // เคลียร์ทั้งแถว

  // คำนวณความกว้างทั้งหมดของแถวสถานะเพื่อหาจุดเริ่มต้น
  String wf_s = wifiStatus ? "OK" : "...";
  String np_s = netpieStatus ? "OK" : "...";
  String tx_s = lastSendStatus ? "OK" : "FAIL";
  int total_width = tft.textWidth("WF:") + tft.textWidth(wf_s) + tft.textWidth(" NP:") + tft.textWidth(np_s) + tft.textWidth(" TX:") + tft.textWidth(tx_s);
  int current_x = (tft.width() - total_width) / 2; // หาจุดเริ่มต้น (Start X)

  // วาดแต่ละส่วนตามลำดับจากจุดเริ่มต้น
  tft.setTextColor(TFT_WHITE);
  tft.drawString("WF:", current_x, 120, 2);
  current_x += tft.textWidth("WF:");
  tft.setTextColor(wifiStatus ? TFT_GREEN : TFT_RED);
  tft.drawString(wf_s, current_x, 120, 2);
  current_x += tft.textWidth(wf_s);

  tft.setTextColor(TFT_WHITE);
  tft.drawString(" NP:", current_x, 120, 2);
  current_x += tft.textWidth(" NP:");
  tft.setTextColor(netpieStatus ? TFT_GREEN : (wifiStatus ? TFT_YELLOW : TFT_RED));
  tft.drawString(np_s, current_x, 120, 2);
  current_x += tft.textWidth(np_s);

  tft.setTextColor(TFT_WHITE);
  tft.drawString(" TX:", current_x, 120, 2);
  current_x += tft.textWidth(" TX:");
  tft.setTextColor(lastSendStatus ? TFT_GREEN : TFT_RED);
  tft.drawString(tx_s, current_x, 120, 2);
}


// << แก้ไข: จัดตำแหน่งการแสดงค่า Sensor ให้อยู่กึ่งกลาง >>
void updateSensorDisplay() {
    char valBuffer[10];
    String line;
    // --- ตั้งค่าการวาดให้อยู่กึ่งกลาง ---
    tft.setTextDatum(TC_DATUM);

    // --- แสดงค่า Oxygen ---
    dtostrf(lastConcentration, 5, 2, valBuffer); // จัด format (กว้าง 5, ทศนิยม 2)
    line = "O2:   " + String(valBuffer) + " %";
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.fillRect(0, 30, 240, 20, TFT_BLACK); // เคลียร์ทั้งแถว
    tft.drawString(line, tft.width() / 2, 30, 2);

    // --- แสดงค่า Flow ---
    dtostrf(lastFlow, 5, 2, valBuffer);
    line = "Flow: " + String(valBuffer) + " L/m";
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.fillRect(0, 55, 240, 20, TFT_BLACK);
    tft.drawString(line, tft.width() / 2, 55, 2);

    // --- แสดงค่า Temperature ---
    dtostrf(lastTemperature, 5, 2, valBuffer);
    line = "Temp: " + String(valBuffer) + " C";
    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
    tft.fillRect(0, 80, 240, 20, TFT_BLACK);
    tft.drawString(line, tft.width() / 2, 80, 2);

    // --- ตั้งค่ากลับเป็นปกติ ---
    tft.setTextDatum(TL_DATUM);
}


// << เพิ่ม: ฟังก์ชันสำหรับวาดแถวเวลา Rx/Tx ให้อยู่กึ่งกลาง >>
void drawTimestampLine() {
    String line = "Rx: " + lastReceivedTimestamp + "   Tx: " + lastSentTimestamp;
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillRect(0, 105, 240, 16, TFT_BLACK);
    tft.drawString(line, tft.width() / 2, 105, 2);
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

        lastReceivedTimestamp = getCurrentTimeOnly();
        drawTimestampLine(); // << แก้ไข: เรียกฟังก์ชันวาดแถวเวลาใหม่
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
    lastSentTimestamp = getCurrentTimeOnly();
    drawTimestampLine(); // << แก้ไข: เรียกฟังก์ชันวาดแถวเวลาใหม่
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
