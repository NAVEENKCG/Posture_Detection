/*
 * ============================================================
 * PARALYZED PATIENT POSTURE & BLOOD CLOT PREVENTION SYSTEM
 * Hardware: ESP32 + MPU6050 + MAX30102 + Servo Motors + Camera
 * Version: 2.0 | Medical IoT System
 * ============================================================
 */

#include <Wire.h>
#include <MPU6050.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "heartRate.h"
#include <esp_camera.h>
#include <base64.h>
#include <Preferences.h>
#include <time.h>

// ============================================================
// CONFIGURATION — Update these before flashing
// ============================================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* SERVER_URL    = "https://your-backend.onrender.com";  // Your backend
const char* DEVICE_ID     = "PATIENT_001";
const char* PATIENT_NAME  = "Patient Name";

// ============================================================
// PIN DEFINITIONS (ESP32-CAM / ESP32 WROOM)
// ============================================================
// I2C (MPU6050 + MAX30102)
#define SDA_PIN           21
#define SCL_PIN           22

// Servo Motors (Leg Movement Simulation)
#define SERVO_LEFT_LEG    13
#define SERVO_RIGHT_LEG   12
#define SERVO_LEFT_ANKLE  14
#define SERVO_RIGHT_ANKLE 27

// Buzzer & LED Alerts
#define BUZZER_PIN        26
#define LED_RED           25
#define LED_GREEN         33
#define LED_BLUE          32

// Vibration Motor (haptic alert for caretaker wearable)
#define VIBRATION_PIN     15

// Emergency Button
#define EMERGENCY_BTN     0   // BOOT button on most ESP32 boards

// Camera pins (ESP32-CAM AI Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ============================================================
// THRESHOLDS & CONSTANTS
// ============================================================
#define POSITION_CHANGE_INTERVAL   1800000  // 30 min in ms
#define IMMOBILITY_ALERT_THRESHOLD 900000   // 15 min immobile = high risk
#define HIGH_RISK_IMMOBILITY       3600000  // 60 min = critical risk
#define EXERCISE_DURATION          30000    // 30 sec passive exercise
#define HEART_RATE_LOW             50
#define HEART_RATE_HIGH            120
#define SPO2_CRITICAL              92       // Below 92% = critical
#define SPO2_WARNING               95       // Below 95% = warning
#define TILT_THRESHOLD             30.0     // degrees
#define ACCEL_MOVEMENT_THRESHOLD   0.8      // g-force for movement detection

// ============================================================
// SENSOR OBJECTS
// ============================================================
MPU6050    imu;
MAX30105   pulseOximeter;
Servo      servoLeftLeg, servoRightLeg;
Servo      servoLeftAnkle, servoRightAnkle;
Preferences prefs;

// ============================================================
// GLOBAL STATE
// ============================================================
struct PatientState {
  float   accelX, accelY, accelZ;
  float   gyroX,  gyroY,  gyroZ;
  float   tiltAngle;
  float   rollAngle;
  int32_t heartRate;
  int32_t spo2;
  bool    isMoving;
  bool    isInDangerousPosition;
  bool    bloodClotRisk;
  int     riskLevel;            // 0=safe, 1=low, 2=medium, 3=high, 4=critical
  unsigned long lastMovementTime;
  unsigned long lastPositionChange;
  unsigned long lastExerciseTime;
  unsigned long lastDataSent;
  int     exerciseCount;
  bool    emergencyTriggered;
  String  positionLabel;        // "Supine", "Left Side", "Right Side", "Prone"
};

PatientState patient;

// SpO2 algorithm buffers
uint32_t irBuffer[100];
uint32_t redBuffer[100];
int32_t  bufferLength = 100;
int32_t  spo2Value;
int8_t   validSPO2;
int32_t  heartRateValue;
int8_t   validHeartRate;

