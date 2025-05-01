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

// Firebase objects
FirebaseData firebaseData;
FirebaseAuth auth; 
FirebaseConfig config;

// Function to parse timestamp string to time_t
time_t parseTimestamp(String timestamp) {
  int year, month, day, hour, minute, second;
  char ampm[3];
  sscanf(timestamp.c_str(), "%d-%d-%d %d:%d:%d %s", 
         &year, &month, &day, &hour, &minute, &second, ampm);
  
  // Adjust hour for PM
  if (strcmp(ampm, "PM") == 0 && hour != 12) {
    hour += 12;
  }
  // Adjust for AM 12
  else if (strcmp(ampm, "AM") == 0 && hour == 12) {
    hour = 0;
  }
  
  struct tm timeinfo = {0};
  timeinfo.tm_year = year - 1900;
  timeinfo.tm_mon = month - 1;
  timeinfo.tm_mday = day;
  timeinfo.tm_hour = hour;
  timeinfo.tm_min = minute;
  timeinfo.tm_sec = second;
  
  return mktime(&timeinfo);
}

void printDeviceStatus(String deviceId, String lastUpdate, bool isOnline, double timeDiff) {
  Serial.println("\n----------------------------------------");
  Serial.printf("Device ID: %s\n", deviceId.c_str());
  Serial.printf("Last Update: %s\n", lastUpdate.c_str());
  Serial.printf("Online Status: %s\n", isOnline ? "Online" : "Offline");
  Serial.printf("Time Since Last Update: %.0f seconds\n", timeDiff);
  Serial.println("----------------------------------------");
}

void checkDeviceStatus(String deviceId) {
  String devicePath = "/IOTs/" + deviceId;
  bool currentOnlineStatus = false;
  String lastUpdate = "";
  
  // Get current online status
  if (Firebase.getBool(firebaseData, devicePath + "/isOnline")) {
    currentOnlineStatus = firebaseData.boolData();
  }
  
  // Get last status update
  if (Firebase.getString(firebaseData, devicePath + "/lastStatusUpdate")) {
    lastUpdate = firebaseData.stringData();
    
    // Get current time
    time_t now;
    time(&now);
    
    // Parse last update time
    time_t lastUpdateTime = parseTimestamp(lastUpdate);
    
    // Calculate time difference in seconds
    double timeDiff = difftime(now, lastUpdateTime);
    
    // Print current status
    printDeviceStatus(deviceId, lastUpdate, currentOnlineStatus, timeDiff);
    
    // If last update was more than 20 seconds ago, set device as offline
    if (timeDiff > 20) {
      if (currentOnlineStatus) {  // Only update if currently marked as online
        if (Firebase.setBool(firebaseData, devicePath + "/isOnline", false)) {
          Serial.printf(">>> STATUS CHANGE: Device %s marked as OFFLINE (No updates in %.0f seconds)\n", 
                       deviceId.c_str(), timeDiff);
        }
      }
    }
  }
}

std::vector<String> scanDeviceIds() {
  std::vector<String> deviceIds;
  
  if (Firebase.getJSON(firebaseData, "/IOTs")) {
    FirebaseJson json = firebaseData.jsonObject();
    size_t len = json.iteratorBegin();
    String key, value;
    int type = 0;
    
    for (size_t i = 0; i < len; i++) {
      json.iteratorGet(i, type, key, value);
      if (type == FirebaseJson::JSON_OBJECT) {
        deviceIds.push_back(key);
      }
    }
    json.iteratorEnd();
  }
  
  return deviceIds;
}

void setup() {
  Serial.begin(115200);

  // Wi-Fi connection
  Serial.println("\n=== Status Update Monitor Starting ===");
  Serial.println("Starting Wi-Fi connection...");
  WiFi.begin(WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  // Configure time
  Serial.println("Configuring time...");
  configTime(UTC_OFFSET, DST_OFFSET, NTP_SERVER);
  
  // Initialize Firebase
  Serial.println("Connecting to Firebase...");
  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth); 
  Firebase.reconnectWiFi(true);
  
  Serial.println("Monitor ready! Checking device statuses...\n");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
    // Start new monitoring cycle
    Serial.println("\n=== Starting Device Status Check ===");
    
    // First: Scan for all device IDs
    std::vector<String> deviceIds = scanDeviceIds();
    
    // Print found devices
    Serial.printf("\nFound %d devices:\n", deviceIds.size());
    for (const String& deviceId : deviceIds) {
      Serial.printf("- %s\n", deviceId.c_str());
    }
    
    // Second: Check status for each device
    Serial.println("\nChecking status for each device:");
    for (const String& deviceId : deviceIds) {
      checkDeviceStatus(deviceId);
    }
    
    Serial.println("\n=== Device Status Check Complete ===");
    delay(5000); // Check every 5 seconds
  } else {
    Serial.println("Connection lost. Waiting for reconnection...");
    delay(1000);
  }
}