/**
 * OxygenAnalyzer - Original + Warmup Integration
 * 
 * ✅ แก้ไขจากต้นฉบับ 2 จุดเท่านั้น:
 *   1. CONCENTRATION_OFFSET: 1.00 → 0.40
 *   2. เพิ่ม Warm-up 60 วิ ใน setup() ระหว่าง WiFi กำลัง connect
 * 
 * ทุกอย่างอื่นเหมือนเดิมทุกบรรทัด
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// =============================================
//  ⚙️ CONFIG (เหมือนเดิม)
// =============================================
const char* ssid              = "someone2G";
const char* password          = "ab123654";
const char* ntpServer         = "pool.ntp.org";
const long  gmtOffset_sec     = 7 * 3600;
const int   daylightOffset_sec = 0;
const char* MQTT_SERVER       = "broker.netpie.io";
const int   MQTT_PORT         = 1883;
const char* CLIENT_ID = "d8914184-d6e2-413d-ba70-9ff7b3d41dbd";
const char* TOKEN = "zJxHJBYF3c6iXmPFuffAdBXJY73mTuHJ";
const char* SECRET = "QWJyamMCyqgJ72NiEKNp7SFAkvCq3yvU";

// << แก้ไข #1: ลด offset จาก 1.00 → 0.40 >>
// เหตุผล: sensor อุ่นแล้ว raw ≈ 20.50%, target 20.90% → ต้องการ +0.40 เท่านั้น
const float CONCENTRATION_OFFSET = 0.40;
const float FLOW_OFFSET          = -0.90;
const float TEMPERATURE_OFFSET   = 0.00;

// << แก้ไข #2: เพิ่ม Warm-up config >>
const int WARMUP_SECONDS = 60;  // วินาทีรอ sensor นิ่ง

// =============================================
//  📦 GLOBAL VARIABLES (เหมือนเดิมทุกบรรทัด)
// =============================================
TFT_eSPI tft = TFT_eSPI(135, 240);
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastSendTime       = 0;
const unsigned long SEND_INTERVAL = 30000;
unsigned long lastStatusUpdateTime = 0;

float lastConcentration  = 0;
float lastFlow           = 0;
float lastTemperature    = 0;
bool  dataReady          = false;
String lastReceivedTimestamp = "--:--:--";
String lastSentTimestamp     = "--:--:--";

bool wifiStatus    = false;
bool netpieStatus  = false;
bool lastSendStatus = false;

// =============================================
//  🔧 FUNCTION PROTOTYPES (เหมือนเดิม + เพิ่ม warmup)
// =============================================
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

// << แก้ไข #2: เพิ่ม prototype ใหม่ >>
void runWarmup();
void readSensorRawOnly();
void drawWarmupScreen(int elapsed, int remaining, bool wifiOk);

// =============================================
//  🚀 SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  Serial.println("OxygenAnalyzer - With Warmup");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // เริ่ม Sensor Serial ก่อน เพื่อให้อ่านค่า live ระหว่าง warmup ได้
  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // << แก้ไข #2: เริ่ม WiFi แบบ non-blocking แล้วทำ warmup ไปพร้อมกัน >>
  WiFi.begin(ssid, password);

  // แสดงหน้าจอ Warmup 60 วิ (WiFi กำลัง connect ในพื้นหลัง)
  runWarmup();

  // หลังจาก warmup เสร็จ → ทำงานต่อแบบเดิมทุกอย่าง
  wifiStatus = (WiFi.status() == WL_CONNECTED);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  drawInterface();
}

// =============================================
//  🔄 LOOP (เหมือนเดิมทุกบรรทัด ไม่แตะเลย)
// =============================================
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

// =============================================
//  << แก้ไข #2: ฟังก์ชัน Warmup ใหม่ >>
// =============================================

// อ่าน sensor แบบ raw เฉยๆ (ไม่ใส่ offset ไม่อัปเดตหน้าจอ)
void readSensorRawOnly() {
  if (SensorSerial.available() >= 12) {
    if (SensorSerial.read() == 0x16) {
      byte data[12];
      data[0] = 0x16;
      SensorSerial.readBytes(&data[1], 11);
      byte checksum = 0;
      for (int i = 0; i < 11; i++) checksum += data[i];
      if (((checksum + data[11]) & 0xFF) == 0) {
        lastConcentration = (data[3] * 256 + data[4]) / 10.0; // raw ไม่ใส่ offset
        lastFlow          = (data[5] * 256 + data[6]) / 10.0;
        lastTemperature   = (data[7] * 256 + data[8]) / 10.0;
        dataReady = true;
      }
    }
  }
}

void drawWarmupScreen(int elapsed, int remaining, bool wifiOk) {
  // --- Title bar ---
  tft.fillRect(0, 0, 240, 18, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_CYAN);
  tft.drawString("SENSOR WARM-UP", 120, 2, 2);
  tft.setTextDatum(TL_DATUM);

  // --- Raw O2 (live) ---
  tft.fillRect(0, 22, 240, 20, TFT_BLACK);
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString("Raw O2:", 10, 24, 2);
  if (dataReady) {
    tft.setTextColor(TFT_WHITE);
    tft.drawString(String(lastConcentration, 2) + " %", 95, 24, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("No Signal", 95, 24, 2);
  }

  // --- WiFi status ---
  tft.fillRect(0, 44, 240, 18, TFT_BLACK);
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString("WiFi:", 10, 46, 2);
  if (wifiOk) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Connected", 65, 46, 2);
  } else {
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("Connecting...", 65, 46, 2);
  }

  // --- Info text ---
  tft.fillRect(0, 64, 240, 18, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("!! Keep in Open Air !!", 120, 66, 1);
  tft.setTextDatum(TL_DATUM);

  // --- Progress bar ---
  int percent = (elapsed * 100) / WARMUP_SECONDS;
  if (percent > 100) percent = 100;
  const int PB_X = 10, PB_Y = 86, PB_W = 220, PB_H = 14;
  tft.drawRect(PB_X, PB_Y, PB_W, PB_H, TFT_DARKGREY);
  int fillW = ((PB_W - 2) * percent) / 100;
  tft.fillRect(PB_X + 1, PB_Y + 1, fillW, PB_H - 2, TFT_BLUE);
  tft.fillRect(PB_X + 1 + fillW, PB_Y + 1, PB_W - 2 - fillW, PB_H - 2, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(String(percent) + "%", PB_X + PB_W / 2, PB_Y + PB_H / 2, 1);
  tft.setTextDatum(TL_DATUM);

  // --- Countdown ---
  tft.fillRect(0, 104, 240, 18, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Please wait: " + String(remaining) + " s", 120, 106, 2);
  tft.setTextDatum(TL_DATUM);

  // --- O2 with offset preview (เมื่อ sensor ready) ---
  tft.fillRect(0, 120, 240, 16, TFT_BLACK);
  if (dataReady) {
    float preview = lastConcentration + CONCENTRATION_OFFSET;
    tft.setTextColor(TFT_DARKGREY);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("After offset: " + String(preview, 2) + " %", 120, 122, 1);
    tft.setTextDatum(TL_DATUM);
  }
}

void runWarmup() {
  unsigned long warmupStart = millis();

  while (true) {
    int elapsed   = (millis() - warmupStart) / 1000;
    int remaining = WARMUP_SECONDS - elapsed;

    if (remaining <= 0) break;

    // อ่าน sensor raw ระหว่างรอ
    readSensorRawOnly();

    // ตรวจสอบ WiFi
    bool wifiOk = (WiFi.status() == WL_CONNECTED);

    // วาดหน้าจอทุก 500ms
    drawWarmupScreen(elapsed, remaining, wifiOk);
    delay(500);

    Serial.printf("Warmup: %d/%d | Raw O2: %.2f%%\n",
                  elapsed, WARMUP_SECONDS, lastConcentration);
  }

  // หลัง warmup เสร็จ: reset ค่า sensor ให้ loop() เริ่มใหม่สะอาด
  dataReady        = false;
  lastConcentration = 0;
  lastFlow          = 0;
  lastTemperature   = 0;

  Serial.println(">> WARMUP complete → Starting normal operation");
  tft.fillScreen(TFT_BLACK);
}

// =============================================
//  ✅ ฟังก์ชันเหมือนเดิมทุกบรรทัด (ไม่แตะ)
// =============================================

void drawInterface() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("O2:", 10, 30, 2);
  tft.setTextColor(TFT_CYAN);
  tft.drawString("Flow:", 10, 55, 2);
  tft.setTextColor(TFT_ORANGE);
  tft.drawString("Temp:", 10, 80, 2);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Rx:", 10, 105, 2);
  tft.drawString(lastReceivedTimestamp, 45, 105, 2);
  tft.drawString("Tx:", 130, 105, 2);
  tft.drawString(lastSentTimestamp, 165, 105, 2);
  tft.drawString("WF:", 10, 120, 2);
  tft.drawString("NP:", 85, 120, 2);
  tft.drawString("TX:", 160, 120, 2);
}

void updateDateTimeAndStatus() {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(getCurrentDateTimeString(), tft.width() / 2, 5, 2);
  tft.setTextDatum(TL_DATUM);
  tft.fillRect(45, 120, 38, 16, TFT_BLACK);
  if (wifiStatus) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("OK", 45, 120, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("...", 45, 120, 2);
  }
  tft.fillRect(120, 120, 38, 16, TFT_BLACK);
  if (netpieStatus) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("OK", 120, 120, 2);
  } else {
    tft.setTextColor(wifiStatus ? TFT_YELLOW : TFT_RED);
    tft.drawString("...", 120, 120, 2);
  }
  tft.fillRect(195, 120, 45, 16, TFT_BLACK);
  if (lastSendStatus) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("OK", 195, 120, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("FAIL", 195, 120, 2);
  }
}

void updateSensorDisplay() {
  tft.setTextDatum(TR_DATUM);
  String text;
  text = String(lastConcentration, 2) + " %";
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillRect(80, 30, 155, 20, TFT_BLACK);
  tft.drawString(text, 235, 30, 2);
  text = String(lastFlow, 2) + " L/m";
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.fillRect(80, 55, 155, 20, TFT_BLACK);
  tft.drawString(text, 235, 55, 2);
  text = String(lastTemperature, 2) + " C";
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.fillRect(80, 80, 155, 20, TFT_BLACK);
  tft.drawString(text, 235, 80, 2);
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
        lastFlow          = ((data[5] * 256 + data[6]) / 10.0) + FLOW_OFFSET;
        lastTemperature   = ((data[7] * 256 + data[8]) / 10.0) + TEMPERATURE_OFFSET;
        dataReady = true;
        lastReceivedTimestamp = getCurrentTimeOnly();
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.fillRect(45, 105, 80, 16, TFT_BLACK);
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
  String sendTime = getCurrentDateTimeString();
  char concStr[10], flowStr[10], tempStr[10];
  dtostrf(lastConcentration, 4, 2, concStr);
  dtostrf(lastFlow, 4, 2, flowStr);
  dtostrf(lastTemperature, 4, 2, tempStr);
  StaticJsonDocument<500> shadowDoc;
  shadowDoc["data"]["oxygen_concentration"] = concStr;
  shadowDoc["data"]["flow_rate"]            = flowStr;
  shadowDoc["data"]["temperature"]          = tempStr;
  shadowDoc["data"]["send_time"]            = sendTime;
  String shadowPayload;
  serializeJson(shadowDoc, shadowPayload);
  String shadowTopic = "@shadow/data/update";
  if (client.publish(shadowTopic.c_str(), shadowPayload.c_str())) {
    Serial.println("✅ Shadow sent successfully");
    lastSendStatus = true;
    lastSentTimestamp = getCurrentTimeOnly();
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillRect(165, 105, 80, 16, TFT_BLACK);
    tft.drawString(lastSentTimestamp, 165, 105, 2);
  } else {
    Serial.println("❌ Shadow send failed!");
    lastSendStatus = false;
  }
  updateDateTimeAndStatus();
}

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
