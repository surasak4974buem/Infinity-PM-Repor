# INFINITY Oxygen Concentrator PM Inspection & CYD Analyzer System

ระบบบริหารจัดการงานตรวจเช็คบำรุงรักษาเครื่องผลิตออกซิเจน (**PM Inspection Report**) และออกแบบเครื่องวิเคราะห์ออกซิเจนความแม่นยำสูง (**ESP32-CYD Oxygen Analyzer**) สำหรับ **INFINITY SERVICE MIND AND EQUIPMENT**

## ไฟล์หลักในโปรเจกต์
- **`PM_Inspection_Report_Infinity.html`** — เว็บแอปออกใบรายงานผลการตรวจเช็คบำรุงรักษา (PM Inspection Report) 2 หน้า A4 พร้อม Export PDF, นำเข้า/ขยับ/ย่อ-ขยายลายเซ็น, นำเข้าไฟล์ `.CSV` จากเครื่อง CYD และเชื่อมต่อบันทึก/ดึงข้อมูลบน GitHub Private Cloud (`records/`) พร้อมระบบ **Force Save As** ป้องกันการบันทึกทับประวัติเดิม
- **`records/`** — โฟลเดอร์จัดเก็บประวัติใบรายงานผลตรวจเช็คของเครื่องแต่ละ S/N ในรูปแบบ `.json`
- **`SYSTEM_DESIGN.md`** — เอกสารออกแบบสถาปัตยกรรมระบบ ESP32-CYD Purity Oxygen Analyzer (BLE Server + SD Card + Metrology Calibration)
- **`Oxygen_Concentrator_Inspection_Form.html`** / **`INSPECTION_FORM.md`** — แบบฟอร์มตรวจเช็คมาตรฐาน (ISO 80601-2-69:2026 / ECRI 2017 / Uncertainty Budget)
- **`เช็คเครื่องผลิตออกซิเจน_มาตรฐาน2569_v2.xlsx`** — เทมเพลต Excel 6 ชีต (PM Form, Uncertainty Budget, Traceability Matrix, ระบบคำนวณต้นทุน-ค่าเดินทาง Isuzu 1.9, และใบเสนอราคาลูกค้า)
