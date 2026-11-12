# สถาปัตยกรรมและการออกแบบระบบ: High-Precision Purity Oxygen Analyzer (ESP32-CYD)
**เอกสารสรุปความคืบหน้าการออกแบบระบบ (System Architecture & Progress Specification)**  
*เวอร์ชัน: 1.0 (สถานะ: ขั้นตอนการออกแบบสถาปัตยกรรมและตรรกะระบบ)*  
*วันที่จัดทำ: 8 พฤศจิกายน 2026*  
*เป้าหมายหลัก: สร้างเครื่องวัดและวิเคราะห์ความบริสุทธิ์ออกซิเจนที่มีความแม่นยำสูง อิงเกณฑ์มาตรฐาน WHO/ISO และพร้อมส่งสอบเทียบเพื่อขอรับใบรับรอง (Calibration Certification ISO/IEC 17025 Ready)*

---

## 1. วัตถุประสงค์และมาตรฐานอ้างอิง (Objectives & Standards)

1. **มาตรฐานทางการแพทย์ระดับสากล:**
   - **WHO Technical Specifications for Oxygen Concentrators (2015):** เกณฑ์ชี้วัดความบริสุทธิ์ออกซิเจนต้องไม่ต่ำกว่า **82.0%**
   - **ISO 80601-2-69:2014 / ISO 8359:** มาตรฐานความปลอดภัยและสมรรถนะของอุปกรณ์ผลิตออกซิเจน
   - **IEC 60601-1-8:** มาตรฐานระบบสัญญาณเตือนทางการแพทย์ (Alarm Priorities)
2. **มาตรฐานการวัดและออกใบเซอร์ (Metrology & Certification Readiness):**
   - รองรับการทวนสอบเทียบกับเครื่องมือมาตรฐาน (Master Reference Instrument)
   - รองรับการสอบเทียบ 2 จุด (Two-Point Linear Calibration: Zero & Span)
   - บันทึกประวัติเครื่อง: Serial Number, วันที่สอบเทียบ (Cal Date), และกำหนดสอบเทียบครั้งถัดไป (Due Date)
3. **การใช้งานภาคสนามโดยช่างเทคนิค:**
   - ไม่รบกวนอินเทอร์เน็ตของโทรศัพท์ (ใช้ Bluetooth Low Energy แทน Wi-Fi เพื่อให้ช่างคุย LINE / ค้นหาข้อมูลได้ตามปกติ)
   - จัดเก็บข้อมูลลง MicroSD Card เป็นไฟล์ `.CSV` เปิดอ่านผ่าน **Notepad** ได้ทันที และมีฟอร์แมตที่ **AI สามารถดึงไปกรอกลงแบบฟอร์มใบเซอร์ได้อัตโนมัติ**

---

## 2. ฮาร์ดแวร์หลักของระบบ (Hardware Platform)

```
  ┌───────────────────────────────────────────────────────────────┐
  │                   ESP32-CYD (ESP32-2432S028R)                 │
  │   - Dual-Core Xtensa LX6 @ 240MHz, 520KB SRAM, 4MB Flash      │
  │   - 2.8" TFT 320x240 (ILI9341/ST7789) + XPT2046 Touch         │
  │   - On-board MicroSD Slot (SPI)                               │
  │   - Built-in Bluetooth Low Energy (BLE 4.2 / 5.0)             │
  └───────────────────────────────┬───────────────────────────────┘
                                  │ UART (9600 bps, 3.3V TTL)
                                  ▼
  ┌───────────────────────────────────────────────────────────────┐
  │        OCS-3F Ver 2.1 Ultrasonic O2 / Flow / Temp Sensor       │
  │   - O2 Range: 21.0% - 95.6% (Res: 0.1%, Acc: ±1.5% FS)        │
  │   - Flow Range: 0 - 10.0 L/min (Res: 0.1 L/min, Acc: ±0.2)    │
  │   - Gas Temperature: 0 - 50 °C (Real-time Compensation)       │
  │   - Power: +12V DC / 50mA                                     │
  │   - Output Cycle: Autosend ทุกๆ 500 ms (2 Hz)                 │
  └───────────────────────────────────────────────────────────────┘
```

