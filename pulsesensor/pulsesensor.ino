// Pulse Sensor Heart Rate Monitor for NodeMCU ESP8266
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <time.h>

#define WIFI_SSID "Hidden"
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

// Define the pin where the pulse sensor is connected
const int pulsePin = A0;  // NodeMCU has only one analog pin (A0)

// Variables for heart rate calculation
int sensorValue = 0;
int threshold = 620;      // Lower threshold to detect more beats
unsigned long lastBeatTime = 0;
float beatsPerMinute = 0;
boolean beatDetected = false;
boolean hasValidReading = false;  // New flag to track valid readings
unsigned long lastUpdateTime = 0;
const unsigned long UPDATE_INTERVAL = 3000;

// Add variables for signal analysis
int signalMin = 1023;
int signalMax = 0;

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

void setup() {
  Serial.begin(115200);
  Serial.println("\nPulse Sensor Heart Rate Monitor");
  
  // Start Wi-Fi connection
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP: " + WiFi.localIP().toString());

  // Firebase configuration
  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Synchronize time after connecting to WiFi and Firebase
  if (WiFi.status() == WL_CONNECTED) {
    if (!syncTime()) {
      Serial.println("Warning: Time synchronization failed, device will continue with unsynchronized time");
    }
  } else {
      Serial.println("Cannot sync time, Wi-Fi not connected.");
  }
  
  Serial.println("Place your finger on the sensor");
}

void loop() {
  // Read sensor value
  sensorValue = analogRead(pulsePin);
  
  // Track signal range for better finger detection
  if (sensorValue > signalMax) signalMax = sensorValue;
  if (sensorValue < signalMin) signalMin = sensorValue;
  
  // Beat detection
  if (sensorValue > threshold && !beatDetected) {
    beatDetected = true;
    
    // Calculate BPM
    unsigned long currentTime = millis();
    if (lastBeatTime != 0) {
      unsigned long beatInterval = currentTime - lastBeatTime;
      float instantBPM = 60000.0 / beatInterval;
      
      // Only use reasonable BPM values
      if (instantBPM >= 30 && instantBPM <= 180) {
        // Smooth BPM readings
        beatsPerMinute = beatsPerMinute == 0 ? instantBPM : (beatsPerMinute * 0.7 + instantBPM * 0.3);
        hasValidReading = true;  // Set flag when we have a valid reading
        Serial.print("♥ Beat! BPM: ");
        Serial.println(beatsPerMinute, 1);
      }
    }
    lastBeatTime = currentTime;
  } 
  else if (sensorValue < threshold - 20) {  // Reduced hysteresis window
    // Reset beat detection when signal drops below threshold
    beatDetected = false;
  }
  
  // Reset signal range periodically
  static unsigned long lastRangeReset = 0;
  if (millis() - lastRangeReset > 5000) {
    signalMin = 1023;
    signalMax = 0;
    lastRangeReset = millis();
  }
  
  // Update Firebase periodically
  unsigned long currentMillis = millis();
  if (currentMillis - lastUpdateTime >= UPDATE_INTERVAL && Firebase.ready()) {
    lastUpdateTime = currentMillis;
    
    // Get current formatted timestamp
    String timestamp;
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Check if time is valid before formatting
    if (timeinfo.tm_year + 1900 >= 2024) { // Check if year seems valid
        timestamp = get12HourTime(&timeinfo);
    } else {
        timestamp = "Time not synced"; // Default message if time is not synced
    }
    
    // isOnline should reflect if the device is powered and connected
    bool isActive = true;  // Device is always online if it's running and connected
    
    // If no valid reading or signal is too low, set BPM to 0
    if (!hasValidReading || (signalMax - signalMin <= 30)) {
      beatsPerMinute = 0;
    }
    
    if (Firebase.setFloat(firebaseData, "/IOTs/PS_0000/Readings", beatsPerMinute) &&
        Firebase.setString(firebaseData, "/IOTs/PS_0000/lastStatusUpdate", timestamp) &&
        Firebase.setBool(firebaseData, "/IOTs/PS_0000/isOnline", isActive) &&
        Firebase.setString(firebaseData, "/IOTs/PS_0000/deviceType", "Pulse Sensor")) {
      Serial.println("Data updated in Firebase");
    }
    
    // Reset valid reading flag after update
    hasValidReading = false;
  }
  
  delay(10);  // Faster sampling rate
  yield();
}
