#include <WiFi.h>
#include <FirebaseESP32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "time.h"
#include <WebServer.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"



// WiFi Credentials
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Firebase Credentials
#define FIREBASE_HOST "https://your-project-id.firebaseio.com/"
#define FIREBASE_AUTH "YOUR_FIREBASE_AUTH_KEY"


FirebaseData firebaseData;
FirebaseAuth auth;
FirebaseConfig config;

// Web Server
WebServer server(80);

// Time Config (NTP Server)
const char* ntpServer = "time.google.com";
const long gmtOffset_sec = 8 * 3600;  // GMT+8 for Asia
const int daylightOffset_sec = 0;

// HX711 Load Cell
#define DT 33
#define SCK 32
HX711 scale;
float initialWeight = 0, finalWeight = 0;
float weight = 0;


// DS18B20 Temperature Sensor
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float temperature = 0;

// Relay and Button
#define COIL1 13
#define COIL2 14
#define COIL3 15
#define COIL4 16
#define LAMP5 17
#define LAMP6 18
#define ROTOR7 19
#define FAN8 21

#define BUTTON 5

int pressCount = 0;
bool dryingProcess = false;
String sessionID;  // Use date and time as session ID

unsigned long sessionStartTime = 0;  // Start time in milliseconds
bool sessionActive = false;

unsigned long elapsedTime = 0;

unsigned long lastHighTempStart = 0;

static unsigned long buttonPressTime = 0;
static bool buttonWasHeld = false;

// heat threshold
#define CHECK_INTERVAL 60000
#define HIGH_TEMP_INTERVAL 360000   // Every 7  minutes for high-temp mode (10 minutes)
#define HIGH_TEMP_THRESHOLD 65      // High temp threshold (65°C)
#define HEAT_GUN_TEMP_THRESHOLD 55  // Heat guns on if temp <= 55°C
#define FAN_TEMP_THRESHOLD 45       // Fan turns off if temp is less than 45°C

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(COIL1, OUTPUT);
  pinMode(COIL2, OUTPUT);
  pinMode(COIL4, OUTPUT);
  pinMode(LAMP5, OUTPUT);
  pinMode(LAMP6, OUTPUT);
  pinMode(ROTOR7, OUTPUT);
  pinMode(FAN8, OUTPUT);

  digitalWrite(COIL1, HIGH);
  digitalWrite(COIL2, HIGH);
  digitalWrite(COIL4, HIGH);
  digitalWrite(LAMP5, HIGH);
  digitalWrite(LAMP6, HIGH);
  digitalWrite(ROTOR7, HIGH);
  digitalWrite(FAN8, HIGH);
  digitalWrite(BUTTON, HIGH);

  Serial.println("All relays set to OFF at startup.");
  Firebase.setBool(firebaseData, "/fish_drying_system/session_active", false);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Wait for Firebase to be ready before saving IP
  delay(1000);
  Firebase.setString(firebaseData, "/fish_drying_system/esp32_ip", WiFi.localIP().toString());
  Serial.println("ESP32 IP saved to Firebase.");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Time initialized.");

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.println("Waiting for time...");
    delay(1000);
  }
  Serial.println("Time acquired successfully.");

  scale.begin(DT, SCK);
  scale.set_scale(5.31);  // Optional tare at startup


  // Start Web Server
  server.on("/temp", HTTP_GET, handleTemperature);
  server.on("/weight", HTTP_GET, handleWeight);
  server.on("/save_initial_weight", HTTP_GET, handleSaveInitialWeight);
  server.on("/save_final_weight", HTTP_GET, handleSaveFinalWeight);
  server.on("/fab_pressed", HTTP_GET, handleFabPressed);
  server.on("/duration", HTTP_GET, handleDuration);
  server.on("/tare", HTTP_GET, handleTare);  // ✅ Tare handler

  server.begin();
  sensors.begin();
}