---

## 3. ตรรกะการวัดและการสอบเทียบ (Measurement & Metrology Logic)

### 3.1 ตัวแปรชดเชยค่าแบบ Dynamic (Non-Const & NVS Persistent)
ตัวแปรจะไม่ถูกฟิกซ์ค่า (`const`) เพื่อให้ระบบสามารถปรับแต่งหน้างาน และบันทึกลงหน่วยความจำถาวร (ESP32 NVS / Preferences):
```cpp
// ตัวแปรการสอบเทียบ (บันทึกลง NVS)
float FLOW_OFFSET          = 0.00f; // ค่าชดเชยการไหล (ได้จาก Auto Zero)
float O2_ZERO_OFFSET       = 0.00f; // ค่าชดเชยจุดต่ำ (Room Air 20.9%)
float O2_SPAN_GAIN         = 1.00f; // สัมประสิทธิ์ความชัน (Master Span)
float TEMPERATURE_OFFSET   = 0.00f; // ค่าชดเชยอุณหภูมิ

// ค่าคอนฟิกการทำงาน
int   WARMUP_SECONDS       = 60;    // ระยะเวลาอุ่นเครื่อง (ปรับได้ 10-120 วินาที)
int   LOG_INTERVAL_MS      = 1000;  // ความถี่บันทึก SD Card (Default: 1s)
```

### 3.2 ขั้นตอนการทำงานตอนเปิดเครื่อง (Startup & Warm-up Sequence)
1. **State 1: Sensor Warm-up (นับถอยหลัง):**
   - แสดงเวลานับถอยหลัง `WARMUP_SECONDS` บนหน้าจอ
   - รอให้อุณหภูมิภายในและคลื่นเสียงอัลตราโซนิกเข้าสู่สภาวะสมดุลความร้อน (Thermal Equilibrium)
2. **State 2: Auto Zero Flow Prompt:**
   - ระบบแจ้งเตือนให้ผู้ใช้ปล่อยท่อว่าง (ไม่มีลมไหล)
   - เซ็นเซอร์อ่านค่า 10–20 ค่า นำมาหาค่าเฉลี่ย แล้วเซ็ตเป็น `FLOW_OFFSET`
   - เพิ่ม **Deadband/Clamping:** หากค่าคำนวณได้ $-0.05$ ถึง $+0.05$ L/min ให้แสดงเป็น `0.00 L/min` ป้องกันตัวเลขกระดิก
3. **State 3: Ready & Real-time Measurement:**
   - เข้าสู่หน้าจอหลัก แสดงค่า O2, Flow, Temp และไฟสถานะมาตรฐาน WHO

### 3.3 ตรรกะการสอบเทียบ 2 จุด (Two-Point Metrological Calibration)
เพื่อความแม่นยำสูงระดับห้องปฏิบัติการที่สามารถออกใบรับรองได้:
1. **จุดที่ 1 (Zero Point / Room Air):**
   - สัมผัสอากาศห้องบริสุทธิ์ ($20.9\% \text{ } O_2$)
   - บันทึกค่าดิบเป็น $\text{Raw}_{Air}$
2. **จุดที่ 2 (Span Point / Master Standard):**
   - จ่ายก๊าซมาตรฐานจากหัวถังหรือ Concentrator Master (เช่น $93.0\%$ หรือ $99.5\%$)
   - บันทึกค่าดิบเป็น $\text{Raw}_{Master}$
3. **สมการแปลงค่าเชิงเส้น ($y = mx + c$):**
   $$\text{Gain } (m) = \frac{\text{Master High Target} - 20.90}{\text{Raw}_{Master} - \text{Raw}_{Air}}$$
   $$O_2\% = m \times (\text{Raw}_{O2} - \text{Raw}_{Air}) + 20.90$$