// Timing
unsigned long previousMillis = 0;
unsigned long sensorMillis   = 0;
unsigned long alertMillis    = 0;
bool          cameraReady    = false;

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== PATIENT MONITORING SYSTEM STARTING ===");

  // GPIO Init
  initGPIO();

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);

  // WiFi
  connectWiFi();

  // NTP Time Sync
  configTime(5 * 3600 + 1800, 0, "pool.ntp.org");  // IST UTC+5:30

  // IMU
  initIMU();

  // Pulse Oximeter
  initPulseOx();

  // Servos
  initServos();

  // Camera
  cameraReady = initCamera();

  // Load saved state
  prefs.begin("patient", false);
  patient.exerciseCount     = prefs.getInt("exCount", 0);
  patient.lastMovementTime  = millis();
  patient.lastPositionChange= millis();
  patient.lastExerciseTime  = millis();
  patient.riskLevel         = 0;

  // Startup beep
  startupChime();

  Serial.println("=== SYSTEM READY ===");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  unsigned long now = millis();

  // Read sensors every 1 second
  if (now - sensorMillis >= 1000) {
    sensorMillis = now;
    readIMU();
    assessMovement();
    assessPosition();
    calculateRiskLevel();
  }

  // Read SpO2/HR every 5 seconds
  static unsigned long spo2Millis = 0;
  if (now - spo2Millis >= 5000) {
    spo2Millis = now;
    readPulseOx();
  }

  // Send data to server every 30 seconds
  if (now - patient.lastDataSent >= 30000) {
    patient.lastDataSent = now;
    sendDataToServer();
  }

  // Check if passive exercise needed
  if (now - patient.lastExerciseTime >= POSITION_CHANGE_INTERVAL) {
    triggerPassiveExercise();
    patient.lastExerciseTime = now;
  }

  // Check immobility alert
  checkImmobilityAlert(now);

  // Handle alerts
  handleAlerts();

  // Emergency button
  if (digitalRead(EMERGENCY_BTN) == LOW) {
    triggerEmergency();
    delay(1000);
  }

  delay(10);
}

// ============================================================
// GPIO INITIALIZATION
// ============================================================
void initGPIO() {
  pinMode(BUZZER_PIN,    OUTPUT);
  pinMode(LED_RED,       OUTPUT);
  pinMode(LED_GREEN,     OUTPUT);
  pinMode(LED_BLUE,      OUTPUT);
  pinMode(VIBRATION_PIN, OUTPUT);
  pinMode(EMERGENCY_BTN, INPUT_PULLUP);

  // All off
  digitalWrite(LED_RED,   LOW);
  digitalWrite(LED_GREEN, HIGH);  // Green = system ok
  digitalWrite(LED_BLUE,  LOW);
  digitalWrite(BUZZER_PIN,    LOW);
  digitalWrite(VIBRATION_PIN, LOW);
}

// ============================================================
// WIFI
// ============================================================
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());
    setLED(0, 0, 1);  // Blue = WiFi ok
  } else {
    Serial.println("\nWiFi FAILED - Running in offline mode");
    setLED(1, 0, 0);  // Red = no WiFi
  }
}

// ============================================================
// IMU INIT & READ
// ============================================================
void initIMU() {
  imu.initialize();
  if (imu.testConnection()) {
    Serial.println("MPU6050 connected");
    imu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
    imu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);
    // Calibrate offsets (patient lying still)
    imu.setXAccelOffset(-1869);
    imu.setYAccelOffset(-1030);
    imu.setZAccelOffset(1133);
    imu.setXGyroOffset(58);
    imu.setYGyroOffset(-29);
    imu.setZGyroOffset(17);
  } else {
    Serial.println("MPU6050 FAILED!");
  }
}

void readIMU() {
  int16_t ax, ay, az, gx, gy, gz;
  imu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  patient.accelX = ax / 16384.0;
  patient.accelY = ay / 16384.0;
  patient.accelZ = az / 16384.0;
  patient.gyroX  = gx / 131.0;
  patient.gyroY  = gy / 131.0;
  patient.gyroZ  = gz / 131.0;

  // Tilt angle from accelerometer
  patient.tiltAngle = atan2(patient.accelY,
                             sqrt(patient.accelX * patient.accelX +
                                  patient.accelZ * patient.accelZ)) * 180.0 / PI;
  patient.rollAngle = atan2(patient.accelX,
                             sqrt(patient.accelY * patient.accelY +
                                  patient.accelZ * patient.accelZ)) * 180.0 / PI;
}