void loop() {
  server.handleClient();  // Handle HTTP requests

  sensors.requestTemperatures();
  delay(750);  // Optional but useful for reliability
  temperature = sensors.getTempCByIndex(0);

  Serial.println("===== Sensor Readings =====");
  if (temperature == -127.0 || temperature == 85.0) {
    Serial.println("Temperature: INVALID");
    temperature = 0;
  } else {
    Serial.printf("Temperature: %.2f °C\n", temperature);
  }

  // Check if the weight sensor is connected before reading
  if (!scale.is_ready()) {
    Serial.println("Weight: INVALID (HX711 not ready)");
    weight = 0;
  } else {
    weight = scale.get_units(10);               // Take 10 readings for averaging
    float weight = scale.get_units() / 1000.0;  // Convert grams to kg
    if (weight > 5.01) {
      weight = 5.01;
    }
    weight = max(weight, 0.0f);  // Ensure weight is not negative
    Serial.printf("Weight: %.2f g\n", weight);
  }

  float moistureLoss = (initialWeight > 0) ? ((initialWeight - weight) / initialWeight) * 100 : 0;

  if (digitalRead(BUTTON) == LOW) {
    if (buttonPressTime == 0) {
      buttonPressTime = millis();
    } else if (millis() - buttonPressTime > 3000 && !buttonWasHeld) {
      Serial.println("Button held for 3 seconds. Taring scale...");
      Firebase.setString(firebaseData, "/fish_drying_sessions/logs", "Scale tared via long-press.");
      buttonWasHeld = true;
    }
  } else {
    if (buttonPressTime != 0 && !buttonWasHeld) {
      handleFabPressed();  // Normal press
    }
    buttonPressTime = 0;
    buttonWasHeld = false;
  }


  // Always update temperature and weight in real time
  Firebase.setFloat(firebaseData, "/fish_drying_sessions/real_time/temp", temperature);
  Firebase.setFloat(firebaseData, "/fish_drying_sessions/real_time/weight", weight);

  if (sessionActive) {
    elapsedTime = (millis() - sessionStartTime) / 1000;  // in seconds
    Firebase.setInt(firebaseData, "/fish_drying_sessions/real_time/duration", elapsedTime);
  } else {
    Firebase.setInt(firebaseData, "/fish_drying_sessions/real_time/duration", 0);
  }

  // 🛑 Return early if session is not active
  if (!sessionActive) return;

  // 🔁 If session is active, check for 3-hour auto-end
  if (elapsedTime >= 3 * 3600) {
    Serial.println("3 hours reached. Ending session automatically.");
    sessionActive = false;

    long calculateSessionDurationMinutes = elapsedTime;
    finalWeight = scale.get_units(10);
    if (finalWeight < 0) finalWeight = 0;

    float moistureLoss = (initialWeight > 0) ? ((initialWeight - finalWeight) / initialWeight) * 100 : 0;
    storeData(finalWeight, moistureLoss, calculateSessionDurationMinutes);
    deactivateRelays();
    dryingProcess = false;
    pressCount = 0;

    Serial.println("\n========== SESSION AUTO-SAVED ==========");
    Serial.printf("Temp: %.2f °C | Final Weight: %.2f g | Moisture Loss: %.2f%%\n", temperature, finalWeight, moistureLoss);
  }

  handleFabPressed();

  delay(500);
}

// Generates session ID with date-time format
void generateSessionID() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buffer[25];
    strftime(buffer, sizeof(buffer), "session_%d-%m-%Y_%H-%M-%S", &timeinfo);
    sessionID = String(buffer);
  } else {
    sessionID = "session_unknown";
  }
}
// Web Server Handlers
void handleTemperature() {
  server.send(200, "text/plain", String(temperature));
}

void handleWeight() {
  server.send(200, "text/plain", String(weight));
}

void handleSaveInitialWeight() {
  initialWeight = scale.get_units(10);
  if (initialWeight < 0) initialWeight = 0;
  dryingProcess = true;
  generateSessionID();
  getEpochTime();  // Start session timer
  activateRelaysSequentially();
  server.send(200, "text/plain", "Initial weight saved.");
}

void startDryingSession() {
  sessionStartTime = millis() / 1000;  // Store start time in seconds
}

long calculateSessionDuration() {
  return millis() / 1000 - sessionStartTime;
}

// ✅ Define this outside handleFabPressed()
long calculateSessionDurationMinutes() {
  return (millis() - sessionStartTime) / 1000 / 60;  // Duration in minutes
}

void handleFabPressed() {
  Serial.println("FAB pressed. Starting session...");

  // Start tracking
  sessionActive = true;
  sessionStartTime = millis();

  // Get initial weight
  initialWeight = scale.get_units(10);
  if (initialWeight < 0) initialWeight = 0;


  // Set system flags
  dryingProcess = true;

  // Generate session ID and activate relays
  generateSessionID();
  activateRelaysSequentially();

  // Save session data
  startDryingSession();

  // End session tracking
  sessionActive = false;

  // Optional: Acknowledge FAB press to client
  server.send(200, "text/plain", "Drying session started.");
}