---

## 4. สถานะไฟ LED และ UI ตามมาตรฐาน WHO & ISO 80601-2-69

| ระดับสถานะ | สีแสดงผล (UI / LED) | ช่วงความเข้มข้น ($O_2$) | ความหมายและระบบตอบสนอง |
| :---: | :---: | :---: | :--- |
| **NORMAL (ปกติ)** | 🟢 **GREEN** | **$\ge 82.0\%$** | **ผ่านเกณฑ์มาตรฐาน WHO** สำหรับการรักษา (ปกติ 82%–96%) |
| **WARNING (เฝ้าระวัง)** | 🟡 **YELLOW** | **$72.0\% \le O_2 < 82.0\%$** | **Low-Priority Alarm** ออกซิเจนต่ำกว่าเกณฑ์ ต้องตรวจเช็คไส้กรอง |
| **CRITICAL (วิกฤต)** | 🔴 **RED** | **$< 72.0\%$** *(หรือ No Flow)* | **High-Priority Alarm** ตกมาตรฐานอันตราย ต้องเปลี่ยนใช้ถังสำรอง |

---

## 5. การจัดการ MicroSD Card และรูปแบบข้อมูล (Data Logging)

### 5.1 การแสดงผลสถานะหน่วยความจำ (Storage Health Monitoring)
บนแถบสถานะ (Status Bar) ของหน้าจอ CYD จะแสดง:
* **Connection Status:** แสดงว่าใส่การ์ดอยู่หรือไม่ (`MOUNTED` / `NO_CARD` / `ERROR`)
* **พื้นที่ความจุ:** 
  - ความจุทั้งหมด (Total Size, e.g. `14.8 GB`)
  - พื้นที่ที่ใช้ไป (Used Space, e.g. `1.2 GB`)
  - พื้นที่คงเหลือ (Free Space, e.g. `13.6 GB (91%)`)
* **เกณฑ์แจ้งเตือน:**
  - 🟢 เขียว: พื้นที่เหลือ $> 20\%$
  - 🟡 เหลือง: พื้นที่เหลือ $< 10\%$ (เตือนให้สำรองข้อมูล)
  - 🔴 แดง: พื้นที่เหลือ $< 1\%$ หรือการ์ดมีปัญหา (หยุดเขียนเพื่อป้องกันไฟล์เสีย)

### 5.2 โครงสร้างไฟล์ `.CSV` (รองรับ Notepad & AI Auto-fill)
บันทึกเป็นชื่อไฟล์ตามวันเวลา เช่น `LOG_20261108_001.CSV`:
```csv
# ====================================================================
# OXYGEN ANALYZER METROLOGY TEST REPORT
# Device Serial Number : O2-CYD-2026-001
# Firmware Version     : v1.0.0-PRO
# Calibration Date     : 2026-11-08
# Flow Offset          : 0.00 L/min
# Zero Offset (Air)    : 0.00 %
# Span Gain            : 1.0000
# Target Standard      : WHO Technical Series (Threshold >= 82.0%)
# ====================================================================
Timestamp,Elapsed_s,O2_Purity_Pct,Flow_LPM,Temp_C,WHO_Status,Raw_O2,Raw_Flow
2026-11-08 14:00:01,1,93.4,5.0,24.5,NORMAL,93.4,5.0
2026-11-08 14:00:02,2,93.5,5.0,24.5,NORMAL,93.5,5.0
2026-11-08 14:00:03,3,93.5,5.1,24.6,NORMAL,93.5,5.1
```
* **ประโยชน์:** 
  1. ช่างเทคนิคเปิดดูด้วย **Notepad** เพื่อดูค่าทันที
  2. เปิดใน **Excel** เพื่อวาดกราฟประกอบใบรับรอง
  3. ส่งไฟล์นี้ให้ **AI** เพื่ออ่านค่าสถิติ (Mean, Max, Min, SD) แล้วกรอกลงฟอร์มออกใบรับรองอัตโนมัติ

---

