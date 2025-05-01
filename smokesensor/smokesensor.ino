#include <WiFi.h>
#include <FirebaseESP32.h>
#include <time.h>

// Wi-Fi credentials
#define WIFI_SSID "Hidden"
//#define WIFI_PASSWORD "C@bigonUb#s2025" // Use for Wi-Fi password if required

// Firebase credentials
#define FIREBASE_HOST "https://geofencing-2fcd0-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "OvB1mf3RlE18X1q4yehMLjbopMQjWgHuv1Cxa6JM"

// NTP Server details
#define NTP_SERVER "time.google.com"
#define UTC_OFFSET 28800  // UTC+8 for Philippines (8 * 60 * 60)
#define DST_OFFSET 0      // No DST in Philippines

// Device Information
#define DEVICE_TYPE "SmokeSensor"
#define DEVICE_ID "SS_0000"

// Firebase objects
FirebaseData firebaseData;
FirebaseAuth auth; 
FirebaseConfig config;

// MQ Sensor Pin
#define MQ_SENSOR_PIN 32  // Analog pin for ESP32

int currentThreshold = 0;
String currentOperator = "";
int smokeValue = 0;

String get12HourTime(struct tm *timeinfo) {
  char timeStr[50];
  int hour = timeinfo->tm_hour;
  const char* ampm = (hour >= 12) ? "PM" : "AM";
  
  // Convert to 12-hour format
  if (hour > 12) {
    hour -= 12;
  } else if (hour == 0) {
    hour = 12;
  }
  
  sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d %s", 
          timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
          hour, timeinfo->tm_min, timeinfo->tm_sec, ampm);
  return String(timeStr);
}

bool syncTime() {
  Serial.println("Synchronizing time with Google NTP server...");
  configTime(UTC_OFFSET, DST_OFFSET, NTP_SERVER);
  
  // Wait for time to be set
  int timeout = 0;
  while (time(nullptr) < 1000000000 && timeout < 10) {
    Serial.print(".");
    delay(1000);
    timeout++;
  }
  Serial.println();
  
  // Verify time
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  
  // Check if time is reasonable (between 2024 and 2025)
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
    if (Firebase.getString(firebaseData, devicePath + "/deviceType")) {
      // Device exists, read current values
      if (Firebase.getInt(firebaseData, devicePath + "/Threshold")) {
        currentThreshold = firebaseData.intData();
      }
      if (Firebase.getString(firebaseData, devicePath + "/Operator")) {
        currentOperator = firebaseData.stringData();
      }
    } else {
      // New device, initialize with default values
      if (Firebase.setString(firebaseData, devicePath + "/deviceType", DEVICE_TYPE)) {
        if (Firebase.setInt(firebaseData, devicePath + "/Threshold", 1000)) {
          if (Firebase.setString(firebaseData, devicePath + "/Operator", ">")) {
            currentThreshold = 1000;
            currentOperator = ">";
          }
        }
      }
    }
    
    // Update online status and smoke level
    if (Firebase.setBool(firebaseData, devicePath + "/isOnline", true)) {
      if (Firebase.setInt(firebaseData, devicePath + "/smokeLevel", 0)) {
        if (Firebase.setString(firebaseData, devicePath + "/Environment", "No Smoke Detected")) {
          Serial.println("Device data initialized successfully");
          Serial.print("Current Threshold: ");
          Serial.println(currentThreshold);
          Serial.print("Current Operator: ");
          Serial.println(currentOperator);
          return;
        }
      }
    }
    Serial.println("Failed to initialize device data");
    Serial.println(firebaseData.errorReason());
  }
}

void updateOnlineStatus(bool isOnline) {
  if (Firebase.ready()) {
    // Get current timestamp
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Update device information with timestamp
    String devicePath = "/IOTs/" + String(DEVICE_ID);
    
    if (Firebase.setBool(firebaseData, devicePath + "/isOnline", isOnline)) {
      if (Firebase.setString(firebaseData, devicePath + "/lastStatusUpdate", get12HourTime(&timeinfo))) {
        Serial.print("Status updated to: ");
        Serial.print(isOnline ? "Online" : "Offline");
        Serial.print(" at ");
        Serial.println(get12HourTime(&timeinfo));
      }
    } else {
      Serial.println("Failed to update status");
      Serial.println(firebaseData.errorReason());
    }
  }
}

