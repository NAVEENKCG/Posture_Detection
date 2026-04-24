# 🏥 PARALYZED PATIENT POSTURE & BLOOD CLOT PREVENTION SYSTEM
### IoT + AI + Robotics Medical Monitoring System | Version 2.0

---

## 📌 Project Overview

An intelligent medical IoT system designed exclusively for **paralyzed or immobile patients** to:
- Continuously monitor posture and body position
- Detect blood clot (DVT) risk from prolonged immobility
- Automatically perform passive leg exercises via servo motors
- Alert doctors and caretakers via a real-time web dashboard
- Capture and send AI-verified images during critical events
- Measure SpO₂ and heart rate continuously

> **Core Philosophy:** *"If the patient cannot move themselves, the system moves for them."*

---

## 🔧 Hardware Components

| Component | Model | Purpose | Price (INR ~) |
|-----------|-------|---------|--------------|
| Microcontroller | ESP32-CAM (AI Thinker) | Main MCU + WiFi + Camera | ₹450 |
| IMU Sensor | MPU-6050 | Posture, tilt, movement detection | ₹80 |
| Pulse Oximeter | MAX30102 | SpO₂ + Heart Rate | ₹350 |
| Servo Motors (×4) | SG90 / MG996R | Passive leg & ankle exercise | ₹80–200 each |
| Buzzer | Piezo Active 5V | Audio alerts | ₹20 |
| LEDs (RGB) | 5mm Common Cathode | Visual status indicator | ₹10 |
| Vibration Motor | 3V Coin Cell Motor | Caretaker haptic wearable | ₹30 |
| Emergency Button | Tactile / Latching | Manual emergency trigger | ₹15 |
| Power Supply | 5V 3A USB / LiPo | ESP32 + Servos | ₹200 |
| Mounting Frame | 3D Printed / Velcro | Attach to bed/patient | Custom |

**Total Estimated Cost: ₹1,500 – ₹2,500**

---

## 🔌 Wiring Diagram

```
ESP32-CAM
├── GPIO 21 (SDA) ──────────────┬── MPU6050 SDA
│                                └── MAX30102 SDA
├── GPIO 22 (SCL) ──────────────┬── MPU6050 SCL
│                                └── MAX30102 SCL
├── GPIO 13 ──────────────────── Servo Left Leg (Signal)
├── GPIO 12 ──────────────────── Servo Right Leg (Signal)
├── GPIO 14 ──────────────────── Servo Left Ankle (Signal)
├── GPIO 27 ──────────────────── Servo Right Ankle (Signal)
├── GPIO 26 ──────────────────── Buzzer (+)
├── GPIO 25 ──────────────────── LED Red
├── GPIO 33 ──────────────────── LED Green
├── GPIO 32 ──────────────────── LED Blue
├── GPIO 15 ──────────────────── Vibration Motor
├── GPIO 0  ──────────────────── Emergency Button (to GND)
├── 3.3V ───────────────────────┬── MPU6050 VCC
│                                └── MAX30102 VCC
└── GND ────────────────────────┬── All GND connections
                                  └── Servo GND

Servo Power: Connect servo VCC to 5V (external or USB), NOT 3.3V!
```

---

## 📚 Required Libraries (Arduino IDE)

Install via **Library Manager** (`Sketch → Include Library → Manage Libraries`):

```
MPU6050                   → by Electronic Cats
ESP32Servo                → by Kevin Harrington
ArduinoJson               → by Benoit Blanchon (v6.x)
MAX30105                  → by SparkFun Electronics
SparkFun_MAX3010x_Sensor  → by SparkFun
base64                    → by Densaugeo
ESP32 Camera              → (built into ESP32 board package)
```

**Board Package:**
```
Tools → Board → Boards Manager → Search "esp32" → Install "esp32 by Espressif Systems"
```

