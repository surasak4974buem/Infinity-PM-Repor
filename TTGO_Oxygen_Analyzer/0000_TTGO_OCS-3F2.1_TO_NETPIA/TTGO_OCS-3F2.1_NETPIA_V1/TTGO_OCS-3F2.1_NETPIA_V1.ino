#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- ส่วนตั้งค่า WiFi ---
const char* ssid = "someone2G";           // เปลี่ยนเป็น WiFi ของคุณ
const char* password = "ab123654";    // เปลี่ยนเป็นรหัสผ่าน WiFi

// --- ส่วนตั้งค่า NETPIE ---
const char* MQTT_SERVER = "broker.netpie.io";
const int MQTT_PORT = 1883;
const char* CLIENT_ID = "d8914184-d6e2-413d-ba70-9ff7b3d41dbd";
const char* TOKEN = "zJxHJBYF3c6iXmPFuffAdBXJY73mTuHJ";
const char* SECRET = "QWJyamMCyqgJ72NiEKNp7SFAkvCq3yvU";

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

void setup() {
  Serial.begin(115200);
  Serial.println("TTGO ESP32 - OCS-3F Sensor Reader with NETPIE");
  
  // --- เริ่มต้นการทำงานของจอ TFT ---
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  // --- เชื่อมต่อ WiFi ---
  connectToWiFi();
  
  // --- ตั้งค่า MQTT ---
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
}

void updateConnectionStatus() {
  tft.fillRect(60, 125, 180, 15, TFT_BLACK);
  
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
        // Checksum ถูกต้อง
        lastConcentration = (data[3] * 256 + data[4]) / 10.0;
        lastFlow = (data[5] * 256 + data[6]) / 10.0;
        lastTemperature = (data[7] * 256 + data[8]) / 10.0;
        dataReady = true;
        
        Serial.printf("O2: %.1f %% | Flow: %.1f L/min | Temp: %.1f C\n", 
                     lastConcentration, lastFlow, lastTemperature);
        
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
  // แสดงค่า Oxygen
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.fillRect(95, 20, 120, 18, TFT_BLACK);
  tft.drawString(String(lastConcentration, 1) + " %", 100, 20, 2);
  
  // แสดงค่า Flow
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.fillRect(95, 55, 120, 18, TFT_BLACK);
  tft.drawString(String(lastFlow, 1) + " L/m", 100, 55, 2);
  
  // แสดงค่า Temperature
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.fillRect(95, 90, 120, 18, TFT_BLACK);
  tft.drawString(String(lastTemperature, 1) + " C", 100, 90, 2);
  
  // อัปเดตสถานะการเชื่อมต่อ
  updateConnectionStatus();
}

void sendDataToNetpie() {
  if (!client.connected()) {
    Serial.println("NETPIE not connected, cannot send data");
    return;
  }
  
  // สร้าง JSON payload สำหรับ Shadow
  StaticJsonDocument<300> shadowDoc;
  shadowDoc["data"]["oxygen_concentration"] = lastConcentration;
  shadowDoc["data"]["flow_rate"] = lastFlow;
  shadowDoc["data"]["temperature"] = lastTemperature;
  shadowDoc["data"]["timestamp"] = millis();
  
  String shadowPayload;
  serializeJson(shadowDoc, shadowPayload);
  
  // ส่งข้อมูลไปที่ Shadow
  String shadowTopic = "@shadow/data/update";
  bool shadowSent = client.publish(shadowTopic.c_str(), shadowPayload.c_str());
  
  // สร้าง JSON payload สำหรับ Message
  StaticJsonDocument<200> msgDoc;
  msgDoc["oxygen"] = lastConcentration;
  msgDoc["flow"] = lastFlow;
  msgDoc["temperature"] = lastTemperature;
  msgDoc["timestamp"] = millis();
  
  String msgPayload;
  serializeJson(msgDoc, msgPayload);
  
  // ส่งข้อมูลไปที่ Message topic
  String msgTopic = "@msg/sensor_data";
  bool msgSent = client.publish(msgTopic.c_str(), msgPayload.c_str());
  
  Serial.println("Sending to NETPIE:");
  Serial.println("Shadow: " + shadowPayload);
  Serial.println("Message: " + msgPayload);
  
  // แสดงสถานะการส่งข้อมูลบนจอ
  tft.fillRect(150, 125, 90, 15, TFT_BLACK);
  if (shadowSent && msgSent) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Sent OK", 150, 125, 1);
    Serial.println("Data sent successfully to NETPIE");
  } else {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Send Error", 150, 125, 1);
    Serial.println("Failed to send data to NETPIE");
  }
}
