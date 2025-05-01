#include <WiFi.h>
#include <FirebaseESP32.h>
#include <time.h>

// Wi-Fi credentials
#define WIFI_SSID "Hidden"

// Firebase credentials
#define FIREBASE_HOST "https://geofencing-2fcd0-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "OvB1mf3RlE18X1q4yehMLjbopMQjWgHuv1Cxa6JM"

// NTP Server details
#define NTP_SERVER "time.google.com"
#define UTC_OFFSET 28800  // UTC+8 for Philippines
#define DST_OFFSET 0

// Device Information
#define DEVICE_TYPE "ShockSensor"
#define DEVICE_ID "SKS_0001"

// Firebase objects
FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// Shock Sensor Pin (digital)
#define SHOCK_SENSOR_PIN 32

String get12HourTime(struct tm *timeinfo) {
  char timeStr[50];
  int hour = timeinfo->tm_hour;
  const char* ampm = (hour >= 12) ? "PM" : "AM";

  if (hour > 12) hour -= 12;
  else if (hour == 0) hour = 12;

  sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d %s",
          timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
          hour, timeinfo->tm_min, timeinfo->tm_sec, ampm);
  return String(timeStr);
}

bool syncTime() {
  Serial.println("Synchronizing time with Google NTP server...");
  configTime(UTC_OFFSET, DST_OFFSET, NTP_SERVER);
  int timeout = 0;
  while (time(nullptr) < 1000000000 && timeout < 10) {
    Serial.print(".");
    delay(1000);
    timeout++;
  }
  Serial.println();
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year + 1900 >= 2024 && timeinfo.tm_year + 1900 <= 2025) {
    Serial.printf("Time synchronized successfully: %s\n", get12HourTime(&timeinfo).c_str());
    return true;
  }
  Serial.println("Failed to synchronize time");
  return false;
}

void initializeDeviceData() {
  if (Firebase.ready()) {
    String devicePath = "/IOTs/" + String(DEVICE_ID);

    // Check if device data exists
    if (!Firebase.getString(firebaseData, devicePath + "/deviceType")) {
      Firebase.setString(firebaseData, devicePath + "/deviceType", DEVICE_TYPE);
    }

    // Set initial values
    if (Firebase.setBool(firebaseData, devicePath + "/isOnline", true)) {
      if (Firebase.setString(firebaseData, devicePath + "/Readings", "No Shock Detected")) {
        Serial.println("Device data initialized successfully");
        return;
      }
    }
    Serial.println("Failed to initialize device data");
    Serial.println(firebaseData.errorReason());
  }
}

void updateOnlineStatus(bool isOnline) {
  if (Firebase.ready()) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    String devicePath = "/IOTs/" + String(DEVICE_ID);
    if (Firebase.setBool(firebaseData, devicePath + "/isOnline", isOnline)) {
      Firebase.setString(firebaseData, devicePath + "/lastStatusUpdate", get12HourTime(&timeinfo));
      Serial.print("Status updated to: ");
      Serial.println(isOnline ? "Online" : "Offline");
    } else {
      Serial.println("Failed to update status");
      Serial.println(firebaseData.errorReason());
    }
  }
}

void updateShockStatus(bool detected) {
  if (Firebase.ready()) {
    String status = detected ? "Shock Detected" : "No Shock Detected";
    String devicePath = "/IOTs/" + String(DEVICE_ID);
    if (Firebase.setString(firebaseData, devicePath + "/Readings", status)) {
      Serial.println("Shock status updated: " + status);
    } else {
      Serial.println("Failed to update shock status");
      Serial.println(firebaseData.errorReason());
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(SHOCK_SENSOR_PIN, INPUT);

  Serial.println("Starting Wi-Fi connection...");
  WiFi.begin(WIFI_SSID);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > 15000) {
      Serial.println("Failed to connect to Wi-Fi.");
      break;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());

    if (!syncTime()) {
      Serial.println("Warning: Time synchronization failed.");
    }

    config.database_url = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    if (!Firebase.ready()) {
      Serial.println("Failed to connect to Firebase.");
      return;
    }
    Serial.println("Connected to Firebase");

    initializeDeviceData();
    updateOnlineStatus(true);
  } else {
    Serial.println("Wi-Fi connection failed, offline mode.");
    updateOnlineStatus(false);
  }
}

void loop() {
  static unsigned long lastStatusCheck = 0;
  static unsigned long lastTimeSync = 0;
  static unsigned long lastShockCheck = 0;

  const unsigned long STATUS_INTERVAL = 5000;
  const unsigned long TIME_SYNC_INTERVAL = 3600000;
  const unsigned long SHOCK_CHECK_INTERVAL = 500;

  if (millis() - lastStatusCheck >= STATUS_INTERVAL) {
    lastStatusCheck = millis();
    updateOnlineStatus(WiFi.status() == WL_CONNECTED && Firebase.ready());
  }

  if (millis() - lastTimeSync >= TIME_SYNC_INTERVAL) {
    lastTimeSync = millis();
    if (WiFi.status() == WL_CONNECTED) {
      syncTime();
    }
  }

  if (millis() - lastShockCheck >= SHOCK_CHECK_INTERVAL) {
    lastShockCheck = millis();
    bool shockDetected = digitalRead(SHOCK_SENSOR_PIN) == HIGH;
    updateShockStatus(shockDetected);
    Serial.println("Shock Sensor Reading: " + String(shockDetected ? "HIGH (Detected)" : "LOW (No Shock)"));
  }
}