void handleTare() {
  scale.tare();
  Firebase.setString(firebaseData, "/fish_drying_sessions/logs", "Scale tared via /tare route.");
  server.send(200, "text/plain", "Scale tared.");
}

void handleSaveFinalWeight() {
  long calculateSessionDurationMinutes = calculateSessionDuration();  // Compute session duration
  finalWeight = scale.get_units(10);
  if (finalWeight < 0) finalWeight = 0;
  float moistureLoss = (initialWeight > 0) ? ((initialWeight - finalWeight) / initialWeight) * 100 : 0;
  storeData(finalWeight, moistureLoss, calculateSessionDurationMinutes);
  Serial.printf("Final weight saved. Session duration: %ld seconds\n", calculateSessionDurationMinutes);
}

void handleDuration() {
  if (sessionStartTime == 0) {  // Check if drying session has started
    server.send(200, "text/plain", "Not started");
    return;
  }

  unsigned long elapsedTime = (millis() - sessionStartTime) / 1000;  // Convert to seconds
  int hours = elapsedTime / 3600;
  int minutes = (elapsedTime % 3600) / 60;
  int seconds = elapsedTime % 60;

  String duration = String(hours) + ":" + String(minutes) + ":" + String(seconds);
  server.send(200, "text/plain", duration);
}


// Stores data in Firebase
void storeData(float finalWeight, float moistureLoss, long calculateSessionDurationMinutes) {
  long durationSeconds = calculateSessionDurationMinutes % 3600;

  String path = "/fish_drying_sessions/" + sessionID + "/";

  Firebase.setFloat(firebaseData, path + "initial_weight", initialWeight);
  Firebase.setFloat(firebaseData, path + "final_weight", finalWeight);
  Firebase.setFloat(firebaseData, path + "moisture_loss", moistureLoss);
  Firebase.setFloat(firebaseData, path + "temperature", temperature);
  Firebase.setString(firebaseData, path + "timestamp", getFormattedTime());
  Firebase.setInt(firebaseData, path + "epoch_time", getEpochTime());
  Firebase.setInt(firebaseData, path + "session_duration_minutes", calculateSessionDurationMinutes);
  Firebase.setInt(firebaseData, path + "session_duration_seconds", durationSeconds);

  //Serial.printf("Session Duration: %ld hours and %ld seconds\n", durationHours, durationSeconds);
  Serial.println("Data stored in Firebase.");
}


// Converts time to readable format
String getFormattedTime() {
  struct tm timeinfo;
  int retryCount = 10;  // Retry up to 10 times

  while (!getLocalTime(&timeinfo) && retryCount > 0) {
    Serial.println("Retrying time sync...");
    delay(1000);
    retryCount--;
  }

  if (retryCount == 0) {
    Serial.println("Failed to obtain time after retries.");
    return "Unknown Time";
  }
  char buffer[64];  // ✅ Declare buffer before using it
  strftime(buffer, sizeof(buffer), "session_%d-%m-%Y_%H-%M-%S", &timeinfo);
  return String(buffer);
}

long getEpochTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time.");
    return 0;
  }
  time(&now);
  return now;
}