void updateSmokeLevel(int level) {
  if (Firebase.ready()) {
    String devicePath = "/IOTs/" + String(DEVICE_ID);
    
    if (Firebase.setInt(firebaseData, devicePath + "/smokeLevel", level)) {
      // Update smoke detection status based on current operator
      bool isSmokeDetected = false;
      if (currentOperator == ">") {
        isSmokeDetected = (level > currentThreshold);
      } else if (currentOperator == "<") {
        isSmokeDetected = (level < currentThreshold);
      }
      
      String status = isSmokeDetected ? "Smoke Detected" : "No Smoke Detected";
      
      if (Firebase.setString(firebaseData, devicePath + "/Environment", status)) {
        Serial.print("Smoke level updated to: ");
        Serial.println(level);
        if (isSmokeDetected) {
          Serial.println("WARNING: Smoke detected! Level " + currentOperator + " threshold!");
        }
      }
    } else {
      Serial.println("Failed to update smoke level");
      Serial.println(firebaseData.errorReason());
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize MQ sensor pin
  pinMode(MQ_SENSOR_PIN, INPUT);

  // Wi-Fi connection
  Serial.println("Starting Wi-Fi connection...");
  WiFi.begin(WIFI_SSID);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > 15000) {  
      Serial.println("Failed to connect to Wi-Fi.");
      Serial.println("Wi-Fi Status: " + String(WiFi.status()));
      break;  
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());

    // Synchronize time with Google NTP server
    if (!syncTime()) {
      Serial.println("Warning: Time synchronization failed, device will continue with unsynchronized time");
    }
    
    config.database_url = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    Firebase.begin(&config, &auth); 
    Firebase.reconnectWiFi(true);

    if (!Firebase.ready()) {
      Serial.println("Failed to connect to Firebase.");
      Serial.println(firebaseData.errorReason());
      return;
    }
    Serial.println("Connected to Firebase");
    
    // Initialize device data first
    initializeDeviceData();
    
    // Then set online status
    updateOnlineStatus(true);
  } else {
    Serial.println("Wi-Fi connection failed, operating in offline mode.");
    // Try to set online status to false if Firebase is still accessible
    updateOnlineStatus(false);
  }
}

void loop() {
  static unsigned long lastStatusCheck = 0;
  static unsigned long lastTimeSync = 0;
  static unsigned long lastSmokeCheck = 0;
  static unsigned long lastThresholdCheck = 0;
  const unsigned long STATUS_CHECK_INTERVAL = 5000;    // Check every 5 seconds
  const unsigned long TIME_SYNC_INTERVAL = 3600000;   // Sync time every hour
  const unsigned long SMOKE_CHECK_INTERVAL = 1000;    // Check smoke level every second
  const unsigned long THRESHOLD_CHECK_INTERVAL = 5000; // Check threshold every 5 seconds
  
  // Check WiFi status periodically and update Firebase
  if (millis() - lastStatusCheck >= STATUS_CHECK_INTERVAL) {
    lastStatusCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      updateOnlineStatus(false);
    } else if (Firebase.ready()) {
      updateOnlineStatus(true);
    }
  }

  // Periodically sync time
  if (millis() - lastTimeSync >= TIME_SYNC_INTERVAL) {
    lastTimeSync = millis();
    if (WiFi.status() == WL_CONNECTED) {
      syncTime();
    }
  }

  // Check for threshold and operator updates from Firebase
  if (millis() - lastThresholdCheck >= THRESHOLD_CHECK_INTERVAL) {
    lastThresholdCheck = millis();
    
    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
      String devicePath = "/IOTs/" + String(DEVICE_ID);
      
      // Check for threshold updates
      if (Firebase.getInt(firebaseData, devicePath + "/Threshold")) {
        int newThreshold = firebaseData.intData();
        if (newThreshold != currentThreshold) {
          currentThreshold = newThreshold;
          Serial.print("Threshold updated to: ");
          Serial.println(currentThreshold);
        }
      }
      
      // Check for operator updates
      if (Firebase.getString(firebaseData, devicePath + "/Operator")) {
        String newOperator = firebaseData.stringData();
        if (newOperator != currentOperator && (newOperator == ">" || newOperator == "<")) {
          currentOperator = newOperator;
          Serial.print("Operator updated to: ");
          Serial.println(currentOperator);
        }
      }
    }
  }

  // Read and update smoke sensor value
  if (millis() - lastSmokeCheck >= SMOKE_CHECK_INTERVAL) {
    lastSmokeCheck = millis();
    
    // Read the analog value from MQ sensor
    smokeValue = analogRead(MQ_SENSOR_PIN);
    
    // Update Firebase with new smoke level
    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
      updateSmokeLevel(smokeValue);
    }
    
    // Print smoke level to Serial for debugging
    Serial.print("Smoke Level: ");
    Serial.println(smokeValue);
  }
}