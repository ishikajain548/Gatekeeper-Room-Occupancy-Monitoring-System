#include <WiFi.h>
#include <HTTPClient.h>
#include <Firebase_ESP_Client.h>

// Firebase helper files
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// ==============================
// WIFI + FIREBASE CONFIG
// ==============================
#define WIFI_SSID       "Motorola-hotspot"
#define WIFI_PASSWORD   "12345678"

#define API_KEY         "AIzaSyBVyq-F1bUQiSsxFoBLF6EyaZjjB0avyvA"
#define DATABASE_URL    "https://fir-acc9d-default-rtdb.asia-southeast1.firebasedatabase.app/"

// ==============================
// GOOGLE SHEETS WEB APP URL
// ==============================
const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbyhvntOd1griN0LhQWODPzOatI4xgTYlpt_teejuYuH_KHNdJqAgXmL4xhjNHPEmwPd/exec";

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool firebaseReady = false;

// Firebase device path (for 1 gate room with 2 sensors)
String devicePath = "/devices/roomB_1gate";

// ==============================
// LCD Object
// ==============================
hd44780_I2Cexp lcd;

// ==============================
// Sensor Pins
// ==============================
const int S1 = 5;    // Sensor 1 -> D5
const int S2 = 19;   // Sensor 2 -> D19

// ==============================
// Device Info
// ==============================
const String DEVICE_ID = "roomB_1gate";
const String ROOM_NAME = "Lab 1";
const int GATE_TYPE = 1;
const int ROOM_CAPACITY = 20;

// ==============================
// Count Variables
// ==============================
int peopleCount = 0;   // Current people inside
int inCount = 0;       // Total entries
int outCount = 0;      // Total exits

// ==============================
// Gate Status Tracking
// ==============================
String currentGate1Status = "inactive";

// ==============================
// Queue Settings
// ==============================
#define MAX_QUEUE 10
char sensorQueue[MAX_QUEUE];
int queueSize = 0;

// ==============================
// Timing Settings
// ==============================
unsigned long lastEventTime = 0;
const unsigned long SEQUENCE_TIMEOUT = 3000;

const unsigned long SENSOR_COOLDOWN = 300;
unsigned long lastS1TriggerTime = 0;
unsigned long lastS2TriggerTime = 0;

// Firebase periodic sync
unsigned long lastFirebaseSync = 0;
const unsigned long FIREBASE_SYNC_INTERVAL = 500; // every 5 sec heartbeat update

// ==============================
// Sensor States
// ACTIVE HIGH:
// LOW  = no object
// HIGH = object detected
// ==============================
bool lastS1State = LOW;
bool lastS2State = LOW;

// ==============================
// Helper Functions
// ==============================
float getOccupancyPercent()
{
  if (ROOM_CAPACITY <= 0) return 0.0;
  return ((float)peopleCount / ROOM_CAPACITY) * 100.0;
}

int getAvailableSpace()
{
  int available = ROOM_CAPACITY - peopleCount;
  if (available < 0) available = 0;
  return available;
}

// ==============================
// LCD Update Function
// ==============================
void updateLCD(String lastEvent = "READY")
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SMART PEOPLE COUNTER");

  lcd.setCursor(0, 1);
  lcd.print("IN:");
  lcd.print(inCount);

  lcd.setCursor(10, 1);
  lcd.print("OUT:");
  lcd.print(outCount);

  lcd.setCursor(0, 2);
  lcd.print("CURRENT:");
  lcd.print(peopleCount);

  lcd.setCursor(0, 3);
  lcd.print("LAST:");
  lcd.print(lastEvent);
}