## 6. สถาปัตยกรรม Bluetooth Server (BLE GATT Architecture)

*เหตุผลที่เลือก BLE แทน Wi-Fi:* ช่างสามารถเปิดโทรศัพท์เพื่อดูค่าวัดสดๆ หรือดึงรายงาน โดยที่โทรศัพท์**ยังคงต่อเน็ต 4G/5G หรือ Wi-Fi ของโรงพยาบาลเพื่อคุย LINE หรือโทรศัพท์ได้ตามปกติ 100%**

```
[ ESP32 CYD : BLE Device Name = "O2-ANALYZER-PRO" ]
  │
  ├── 1. Live Telemetry Service (UUID: 0x180D / Custom)
  │      └── Notify Characteristic (ส่งค่าทุก 500ms):
  │          [O2: 93.5 | Flow: 5.0 | Temp: 24.5 | Status: NORMAL]
  │
  ├── 2. Device Control & Calibration Service (Write Characteristic)
  │      └── รับคำสั่งจากโทรศัพท์/คอมพิวเตอร์:
  │          - "CMD:TARE"        -> สั่ง Zero Flow
  │          - "CMD:CAL_AIR"     -> สั่ง Calibrate 20.9%
  │          - "CMD:CAL_SPAN,93" -> สั่ง Calibrate Span 93%
  │          - "CMD:LOG_START"   -> สั่งเริ่มบันทึกไฟล์ใหม่
  │          - "CMD:LOG_STOP"    -> สั่งหยุดบันทึก
  │
  └── 3. Storage & Data Transfer Service (Read/Notify Characteristic)
         ├── Status: [Total: 14.8GB | Used: 1.2GB | Free: 13.6GB]
         └── File Dump: สตรีมข้อมูลจากไฟล์ CSV เข้าสู่มือถือ/คอมฯ แบบไร้สาย
```
* **Client Interface:** สามารถใช้งานผ่าน **Web Bluetooth บน Google Chrome** (เปิดไฟล์ HTML หน้าเดียว ไม่ต้องติดตั้งแอปลงเครื่อง) หรือใช้แอป BLE Terminal ทั่วไป

---

## 7. การบริหารคอร์ประมวลผล (ESP32 Dual-Core Concurrency)

เพื่อรักษาความแม่นยำสูงและไม่ให้การทำงานสะดุด:
* **Core 0 (Background & Communications):**
  - จัดการ Bluetooth Low Energy (BLE Server)
  - จัดการการเขียนไฟล์ลง SD Card ผ่าน Ring Buffer (Non-blocking I/O)
* **Core 1 (Real-time Instrumentation & GUI):**
  - อ่านข้อมูลเซ็นเซอร์ OCS-3F ผ่าน UART2 (ทุกๆ 500 ms)
  - ประมวลผล Moving Average Filter และสมการชดเชยค่า (Calibration Math)
  - เรนเดอร์หน้าจอ TFT และรับสัมผัสจาก XPT2046 Touch Controller

---

## 8. สถานะความคืบหน้าและขั้นตอนถัดไป (Progress & Next Steps)

- [x] **Phase 1: Research & Standard Alignment:** ศึกษามาตรฐาน WHO.pdf และ Data sheet OCS-3F 2.1 เสร็จสมบูรณ์
- [x] **Phase 2: Architectural Design:** กำหนดโครงสร้างระบบ, Metrology logic, BLE Server, และระบบ SD Card Storage เรียบร้อย
- [ ] **Phase 3 (ถัดไป): Pin Mapping & Hardware Validation:** ตรวจสอบขาพินของบอร์ด CYD (TFT, Touch, SD Card, UART2) เพื่อป้องกันการชนกันของพิน
- [ ] **Phase 4: UI / UX Layout Wireframe:** ออกแบบหน้าจอ CYD (Main Screen, Status Bar, Calibration Screen)
- [ ] **Phase 5: Implementation:** เริ่มเขียนโค้ดทีละโมดูลอย่างเป็นระบบ
