/**
 * OxygenAnalyzer - Full Integrated Version (Auto Zero Flow)
 * ✅ รายการแก้ไข:
 * 1. CONCENTRATION_OFFSET: 0.40
 * 2. Warm-up System: 60 วินาทีใน setup()
 * 3. Year Fix: ปรับการแสดงผลปีเป็น ค.ศ. 2026 (ดึงข้อมูล 4 หลัก)
 * 4. Auto Zero Flow: จับค่า Raw Flow ตอนเปิดเครื่องมาคำนวณเป็น Offset อัตโนมัติ
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// =============================================
//  ⚙️ CONFIGURATION
// =============================================
const char* ssid              = "someone2G";
const char* password          = "ab123654";
const char* ntpServer         = "pool.ntp.org";
const long  gmtOffset_sec     = 7 * 3600;
const int   daylightOffset_sec = 0;

const char* MQTT_SERVER       = "broker.netpie.io";
const int   MQTT_PORT         = 1883;
const char* CLIENT_ID         = "d8914184-d6e2-413d-ba70-9ff7b3d41dbd";
const char* TOKEN             = "zJxHJBYF3c6iXmPFuffAdBXJY73mTuHJ";
const char* SECRET            = "QWJyamMCyqgJ72NiEKNp7SFAkvCq3yvU";

// --- แก้ไขจุดนี้ ---
// เปลี่ยน FLOW_OFFSET จาก const float เป็น float เพื่อให้คำนวณ Auto Zero ได้
const float CONCENTRATION_OFFSET = 0.40; 
float FLOW_OFFSET                = 0.00; 
const float TEMPERATURE_OFFSET   = 0.00;

// ตั้งค่าเวลา Warm-up
const int WARMUP_SECONDS = 60; 

// =============================================
//  📦 GLOBAL VARIABLES
// =============================================
TFT_eSPI tft = TFT_eSPI(135, 240);
#define SENSOR_RX_PIN 13
#define SENSOR_TX_PIN 15
HardwareSerial SensorSerial(2);

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastSendTime         = 0;
const unsigned long SEND_INTERVAL  = 30000;
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
//  🔧 FUNCTION PROTOTYPES
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
void runWarmup();
void readSensorRawOnly();
void drawWarmupScreen(int elapsed, int remaining, bool wifiOk);

// =============================================
//  🚀 SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  Serial.println("OxygenAnalyzer - Starting...");

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  SensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // เริ่ม WiFi แบบ non-blocking
  WiFi.begin(ssid, password);

  // เข้าสู่โหมด Warm-up 60 วินาที
  runWarmup();

  // หลัง Warm-up เสร็จ ทำการซิงค์เวลาและตั้งค่า MQTT
  wifiStatus = (WiFi.status() == WL_CONNECTED);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  
  drawInterface();
}

// =============================================
//  🔄 LOOP (Logic คงเดิม 100%)
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
//  🛠️ WARM-UP & ZERO CALIBRATION
// =============================================

void readSensorRawOnly() {
  if (SensorSerial.available() >= 12) {
    if (SensorSerial.read() == 0x16) {
      byte data[12];
      data[0] = 0x16;
      SensorSerial.readBytes(&data[1], 11);
      byte checksum = 0;
      for (int i = 0; i < 11; i++) checksum += data[i];
      if (((checksum + data[11]) & 0xFF) == 0) {
        lastConcentration = (data[3] * 256 + data[4]) / 10.0; 
        lastFlow          = (data[5] * 256 + data[6]) / 10.0; // ดึง Raw Flow มาด้วยเพื่อเตรียมทำ Zero
        lastTemperature   = (data[7] * 256 + data[8]) / 10.0;
        dataReady = true;
      }
    }
  }
}

void drawWarmupScreen(int elapsed, int remaining, bool wifiOk) {
  tft.fillRect(0, 0, 240, 18, TFT_NAVY);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_CYAN);
  tft.drawString("SENSOR WARM-UP", 120, 2, 2);

  tft.fillRect(0, 22, 240, 20, TFT_BLACK);
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Raw O2:", 10, 24, 2);
  if (dataReady) {
    tft.setTextColor(TFT_WHITE);
    tft.drawString(String(lastConcentration, 2) + " %", 95, 24, 2);
  } else {
    tft.setTextColor(TFT_RED);
    tft.drawString("Searching...", 95, 24, 2);
  }

  tft.fillRect(0, 44, 240, 18, TFT_BLACK);
  tft.setTextColor(TFT_DARKGREY);
  tft.drawString("WiFi Status:", 10, 46, 2);
  if (wifiOk) {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Connected", 110, 46, 2);
  } else {
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("Connecting...", 110, 46, 2);
  }

  int percent = (elapsed * 100) / WARMUP_SECONDS;
  if (percent > 100) percent = 100;
  tft.drawRect(10, 86, 220, 14, TFT_DARKGREY);
  tft.fillRect(11, 87, (218 * percent) / 100, 12, TFT_BLUE);
  
  tft.fillRect(0, 104, 240, 18, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Ready in: " + String(remaining) + " s", 120, 106, 2);
}

void runWarmup() {
  unsigned long warmupStart = millis();
  while (true) {
    int elapsed = (millis() - warmupStart) / 1000;
    int remaining = WARMUP_SECONDS - elapsed;
    if (remaining <= 0) break;

    readSensorRawOnly();
    drawWarmupScreen(elapsed, remaining, (WiFi.status() == WL_CONNECTED));
    delay(500);
  }
  
  // ✅ ระบบ Auto Zero Flow Calibration
  // เมื่อครบ 60 วิ นำค่า Raw Flow ณ เวลานั้นมาตั้งเป็นตัวลบ (Offset) เพื่อให้ Flow เริ่มที่ศูนย์
  if (dataReady) {
    FLOW_OFFSET = -lastFlow; 
    Serial.printf(">> Auto Zero Flow Calibrated: Offset = %.2f\n", FLOW_OFFSET);
  } else {
    FLOW_OFFSET = 0.00;
  }

  dataReady = false; 
  tft.fillScreen(TFT_BLACK);
}

// =============================================
//  📡 CORE FUNCTIONS
// =============================================

void drawInterface() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.drawString("O2:", 10, 30, 2);
  tft.setTextColor(TFT_CYAN);
  tft.drawString("Flow:", 10, 55, 2);
  tft.setTextColor(TFT_ORANGE);
  tft.drawString("Temp:", 10, 80, 2);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Rx:", 10, 105, 2);
  tft.drawString("Tx:", 130, 105, 2);
  tft.drawString("WF:", 10, 120, 2);
  tft.drawString("NP:", 85, 120, 2);
  tft.drawString("TX:", 160, 120, 2);
}

void readSensorData() {
  if (SensorSerial.available() >= 12) {
    if (SensorSerial.read() == 0x16) {
      byte data[12];
      data[0] = 0x16;
      SensorSerial.readBytes(&data[1], 11);
      byte checksum = 0;
      for (int i = 0; i < 11; i++) checksum += data[i];
      if (((checksum + data[11]) & 0xFF) == 0) {
        // นำค่าที่คำนวณ Auto Zero มาใช้บวกตรงนี้
        lastConcentration = ((data[3] * 256 + data[4]) / 10.0) + CONCENTRATION_OFFSET;
        lastFlow          = ((data[5] * 256 + data[6]) / 10.0) + FLOW_OFFSET;
        lastTemperature   = ((data[7] * 256 + data[8]) / 10.0) + TEMPERATURE_OFFSET;
        dataReady = true;
        lastReceivedTimestamp = getCurrentTimeOnly();
        updateSensorDisplay();
      }
    }
  }
}

void sendDataToNetpie() {
  if (!client.connected()) return;

  String sendTime = getCurrentDateTimeString();
  StaticJsonDocument<500> doc;
  
  char c[10], f[10], t[10];
  dtostrf(lastConcentration, 4, 2, c);
  dtostrf(lastFlow, 4, 2, f);
  dtostrf(lastTemperature, 4, 2, t);

  doc["data"]["oxygen_concentration"] = atof(c);
  doc["data"]["flow_rate"] = atof(f);
  doc["data"]["temperature"] = atof(t);
  doc["data"]["send_time"] = sendTime;

  String payload;
  serializeJson(doc, payload);
  if (client.publish("@shadow/data/update", payload.c_str())) {
    lastSendStatus = true;
    lastSentTimestamp = getCurrentTimeOnly();
  } else {
    lastSendStatus = false;
  }
  updateDateTimeAndStatus();
}

String getCurrentDateTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { return "No Time Sync"; }
  
  char timeString[25];
  // แสดงผลเป็น ค.ศ. แบบ 4 หลัก (เช่น 2026)
  strftime(timeString, sizeof(timeString), "%d/%m/%Y %H:%M:%S", &timeinfo); 
  
  return String(timeString);
}

String getCurrentTimeOnly() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { return "--:--:--"; }
  char timeString[10];
  strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
  return String(timeString);
}

void updateSensorDisplay() {
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillRect(80, 30, 155, 20, TFT_BLACK);
  tft.drawString(String(lastConcentration, 2) + " %", 235, 30, 2);
  
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.fillRect(80, 55, 155, 20, TFT_BLACK);
  tft.drawString(String(lastFlow, 2) + " L/m", 235, 55, 2);
  
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.fillRect(80, 80, 155, 20, TFT_BLACK);
  tft.drawString(String(lastTemperature, 2) + " C", 235, 80, 2);
}

void updateDateTimeAndStatus() {
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.fillRect(40, 5, 160, 20, TFT_BLACK);
  tft.drawString(getCurrentDateTimeString(), 120, 5, 2);

  tft.setTextDatum(TL_DATUM);
  tft.fillRect(45, 105, 80, 16, TFT_BLACK);
  tft.setTextColor(TFT_YELLOW);
  tft.drawString(lastReceivedTimestamp, 45, 105, 2);
  
  tft.fillRect(165, 105, 80, 16, TFT_BLACK);
  tft.drawString(lastSentTimestamp, 165, 105, 2);

  tft.fillRect(45, 120, 35, 16, TFT_BLACK);
  if (wifiStatus) { tft.setTextColor(TFT_GREEN); tft.drawString("OK", 45, 120, 2); }
  else { tft.setTextColor(TFT_RED); tft.drawString("!!", 45, 120, 2); }

  tft.fillRect(120, 120, 35, 16, TFT_BLACK);
  if (netpieStatus) { tft.setTextColor(TFT_GREEN); tft.drawString("OK", 120, 120, 2); }
  else { tft.setTextColor(TFT_RED); tft.drawString("!!", 120, 120, 2); }

  tft.fillRect(195, 120, 45, 16, TFT_BLACK);
  if (lastSendStatus) { tft.setTextColor(TFT_GREEN); tft.drawString("OK", 195, 120, 2); }
  else { tft.setTextColor(TFT_RED); tft.drawString("FAIL", 195, 120, 2); }
}

void connectToWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(ssid, password);
}

void reconnectNetpie() {
  if (client.connect(CLIENT_ID, TOKEN, SECRET)) {
    netpieStatus = true;
    client.subscribe("@shadow/data/update");
  } else {
    netpieStatus = false;
  }
}

void callback(char* topic, byte* payload, unsigned int length) { }
