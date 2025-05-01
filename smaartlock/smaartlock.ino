#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <time.h>

// Wi-Fi credentials
#define WIFI_SSID "Hidden"
// #define WIFI_PASSWORD "C@bigonUb#s2025" // Use for Wi-Fi password if required

// Firebase credentials
#define FIREBASE_HOST "https://geofencing-2fcd0-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "OvB1mf3RlE18X1q4yehMLjbopMQjWgHuv1Cxa6JM"

// NTP Server details
#define NTP_SERVER "time.google.com"
#define UTC_OFFSET 28800  // UTC+8 for Philippines (8 * 60 * 60)
#define DST_OFFSET 0      // No DST in Philippines

// Device Information
#define DEVICE_TYPE "SmartLock"
#define DEVICE_ID "SL_0000"

// Firebase objects
FirebaseData firebaseData;
FirebaseAuth auth; 
FirebaseConfig config;

// Solenoid Lock Pin
#define SOLENOID_PIN D5  

int previousValue = -1;

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
    
    // Initialize all device fields
    if (Firebase.setString(firebaseData, devicePath + "/deviceType", DEVICE_TYPE)) {
      if (Firebase.setInt(firebaseData, devicePath + "/Status", 0)) {
        if (Firebase.setBool(firebaseData, devicePath + "/isOnline", true)) {
          Serial.println("Device data initialized successfully");
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

void setup() {
  Serial.begin(115200);

  // Initialize solenoid pin
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW);  

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
  const unsigned long STATUS_CHECK_INTERVAL = 5000; // Check every 5 seconds
  const unsigned long TIME_SYNC_INTERVAL = 3600000; // Sync time every hour
  
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

  if (WiFi.status() == WL_CONNECTED) {
    String devicePath = "/IOTs/" + String(DEVICE_ID);
    if (Firebase.getInt(firebaseData, devicePath + "/Status")) {
      int currentValue = firebaseData.intData();

      if (currentValue != previousValue) {
        Serial.print("Firebase value: ");
        Serial.println(currentValue);

        if (currentValue == 1) {
          digitalWrite(SOLENOID_PIN, HIGH);
          Serial.println("LOCK LOCKED");
        } else if (currentValue == 0) {
          digitalWrite(SOLENOID_PIN, LOW);
          Serial.println("LOCK UNLOCKED");
        }

        previousValue = currentValue;
      }
    } else {
      Serial.println("Failed to read value from Firebase");
      Serial.println(firebaseData.errorReason());
    }
  } 
}