// ==============================
// WIFI CONNECT
// ==============================
void connectWiFi()
{
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// ==============================
// FIREBASE INIT (Anonymous Auth)
// ==============================
void initFirebase()
{
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  // Anonymous Sign In
  if (Firebase.signUp(&config, &auth, "", ""))
  {
    Serial.println("Firebase anonymous auth OK");
    firebaseReady = true;
  }
  else
  {
    Serial.print("Firebase signUp failed: ");
    Serial.println(config.signer.signupError.message.c_str());
    firebaseReady = false;
    return;
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Firebase initialized successfully");
}

// ==============================
// Wait until Firebase token ready
// ==============================
bool waitForFirebaseReady(unsigned long timeoutMs = 10000)
{
  Serial.println("Waiting for Firebase ready...");

  unsigned long start = millis();
  while (!Firebase.ready() && millis() - start < timeoutMs)
  {
    delay(100);
  }

  if (Firebase.ready())
  {
    Serial.println("Firebase is ready!");
    return true;
  }
  else
  {
    Serial.println("Firebase not ready after waiting.");
    return false;
  }
}

// ==============================
// GOOGLE SHEETS LOGGING
// ONLY call on ENTRY / EXIT
// timestamp NOT sent from ESP32
// because Apps Script uses new Date()
// ==============================
void logToGoogleSheets(String eventType)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Skipping Google Sheets log: WiFi not connected");
    return;
  }

  if (String(GOOGLE_SCRIPT_URL) == "PASTE_YOUR_GOOGLE_APPS_SCRIPT_WEB_APP_URL_HERE")
  {
    Serial.println("Google Sheets URL not set. Skipping Sheets log.");
    return;
  }

  HTTPClient http;
  http.begin(GOOGLE_SCRIPT_URL);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);   // IMPORTANT for script.google.com redirects
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"deviceId\":\"" + DEVICE_ID + "\",";
  payload += "\"roomName\":\"" + ROOM_NAME + "\",";
  payload += "\"gateType\":" + String(GATE_TYPE) + ",";
  payload += "\"roomCapacity\":" + String(ROOM_CAPACITY) + ",";
  payload += "\"currentCount\":" + String(peopleCount) + ",";
  payload += "\"inCount\":" + String(inCount) + ",";
  payload += "\"outCount\":" + String(outCount) + ",";
  payload += "\"gate1Status\":\"" + currentGate1Status + "\",";
  payload += "\"gate2Status\":\"not_applicable\",";
  payload += "\"deviceStatus\":\"online\",";
  payload += "\"lastUpdateMs\":" + String(millis()) + ",";
  payload += "\"occupancyPercent\":" + String(getOccupancyPercent(), 2) + ",";
  payload += "\"availableSpace\":" + String(getAvailableSpace()) + ",";
  payload += "\"eventType\":\"" + eventType + "\"";
  payload += "}";

  Serial.println("==================================");
  Serial.println("[GOOGLE SHEETS] Sending EVENT data...");
  Serial.print("[GOOGLE SHEETS] Payload: ");
  Serial.println(payload);

  int httpResponseCode = http.POST(payload);

  Serial.print("[GOOGLE SHEETS] Response code: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0)
  {
    String response = http.getString();
    Serial.print("[GOOGLE SHEETS] Response: ");
    Serial.println(response);
  }
  else
  {
    Serial.print("[GOOGLE SHEETS] Error: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }

  Serial.println("==================================");

  http.end();
}

// ==============================
// FIREBASE LIVE UPDATE
// ==============================
void updateFirebase(String deviceStatus = "online")
{
  if (!firebaseReady || !Firebase.ready())
  {
    Serial.println("Skipping Firebase update: Firebase not ready");
    return;
  }

  FirebaseJson json;

  json.set("deviceId", DEVICE_ID);
  json.set("roomName", ROOM_NAME);
  json.set("gateType", GATE_TYPE);
  json.set("roomCapacity", ROOM_CAPACITY);

  json.set("currentCount", peopleCount);
  json.set("inCount", inCount);
  json.set("outCount", outCount);

  json.set("gate1Status", currentGate1Status);
  json.set("gate2Status", "not_applicable");

  json.set("status", deviceStatus);
  json.set("lastUpdateMs", millis());

  // Optional extra fields
  json.set("deviceStatus", deviceStatus);
  json.set("occupancyPercent", getOccupancyPercent());
  json.set("availableSpace", getAvailableSpace());

  if (Firebase.RTDB.setJSON(&fbdo, devicePath.c_str(), &json))
  {
    Serial.println("Firebase updated successfully");
  }
  else
  {
    Serial.print("Firebase update failed: ");
    Serial.println(fbdo.errorReason());
  }
}

// ==============================
// Utility: Print Queue
// ==============================
void printQueue()
{
  Serial.print("Queue: ");
  if (queueSize == 0)
  {
    Serial.println("[empty]");
    return;
  }

  for (int i = 0; i < queueSize; i++)
  {
    Serial.print(sensorQueue[i]);
    if (i < queueSize - 1) Serial.print(" ");
  }
  Serial.println();
}

// ==============================
// Queue Operations
// ==============================
void shiftQueue(int n)
{
  for (int i = n; i < queueSize; i++)
  {
    sensorQueue[i - n] = sensorQueue[i];
  }

  queueSize -= n;
  if (queueSize < 0) queueSize = 0;
}

void clearQueue(const char* reason)
{
  queueSize = 0;
  Serial.print("Queue cleared: ");
  Serial.println(reason);
  printQueue();
  Serial.println("----------------------------");

  currentGate1Status = "inactive";
  updateLCD("TIMEOUT");
  updateFirebase("online");   // Firebase only, no Sheets
}

// ==============================
// Add Event to Queue
// ==============================
void addEvent(char sensor)
{
  if (queueSize >= MAX_QUEUE)
  {
    clearQueue("overflow protection");
  }

  if (queueSize < MAX_QUEUE)
  {
    sensorQueue[queueSize++] = sensor;
    lastEventTime = millis();

    Serial.print("Event added: ");
    Serial.println(sensor);
    printQueue();

    // Gate active while sequence in progress
    currentGate1Status = "active";
    updateFirebase("online");   // Firebase live

    if (sensor == '1')
      updateLCD("S1 DETECT");
    else
      updateLCD("S2 DETECT");
  }
}

// ==============================
// Process Queue
// 1 2 = Entry
// 2 1 = Exit
// Google Sheets ONLY on valid ENTRY / EXIT
// IMPORTANT FIX:
// Sheets log is sent while gate is still ACTIVE,
// then gate is set back to INACTIVE.
// ==============================
void processQueue()
{
  if (queueSize < 2)
    return;

  char first = sensorQueue[0];
  char second = sensorQueue[1];

  // ENTRY: S1 -> S2
  if (first == '1' && second == '2')
  {
    peopleCount++;
    inCount++;

    if (peopleCount > ROOM_CAPACITY)
    {
      peopleCount = ROOM_CAPACITY;
    }

    Serial.println(">>> ENTRY DETECTED");
    Serial.print("People Count = ");
    Serial.println(peopleCount);

    updateLCD("ENTRY");

    // Gate is still ACTIVE here, so Google Sheets receives active
    updateFirebase("online");
    logToGoogleSheets("ENTRY");

    // After logging event, mark gate inactive
    currentGate1Status = "inactive";
    updateFirebase("online");

    shiftQueue(2);
    printQueue();
    Serial.println("----------------------------");
  }
  // EXIT: S2 -> S1
  else if (first == '2' && second == '1')
  {
    if (peopleCount > 0)
    {
      peopleCount--;
      outCount++;
    }
    else
    {
      peopleCount = 0;
    }

    Serial.println(">>> EXIT DETECTED");
    Serial.print("People Count = ");
    Serial.println(peopleCount);

    updateLCD("EXIT");

    // Gate is still ACTIVE here, so Google Sheets receives active
    updateFirebase("online");
    logToGoogleSheets("EXIT");

    // After logging event, mark gate inactive
    currentGate1Status = "inactive";
    updateFirebase("online");

    shiftQueue(2);
    printQueue();
    Serial.println("----------------------------");
  }
  else
  {
    Serial.println("Invalid/duplicate pattern detected, removing oldest event");
    shiftQueue(1);
    printQueue();
    Serial.println("----------------------------");
  }
}

// ==============================
// Sensor Trigger Handlers
// ==============================
void handleS1Trigger()
{
  unsigned long now = millis();

  if (now - lastS1TriggerTime < SENSOR_COOLDOWN)
  {
    return;
  }

  lastS1TriggerTime = now;

  Serial.println("S1 TRIGGERED");
  addEvent('1');
}

void handleS2Trigger()
{
  unsigned long now = millis();

  if (now - lastS2TriggerTime < SENSOR_COOLDOWN)
  {
    return;
  }

  lastS2TriggerTime = now;

  Serial.println("S2 TRIGGERED");
  addEvent('2');
}

// ==============================
// Setup
// ==============================
void setup()
{
  Serial.begin(115200);
  delay(1000);

  // Sensor pins
  pinMode(S1, INPUT);
  pinMode(S2, INPUT);

  lastS1State = digitalRead(S1);
  lastS2State = digitalRead(S2);

  // LCD INIT
  Wire.begin(21, 22);   // ESP32 I2C pins

  int status = lcd.begin(20, 4);

  if (status)
  {
    Serial.print("LCD init failed, status = ");
    Serial.println(status);
    while (1);
  }

  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("SMART PEOPLE COUNTER");
  lcd.setCursor(0, 1);
  lcd.print("Connecting WiFi...");

  // WIFI + FIREBASE
  connectWiFi();

  lcd.setCursor(0, 2);
  lcd.print("Connecting Firebase");
  initFirebase();

  // Wait for token ready
  if (firebaseReady && waitForFirebaseReady())
  {
    // Test write
    if (Firebase.RTDB.setString(&fbdo, "/test/message", "ESP32 Connected"))
    {
      Serial.println("Test write success!");
    }
    else
    {
      Serial.print("Test write failed: ");
      Serial.println(fbdo.errorReason());
    }
  }

  // Initial LCD
  updateLCD("READY");

  // Initial Firebase push
  currentGate1Status = "inactive";
  updateFirebase("online");

  Serial.println();
  Serial.println("========================================");
  Serial.println(" roomB_1gate (1 Gate, 2 Sensors) ");
  Serial.println("S1 -> S2 = ENTRY");
  Serial.println("S2 -> S1 = EXIT");
  Serial.println("Firebase Path: /devices/roomB_1gate");
  Serial.println("Google Sheets: ONLY ENTRY / EXIT logs");
  Serial.println("Timestamp: generated by Apps Script new Date()");
  Serial.println("Gate1Status in Sheets: FIXED (active during event)");
  Serial.println("========================================");
  Serial.println();
}

// ==============================
// Main Loop
// ==============================
void loop()
{
  // Reconnect WiFi if disconnected
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi disconnected. Reconnecting...");
    connectWiFi();

    // After reconnect, push online status again
    if (firebaseReady && Firebase.ready())
    {
      updateFirebase("online");
    }
  }

  bool s1State = digitalRead(S1);
  bool s2State = digitalRead(S2);

  // Detect only LOW -> HIGH transitions
  if (lastS1State == LOW && s1State == HIGH)
  {
    handleS1Trigger();
  }

  if (lastS2State == LOW && s2State == HIGH)
  {
    handleS2Trigger();
  }

  // Update previous states
  lastS1State = s1State;
  lastS2State = s2State;

  // Process queue
  processQueue();

  // Timeout protection
  if (queueSize > 0 && (millis() - lastEventTime > SEQUENCE_TIMEOUT))
  {
    clearQueue("sequence timeout");
  }

  // Periodic heartbeat update to Firebase ONLY
  if (millis() - lastFirebaseSync > FIREBASE_SYNC_INTERVAL)
  {
    lastFirebaseSync = millis();
    updateFirebase("online");
  }

  delay(20);
}