// ============================================================
// MOVEMENT DETECTION
// ============================================================
void assessMovement() {
  static float prevAx = 0, prevAy = 0, prevAz = 0;
  float delta = abs(patient.accelX - prevAx)
              + abs(patient.accelY - prevAy)
              + abs(patient.accelZ - prevAz);

  patient.isMoving = (delta > ACCEL_MOVEMENT_THRESHOLD);

  if (patient.isMoving) {
    patient.lastMovementTime = millis();
  }

  prevAx = patient.accelX;
  prevAy = patient.accelY;
  prevAz = patient.accelZ;
}

// ============================================================
// POSITION ASSESSMENT
// ============================================================
void assessPosition() {
  float tilt = patient.tiltAngle;
  float roll = patient.rollAngle;

  if (abs(tilt) < 15 && abs(roll) < 15) {
    patient.positionLabel = "Supine";
    patient.isInDangerousPosition = false;
  } else if (roll > 30) {
    patient.positionLabel = "Right Side";
    patient.isInDangerousPosition = false;
  } else if (roll < -30) {
    patient.positionLabel = "Left Side";
    patient.isInDangerousPosition = false;
  } else if (abs(tilt) > 60) {
    patient.positionLabel = "Prone";
    patient.isInDangerousPosition = true;  // Prone can be risky
  } else {
    patient.positionLabel = "Transitioning";
    patient.isInDangerousPosition = false;
  }

  // Dangerous angle detection
  if (abs(tilt) > TILT_THRESHOLD || abs(roll) > TILT_THRESHOLD + 15) {
    patient.isInDangerousPosition = true;
  }
}

// ============================================================
// RISK LEVEL CALCULATION
// ============================================================
void calculateRiskLevel() {
  unsigned long immobileTime = millis() - patient.lastMovementTime;
  int score = 0;

  // Immobility score (biggest factor)
  if (immobileTime > HIGH_RISK_IMMOBILITY)       score += 40;
  else if (immobileTime > IMMOBILITY_ALERT_THRESHOLD) score += 25;
  else if (immobileTime > 600000)                score += 10;  // 10 min

  // SpO2 score
  if (patient.spo2 > 0 && patient.spo2 < SPO2_CRITICAL)   score += 30;
  else if (patient.spo2 > 0 && patient.spo2 < SPO2_WARNING) score += 15;

  // Heart rate score
  if (patient.heartRate > 0) {
    if (patient.heartRate < HEART_RATE_LOW || patient.heartRate > HEART_RATE_HIGH) score += 20;
    else if (patient.heartRate < 55 || patient.heartRate > 110) score += 10;
  }

  // Position score
  if (patient.isInDangerousPosition) score += 10;

  // Map score to risk level
  if (score >= 70)       patient.riskLevel = 4;  // Critical
  else if (score >= 50)  patient.riskLevel = 3;  // High
  else if (score >= 30)  patient.riskLevel = 2;  // Medium
  else if (score >= 10)  patient.riskLevel = 1;  // Low
  else                   patient.riskLevel = 0;  // Safe

  patient.bloodClotRisk = (patient.riskLevel >= 3);
}

// ============================================================
// PULSE OXIMETER INIT & READ
// ============================================================
void initPulseOx() {
  if (!pulseOximeter.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 FAILED!");
    return;
  }
  Serial.println("MAX30102 connected");
  pulseOximeter.setup();
  pulseOximeter.setPulseAmplitudeRed(0x0A);
  pulseOximeter.setPulseAmplitudeGreen(0);
}

void readPulseOx() {
  // Collect 100 samples
  for (byte i = 0; i < bufferLength; i++) {
    while (!pulseOximeter.available()) pulseOximeter.check();
    redBuffer[i] = pulseOximeter.getRed();
    irBuffer[i]  = pulseOximeter.getIR();
    pulseOximeter.nextSample();
  }
  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, bufferLength, redBuffer,
    &spo2Value, &validSPO2,
    &heartRateValue, &validHeartRate
  );
  if (validSPO2)    patient.spo2      = spo2Value;
  if (validHeartRate) patient.heartRate = heartRateValue;
}

// ============================================================
// SERVO / PASSIVE EXERCISE
// ============================================================
void initServos() {
  servoLeftLeg.attach(SERVO_LEFT_LEG);
  servoRightLeg.attach(SERVO_RIGHT_LEG);
  servoLeftAnkle.attach(SERVO_LEFT_ANKLE);
  servoRightAnkle.attach(SERVO_RIGHT_ANKLE);
  // Center position
  servoLeftLeg.write(90);
  servoRightLeg.write(90);
  servoLeftAnkle.write(90);
  servoRightAnkle.write(90);
}

