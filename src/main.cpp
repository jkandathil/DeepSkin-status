#include "driver/gpio.h"
#include "time.h" // Native Time Lib
#include <Arduino.h>
#include <HTTPClient.h>
#include <SensorQMI8658.hpp>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <Wire.h>


// --- Configuration ---
#define IMU_SDA 11
#define IMU_SCL 10
#define BAT_ADC_PIN 1

// --- Hardware Pins ---
#define PWR_EN_PIN GPIO_NUM_35

// --- Google Script URL ---
String GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/"
                           "AKfycbxs8o2ONK8WsV4lxqd39VjOM88-"
                           "wRPhfB5zPzuAJ69wJhtjCfBIoIiuZAvv66DAW8rUQQ/exec";

SensorQMI8658 qmi;
IMUdata acc;
IMUdata gyr;

// --- Globals ---
unsigned long lastSummaryTime = 0;
float maxG_in_interval = 0.0;
bool potentialFallDetected = false;
uint32_t stepCount = 0;
String deviceName = "Device_Watchc01";

// --- Batching Config ---
String dataBuffer = "";
int bufferCount = 0;
const int BATCH_SIZE = 30;

void latchPower() {
  gpio_hold_dis(PWR_EN_PIN);
  pinMode((int)PWR_EN_PIN, OUTPUT);
  digitalWrite((int)PWR_EN_PIN, HIGH);
  gpio_hold_en(PWR_EN_PIN);
}

int getBatteryPercentage() {
  uint32_t raw = analogRead(BAT_ADC_PIN);
  float voltage = (raw / 4095.0) * 3.3 * 3.0;
  int percentage = (int)((voltage - 3.3) / (4.2 - 3.3) * 100);
  return constrain(percentage, 0, 100);
}

void flushDataToGoogle() {
  if (WiFi.status() == WL_CONNECTED && dataBuffer.length() > 0) {
    HTTPClient http;
    http.begin(GOOGLE_SCRIPT_URL);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Content-Type", "text/csv");

    Serial.println("Uploading Batch...");
    int httpCode = http.POST(dataBuffer);

    if (httpCode > 0) {
      Serial.printf("Batch Upload Success: %d\n", httpCode);
      dataBuffer = "";
      bufferCount = 0;
    } else {
      Serial.printf("Batch Upload Fail: %s\n",
                    http.errorToString(httpCode).c_str());
      dataBuffer = "";
      bufferCount = 0;
    }
    http.end();
  }
}

void setup() {
  latchPower();

  Serial.begin(115200);
  delay(2000);
  Serial.println("--- SYSTEM STARTED (Multi-Device Log) ---");

  pinMode(BAT_ADC_PIN, INPUT);

  WiFiManager wm;
  bool res = wm.autoConnect("DeepSkinWatch");

  if (!res)
    Serial.println("Failed to connect");
  else
    Serial.println("WiFi Connected!");

  // --- Time Sync (Auto DST) ---
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();

  if (!Wire.begin(IMU_SDA, IMU_SCL)) {
    Serial.println("I2C Fail");
  }

  if (!qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IMU_SDA, IMU_SCL)) {
    if (!qmi.begin(Wire, QMI8658_H_SLAVE_ADDRESS, IMU_SDA, IMU_SCL)) {
    }
  }
  Serial.println("QMI8658 Ready");

  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_8G,
                          SensorQMI8658::ACC_ODR_125Hz,
                          SensorQMI8658::LPF_MODE_0);
  qmi.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                      SensorQMI8658::GYR_ODR_112_1Hz,
                      SensorQMI8658::LPF_MODE_3);
  qmi.configPedometer(0x0064, 0x00CC, 0x0066, 0x0050, 0x0014, 0x000A, 0x0000,
                      0x04);
  qmi.enablePedometer();
  qmi.enableAccelerometer();
  qmi.enableGyroscope();
}

String getLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time_Syncing";
  }
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S",
           &timeinfo);
  return String(timeStringBuff);
}

void loop() {
  digitalWrite((int)PWR_EN_PIN, HIGH);

  if (qmi.getDataReady()) {
    if (qmi.getAccelerometer(acc.x, acc.y, acc.z)) {
      float totalG = sqrt(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
      if (totalG > 100.0)
        totalG /= 1000.0;
      if (totalG > maxG_in_interval)
        maxG_in_interval = totalG;
      if (totalG > 2.5)
        potentialFallDetected = true;
    }
    qmi.getGyroscope(gyr.x, gyr.y, gyr.z);
  }

  if (millis() - lastSummaryTime >= 1000) {
    stepCount = qmi.getPedometerCounter();
    int bat = getBatteryPercentage();
    String status = (maxG_in_interval > 1.1) ? "MOVING" : "RESTING";

    String timestamp = getLocalTime();

    Serial.printf("[%d/%d] %s Status: %s | Bat: %d%%\n", bufferCount + 1,
                  BATCH_SIZE, timestamp.c_str(), status.c_str(), bat);

    String line = timestamp + "," + deviceName + "," + status + "," +
                  String(bat) + "," + String(stepCount) + "," +
                  String(maxG_in_interval, 2) + "," +
                  String(potentialFallDetected) + "\n";

    dataBuffer += line;
    bufferCount++;

    if (bufferCount >= BATCH_SIZE) {
      flushDataToGoogle();
    }

    potentialFallDetected = false;
    maxG_in_interval = 0.0;
    lastSummaryTime = millis();
  }

  delay(15);
}