**Board Settings:**
```
Board:           AI Thinker ESP32-CAM
Upload Speed:    115200
CPU Frequency:   240 MHz
Flash Frequency: 80 MHz
Flash Size:      4MB (32Mb)
Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
```

---

## ⚙️ Configuration

Before flashing, update these lines in `main.ino`:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* SERVER_URL    = "https://your-backend.onrender.com";
const char* DEVICE_ID     = "PATIENT_001";
const char* PATIENT_NAME  = "Patient Name";
```

---

## 🔄 System Flow

```
[Boot] → [WiFi Connect] → [Sensor Init] → [Main Loop]
                                                │
              ┌─────────────────────────────────┤
              ↓                                 ↓
    [Every 1s: Read IMU]              [Every 30min: Exercise]
    [Every 5s: Read SpO₂/HR]          ↓
    [Every 30s: Send to Server]    [Servo Routine 30s]
              ↓                    [Log to Server]
    [Risk Calculation]
              ↓
    ┌─────────┴──────────┐
    │  Risk Level 0-4    │
    └────────────────────┘
         ↓         ↓
    [LED Color]  [Alert Server]
    [Buzzer]     [Capture Image if Critical]
    [Vibration]
```

---

## 📊 Risk Level System

| Level | Name | Immobility | SpO₂ | Action |
|-------|------|-----------|------|--------|
| 0 | Safe | < 10 min | > 95% | Green LED |
| 1 | Low | 10–15 min | > 95% | Yellow LED |
| 2 | Medium | 15–30 min | 92–95% | Red LED + 1 beep |
| 3 | High | 30–60 min | < 92% | Red + 3 beeps + vibrate |
| 4 | Critical | > 60 min | < 90% | Alarm + Camera + Server Push |

---

## 🌐 API Endpoints (Backend)

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/api/telemetry` | Receive sensor data |
| POST | `/api/alert` | Receive alert events |
| POST | `/api/image` | Receive camera snapshots |
| GET  | `/api/patient/:id` | Get patient current state |
| GET  | `/api/history/:id` | Get patient history |
| POST | `/api/command/:id` | Send command to device |

---

## 🚀 Flash Instructions

1. Connect ESP32-CAM to USB via FTDI adapter (TX→RX0, RX→TX0, GND→GND, 5V→5V)
2. Connect GPIO0 to GND (flash mode)
3. Press RESET button
4. Upload from Arduino IDE
5. Disconnect GPIO0 from GND
6. Press RESET to run

---

## 🔒 Safety Notes

- ⚠️ This is a **research/educational prototype**. Not a certified medical device.
- All servo movements must be validated by a physiotherapist before patient use.
- Maximum servo exercise torque: use MG996R (10kg-cm) for adult patients.
- Ensure device is waterproof if near patient (use conformal coating on PCB).
- Battery backup recommended (hospital power cuts).

---

## 📱 Web Dashboard Features

- Real-time patient vitals (HR, SpO₂, posture)
- Risk level with color-coded alerts
- Exercise history and schedule
- Camera snapshot viewer
- Push notification to doctor/caretaker
- Manual exercise trigger
- Patient position history log
- Multi-patient support

---

## 🆚 Comparison with Commercial Products

| Feature | Posture Corrector (Amazon) | Our System |
|---------|---------------------------|------------|
| Target Users | Healthy office workers | Paralyzed patients |
| Leg Monitoring | ❌ | ✅ |
| Blood Clot Risk | ❌ | ✅ |
| Auto Exercise | ❌ | ✅ Servo-driven |
| SpO₂ Monitor | ❌ | ✅ |
| Doctor Dashboard | ❌ | ✅ |
| Camera AI Verify | ❌ | ✅ |
| Emergency Alert | ❌ | ✅ |
| Medical Use | ❌ | ✅ |

---

## 👨‍💻 Author
Built as an academic IoT medical project.  
For viva questions, highlight: **DVT prevention**, **passive exercise**, **multi-sensor fusion**, **real-time telemedicine dashboard**.