void triggerPassiveExercise() {
  Serial.println(">> Starting passive exercise routine");
  setLED(0, 1, 1);  // Cyan

  // Notify server
  notifyServer("EXERCISE_START", "Passive leg exercise initiated");

  unsigned long start = millis();
  int cycles = 0;

  while (millis() - start < EXERCISE_DURATION) {
    // Ankle circles
    for (int angle = 60; angle <= 120; angle += 5) {
      servoLeftAnkle.write(angle);
      servoRightAnkle.write(180 - angle);
      delay(50);
    }
    for (int angle = 120; angle >= 60; angle -= 5) {
      servoLeftAnkle.write(angle);
      servoRightAnkle.write(180 - angle);
      delay(50);
    }

    // Leg raises
    for (int angle = 90; angle <= 130; angle += 3) {
      servoLeftLeg.write(angle);
      delay(30);
    }
    for (int angle = 130; angle >= 90; angle -= 3) {
      servoLeftLeg.write(angle);
      delay(30);
    }
    delay(500);
    for (int angle = 90; angle <= 130; angle += 3) {
      servoRightLeg.write(angle);
      delay(30);
    }
    for (int angle = 130; angle >= 90; angle -= 3) {
      servoRightLeg.write(angle);
      delay(30);
    }
    cycles++;
    delay(300);
  }

  // Return to neutral
  servoLeftLeg.write(90);
  servoRightLeg.write(90);
  servoLeftAnkle.write(90);
  servoRightAnkle.write(90);

  patient.exerciseCount++;
  prefs.putInt("exCount", patient.exerciseCount);

  notifyServer("EXERCISE_DONE",
    "Completed " + String(cycles) + " cycles. Total today: " + String(patient.exerciseCount));

  setLED(0, 1, 0);  // Green
  Serial.println(">> Exercise complete. Cycles: " + String(cycles));
}

// ============================================================
// IMMOBILITY ALERTS
// ============================================================
void checkImmobilityAlert(unsigned long now) {
  unsigned long immobileTime = now - patient.lastMovementTime;

  if (immobileTime > HIGH_RISK_IMMOBILITY && !patient.bloodClotRisk) {
    // Critical - send alert + capture image
    sendAlert("CRITICAL_IMMOBILITY",
      "Patient immobile for " + String(immobileTime / 60000) + " minutes. HIGH CLOT RISK!");
    if (cameraReady) captureAndSendImage("critical_immobility");
    patient.bloodClotRisk = true;
  } else if (immobileTime > IMMOBILITY_ALERT_THRESHOLD &&
             immobileTime < HIGH_RISK_IMMOBILITY &&
             !patient.bloodClotRisk) {
    sendAlert("WARNING_IMMOBILITY",
      "Patient immobile for " + String(immobileTime / 60000) + " minutes.");
  }
}

// ============================================================
// ALERTS & LED
// ============================================================
void handleAlerts() {
  switch (patient.riskLevel) {
    case 0:
      setLED(0, 1, 0);   // Green
      break;
    case 1:
      setLED(1, 1, 0);   // Yellow
      break;
    case 2:
      setLED(1, 0, 0);   // Red
      pulseBeep(1, 200);
      break;
    case 3:
      setLED(1, 0, 0);
      pulseBeep(3, 300);
      vibrateAlert(300);
      break;
    case 4:
      setLED(1, 0, 0);
      pulseBeep(5, 500);
      vibrateAlert(1000);
      break;
  }
}

void setLED(bool r, bool g, bool b) {
  digitalWrite(LED_RED,   r ? HIGH : LOW);
  digitalWrite(LED_GREEN, g ? HIGH : LOW);
  digitalWrite(LED_BLUE,  b ? HIGH : LOW);
}