void activateRelaysSequentially() {
  Serial.println("Starting optimized drying process...");
  Firebase.setBool(firebaseData, "/fish_drying_system/session_active", true);

  unsigned long lastHeatToggle = millis();
  unsigned long startTime = millis();  // Track session start time
  lastHighTempStart = millis();
  unsigned long lastNormalToggle = millis();
  bool heatGunsOn = false;
  bool highTempMode = false;
  bool normalModeActive = true;  // Track whether normal mode is running
  bool lampsOn = false;

  digitalWrite(ROTOR7, LOW);  // Start rotor motor (active low)
  Serial.println("Rotary Motor ON");

  // 🔥 PREHEAT PHASE: 2 minutes, max 55°C
  Serial.println("Preheating for 2 minutes (target: 55°C)...");
  unsigned long preheatStart = millis();
  while (millis() - preheatStart < 3.5 * 60 * 1000UL) {
    sensors.requestTemperatures();
    float preheatTemp = sensors.getTempCByIndex(0);
    float preheatWeight = scale.get_units(10);
    if (preheatWeight < 0) preheatWeight = 0;

    Firebase.setFloat(firebaseData, "/fish_drying_sessions/real_time/temp", preheatTemp);
    Firebase.setFloat(firebaseData, "/fish_drying_sessions/real_time/weight", preheatWeight);
    Firebase.setInt(firebaseData, "/fish_drying_sessions/real_time/duration", (millis() - startTime) / 1000);

    if (preheatTemp < HEAT_GUN_TEMP_THRESHOLD) {
      digitalWrite(COIL1, LOW);
      digitalWrite(COIL2, LOW);
      digitalWrite(COIL4, LOW);
      digitalWrite(LAMP5, LOW);
      digitalWrite(LAMP6, LOW);
    } else {
      digitalWrite(COIL1, HIGH);
      digitalWrite(COIL2, HIGH);
      digitalWrite(COIL4, HIGH);
      digitalWrite(LAMP5, HIGH);
      digitalWrite(LAMP6, HIGH);
    }

    delay(1000);  // Wait 1 second between readings
  }

  // Turn off elements after preheat
  digitalWrite(COIL1, HIGH);
  digitalWrite(COIL2, HIGH);
  digitalWrite(COIL4, HIGH);
  digitalWrite(LAMP5, HIGH);
  digitalWrite(LAMP6, HIGH);

  while ((millis() - startTime) < 14400000 ) {  // 3 hours
    sensors.requestTemperatures();
    float temperature = sensors.getTempCByIndex(0);
    float weight = scale.get_units(10);
    if (weight < 0) weight = 0;
    unsigned long elapsedTime = (millis() - startTime) / 1000;


    Firebase.setFloat(firebaseData, "/fish_drying_sessions/real_time/temp", temperature);
    Firebase.setFloat(firebaseData, "/fish_drying_sessions/real_time/weight", weight);
    Firebase.setInt(firebaseData, "/fish_drying_sessions/real_time/duration", elapsedTime);

    delay(1000);

    // Track elapsed time for display
    unsigned long elapsedSec = (millis() - startTime) / 1000;
    int hours = elapsedSec / 3600;
    int minutes = (elapsedSec % 3600) / 60;
    int seconds = elapsedSec % 60;

    // 🌡️ Enable high-temp mode every 10 minutes
    if ((millis() - lastHighTempStart) >= HIGH_TEMP_INTERVAL) {  // Every 10 min
      highTempMode = true;
      normalModeActive = false;
      Firebase.setBool(firebaseData, "/fish_drying_system/high_temp", true);

      // Turn on all heating elements and fan during high-temp mode
      digitalWrite(COIL1, LOW);  // Heat gun coils on (active low)
      digitalWrite(COIL2, LOW);
      digitalWrite(COIL4, LOW);
      digitalWrite(LAMP5, LOW);  // Turn lamps on (active low)
      digitalWrite(LAMP6, LOW);
      digitalWrite(FAN8, HIGH);  // Turn fan off (active low)

      Serial.println("🔺 High-temp mode ENABLED:  up to 65°C");
    }

    // If in high-temp mode, stop heating elements when 65°C is reached
    if (highTempMode && temperature >= HIGH_TEMP_THRESHOLD) {  // Temperature reached 65°C
      // Turn off all heating elements immediately
      digitalWrite(COIL1, HIGH);  // Turn off heat gun coils (active low)
      digitalWrite(COIL2, HIGH);
      digitalWrite(COIL4, HIGH);
      digitalWrite(LAMP5, HIGH);  // Turn off lamps (active low)
      digitalWrite(LAMP6, HIGH);

      // Log that the temperature limit was reached
      Serial.println("🔥 High-temp mode: 65°C reached. Turning off all heating elements.");
      highTempMode = false;  // Exit high-temp mode
      Firebase.setBool(firebaseData, "/fish_drying_system/high_temp", false);
      lastHighTempStart = millis();  // ✅ Reset AFTER mode ends

      // Turn off the fan if necessary (cooling mode)
      digitalWrite(FAN8, HIGH);  // Turn off the fan (active low)

      // Return to normal temperature control (55°C)
      normalModeActive = true;  // Resume normal mode
      Serial.println("Switching back to normal mode (55°C)");
    }

    // 🌡️ Heat gun control logic for normal mode
    if (normalModeActive && !highTempMode) {
      if (heatGunsOn && millis() - lastHeatToggle >= CHECK_INTERVAL) {
        heatGunsOn = false;
        digitalWrite(COIL1, HIGH);  // Turn off heating elements (active low)
        digitalWrite(COIL2, HIGH);
        digitalWrite(COIL4, HIGH);
        Serial.println("Heat Guns: OFF");
        lastHeatToggle = millis();
      }

      else if (!heatGunsOn && millis() - lastHeatToggle >= CHECK_INTERVAL && temperature <= HEAT_GUN_TEMP_THRESHOLD) {
        heatGunsOn = true;
        digitalWrite(COIL1, LOW);  // Turn on heat guns (active low)
        digitalWrite(COIL2, LOW);
        digitalWrite(COIL4, LOW);
        Serial.println("Heat Guns: ON");
        lastHeatToggle = millis();
      }

      else if (temperature >= HEAT_GUN_TEMP_THRESHOLD && heatGunsOn) {
        heatGunsOn = false;
        digitalWrite(COIL1, HIGH);  // Turn off heating elements (active low)
        digitalWrite(COIL2, HIGH);
        digitalWrite(COIL4, HIGH);
        Serial.printf("Heat Guns OFF: Temperature reached %.2f°C\n", HEAT_GUN_TEMP_THRESHOLD);
        lastHeatToggle = millis();
      }
    }

    // Lamp Logic
    if (temperature >= 60) {
      digitalWrite(LAMP5, HIGH);  // Turn on lamps (active low)
      digitalWrite(LAMP6, HIGH);
    } else if (temperature < 60) {
      digitalWrite(LAMP5, LOW);  // Turn off lamps (active low)
      digitalWrite(LAMP6, LOW);
    }

    // Fan Logic
    if (!highTempMode) {
      if (temperature >= 55 && digitalRead(FAN8) == HIGH) {
        digitalWrite(FAN8, LOW);  // Turn on fan (active low)
        Serial.println("Fan ON: Overheat Protection!");
      } else if (temperature < FAN_TEMP_THRESHOLD && digitalRead(FAN8) == LOW) {
        digitalWrite(FAN8, HIGH);  // Turn off fan (active low)
        Serial.println("Fan OFF: Temperature stabilized.");
      }
    }
    Serial.printf("Duration: %02d:%02d:%02d | Weight: %.2f g | Temp: %.2f°C | Heatguns: %s | Exhaust: %s | Lamps: %s | Rotor: %s\n",
                  hours, minutes, seconds,
                  weight, temperature,
                  heatGunsOn ? "ON" : "OFF",
                  digitalRead(FAN8) ? "OFF" : "ON",
                  digitalRead(LAMP5) ? "OFF" : "ON",
                  digitalRead(ROTOR7) ? "OFF" : "ON");

    delay(500);  // Adjust the delay as needed for performance

  deactivateRelays();
  delay(60000);
  // 📋 Session Summary
  finalWeight = scale.get_units(10);
  if (finalWeight < 0) finalWeight = 0;
  float moistureLoss = ((initialWeight - finalWeight) / initialWeight) * 100.0;
  long sessionDuration = (millis() - sessionStartTime) / 60000;

  Serial.println("\n========== SESSION SAVED! ==========");
  Serial.printf("Temperature (C): %.2f\n", temperature);
  Serial.printf("Initial Weight: %.2f g\n", initialWeight);
  Serial.printf("Final Weight: %.2f g\n", finalWeight);
  Serial.printf("Moisture Loss: %.2f%%\n", moistureLoss);
  Serial.printf("Timestamp: %s\n", getFormattedTime().c_str());
  Serial.printf("Session Duration: %ld minutes\n", sessionDuration);
  Serial.println("Process resetting...\n");
  storeData(finalWeight, moistureLoss, calculateSessionDurationMinutes());
}

void deactivateRelays() {
  digitalWrite(COIL1, HIGH);
  digitalWrite(COIL2, HIGH);
  digitalWrite(COIL4, HIGH);
  digitalWrite(LAMP5, HIGH);
  digitalWrite(LAMP6, HIGH);
  digitalWrite(ROTOR7, HIGH);
  digitalWrite(FAN8, HIGH);
  Serial.println("Relays Deactivated.");
}