void pulseBeep(int times, int duration) {
  static unsigned long lastBeep = 0;
  if (millis() - lastBeep < 10000) return;  // Throttle
  lastBeep = millis();
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

void vibrateAlert(int duration) {
  digitalWrite(VIBRATION_PIN, HIGH);
  delay(duration);
  digitalWrite(VIBRATION_PIN, LOW);
}

void startupChime() {
  int notes[] = {262, 330, 392, 523};
  for (int n : notes) {
    tone(BUZZER_PIN, n, 150);
    delay(180);
  }
  noTone(BUZZER_PIN);
}

// ============================================================
// CAMERA INIT & CAPTURE
// ============================================================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sscb_sda  = SIOD_GPIO_NUM;
  config.pin_sscb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = RESET_GPIO_NUM;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_JPEG;
  config.frame_size    = FRAMESIZE_VGA;
  config.jpeg_quality  = 12;
  config.fb_count      = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED: 0x%x\n", err);
    return false;
  }
  Serial.println("Camera ready");
  return true;
}

void captureAndSendImage(String reason) {
  if (!cameraReady) return;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { Serial.println("Camera capture FAILED"); return; }

  String encoded = base64::encode(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  // Send to server
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(String(SERVER_URL) + "/api/image");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-ID", DEVICE_ID);

    StaticJsonDocument<1024> doc;
    doc["deviceId"] = DEVICE_ID;
    doc["reason"]   = reason;
    doc["timestamp"] = getTimestamp();
    doc["image"]    = encoded.substring(0, 500);  // Truncate for demo; use chunked in production

    String payload;
    serializeJson(doc, payload);
    http.POST(payload);
    http.end();
    Serial.println("Image sent: " + reason);
  }
}

// ============================================================
// SERVER COMMUNICATION
// ============================================================
void sendDataToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return;
  }

  HTTPClient http;
  http.begin(String(SERVER_URL) + "/api/telemetry");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-ID", DEVICE_ID);
  http.setTimeout(5000);

  StaticJsonDocument<512> doc;
  doc["deviceId"]         = DEVICE_ID;
  doc["patientName"]      = PATIENT_NAME;
  doc["timestamp"]        = getTimestamp();
  doc["accelX"]           = patient.accelX;
  doc["accelY"]           = patient.accelY;
  doc["accelZ"]           = patient.accelZ;
  doc["tiltAngle"]        = patient.tiltAngle;
  doc["rollAngle"]        = patient.rollAngle;
  doc["heartRate"]        = patient.heartRate;
  doc["spo2"]             = patient.spo2;
  doc["isMoving"]         = patient.isMoving;
  doc["positionLabel"]    = patient.positionLabel;
  doc["riskLevel"]        = patient.riskLevel;
  doc["bloodClotRisk"]    = patient.bloodClotRisk;
  doc["immobileMinutes"]  = (millis() - patient.lastMovementTime) / 60000;
  doc["exerciseCount"]    = patient.exerciseCount;
  doc["dangerousPosition"]= patient.isInDangerousPosition;

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  Serial.println("Telemetry sent. Response: " + String(httpCode));
  http.end();
}

void sendAlert(String alertType, String message) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(SERVER_URL) + "/api/alert");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-ID", DEVICE_ID);

  StaticJsonDocument<256> doc;
  doc["deviceId"]  = DEVICE_ID;
  doc["alertType"] = alertType;
  doc["message"]   = message;
  doc["riskLevel"] = patient.riskLevel;
  doc["timestamp"] = getTimestamp();
  doc["spo2"]      = patient.spo2;
  doc["heartRate"] = patient.heartRate;

  String payload;
  serializeJson(doc, payload);
  http.POST(payload);
  http.end();
  Serial.println("ALERT SENT: " + alertType + " | " + message);
}

void notifyServer(String event, String details) {
  sendAlert(event, details);
}

// ============================================================
// EMERGENCY
// ============================================================
void triggerEmergency() {
  if (patient.emergencyTriggered) return;
  patient.emergencyTriggered = true;

  Serial.println("!!! EMERGENCY TRIGGERED !!!");
  setLED(1, 0, 0);

  // Loud alarm
  for (int i = 0; i < 10; i++) {
    tone(BUZZER_PIN, 1000, 200);
    delay(300);
    tone(BUZZER_PIN, 1500, 200);
    delay(300);
  }
  noTone(BUZZER_PIN);

  if (cameraReady) captureAndSendImage("EMERGENCY");

  sendAlert("EMERGENCY",
    "EMERGENCY BUTTON PRESSED! Immediate attention required!");

  delay(3000);
  patient.emergencyTriggered = false;
}

// ============================================================
// UTILITY
// ============================================================
String getTimestamp() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(buf);
}
