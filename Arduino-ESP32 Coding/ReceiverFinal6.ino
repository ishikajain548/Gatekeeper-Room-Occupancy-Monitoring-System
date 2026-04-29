#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// Token helper
#include "addons/TokenHelper.h"
// RTDB helper
#include "addons/RTDBHelper.h"

// ======================================================
// LCD OBJECT
// ======================================================
hd44780_I2Cexp lcd;

// ======================================================
// DEBUG MODE
// 1 = enable serial logs
// 0 = disable extra logs for faster performance
// ======================================================
#define DEBUG 1

#if DEBUG
  #define DBG_PRINT(x) Serial.print(x)
  #define DBG_PRINTLN(x) Serial.println(x)
#else
  #define DBG_PRINT(x)
  #define DBG_PRINTLN(x)
#endif


#define WIFI_SSID "Motorola-hotspot"
#define WIFI_PASSWORD "12345678"

// ======================================================
// Firebase Project Credentials
// ======================================================
#define API_KEY "AIzaSyBVyq-F1bUQiSsxFoBLF6EyaZjjB0avyvA"
#define DATABASE_URL "https://fir-acc9d-default-rtdb.asia-southeast1.firebasedatabase.app/"

// ======================================================
// GOOGLE SHEETS WEB APP URL (Apps Script deployed as Web App)
// IMPORTANT: Use /exec URL, NOT /dev
// ======================================================
const char* GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbyhvntOd1griN0LhQWODPzOatI4xgTYlpt_teejuYuH_KHNdJqAgXmL4xhjNHPEmwPd/exec";

// ======================================================
// RECEIVER LOCAL GATE SENSORS (Gate 1)
// ======================================================
const int S1 = 5;
const int S2 = 19;

// ======================================================
// Sensor Logic
// If your IR sensor gives LOW when object detected -> keep LOW
// If HIGH when object detected -> change to HIGH
// ======================================================
const int SENSOR_TRIGGERED = LOW;

// ======================================================
// SENDER MAC ADDRESS (GATE 2 ESP32)
// REPLACE WITH YOUR ACTUAL SENDER MAC
// ======================================================
uint8_t senderMAC[] = {0x70, 0x4B, 0xCA, 0x49, 0x9E, 0xF0};

// ======================================================
// Firebase Objects
// ======================================================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool firebaseReady = false;

// ======================================================
// FINAL SYNCHRONIZED COUNTS
// ======================================================
volatile int currentCount = 0;   // currently inside
volatile int inCount = 0;        // total entries
volatile int outCount = 0;

// ======================================================
// GATE STATUS TRACKING (REAL-TIME)
// ======================================================
String gate1Status = "inactive";
String gate2Status = "inactive";

unsigned long gate1LastActiveTime = 0;
unsigned long gate2LastActiveTime = 0;

const unsigned long GATE_ACTIVE_HOLD_MS = 1500; // keep active for 1.5 sec after trigger/event

// ======================================================
// LCD STATUS MESSAGE CONTROL
// ======================================================
String lcdLastMessage = "READY";
unsigned long lcdMessageTime = 0;
const unsigned long LCD_MESSAGE_HOLD_MS = 1500;

// ======================================================
// SMART FIREBASE UPDATE CONTROL
// ======================================================
volatile bool dataChanged = false;
unsigned long lastFirebasePush = 0;
const unsigned long FIREBASE_MIN_GAP = 500; // ms

// ======================================================
// GOOGLE SHEETS DEFERRED UPDATE CONTROL
// IMPORTANT: HTTP call must NOT happen inside ESP-NOW callback
// ======================================================
volatile bool googleSheetsPending = false;
unsigned long lastGoogleSheetsPush = 0;
const unsigned long GOOGLE_SHEETS_MIN_GAP = 300; // ms

// ======================================================
// Sensor State Tracking (Non-blocking)
// ======================================================
const unsigned long SENSOR_DEBOUNCE = 120;
const unsigned long SEQUENCE_TIMEOUT = 2000;

bool s1PrevState = false;
bool s2PrevState = false;

unsigned long lastS1Trigger = 0;
unsigned long lastS2Trigger = 0;

// ======================================================
// Local Gate Sequence Buffer
// ======================================================
char sequence[2];
int seqCount = 0;
unsigned long lastSequenceTime = 0;

// ======================================================
// PACKETS
// E = Event packet (sender -> receiver)
// S = Sync packet  (receiver -> sender)
// ======================================================
typedef struct struct_packet {
  char packetType;          // 'E' = Event, 'S' = Sync
  char eventType;           // valid when packetType='E'
  unsigned long eventId;    // valid when packetType='E'
  int currentCount;         // valid when packetType='S'
  int inCount;              // valid when packetType='S'
  int outCount;             // valid when packetType='S'
} struct_packet;

struct_packet incomingPacket;
struct_packet syncPacket;

// Optional duplicate tracking
unsigned long lastRemoteEventId = 0;

// ======================================================
// FUNCTION DECLARATIONS
// ======================================================
void processLocalSequence();
void applyEvent(char eventType, const char* source);
void sendSyncToSender();
bool sendLiveDataToGoogleSheets();
bool sendLiveDataToFirebase();
void handleFirebaseUpdate();
void handleGoogleSheetsUpdate();
void handleLocalSensors();
void setupEspNow();
void setupFirebase();
void connectToWiFi();
void updateLCD(const char* statusMsg = "READY");
void setLCDMessage(const char* msg);
void handleGateStatusTimeouts();
void handleLCDMessageTimeout();
void markGate1Active();
void markGate2Active();
void resetSequence();
void printSequence();
void addToSequence(char sensor);

// ======================================================
// LCD UPDATE FUNCTION
// ======================================================
void updateLCD(const char* statusMsg) {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("SMART PEOPLE COUNTER");

  lcd.setCursor(0, 1);
  lcd.print("IN: ");
  lcd.print(inCount);

  lcd.setCursor(10, 1);
  lcd.print("OUT: ");
  lcd.print(outCount);

  lcd.setCursor(0, 2);
  lcd.print("CURRENT: ");
  lcd.print(currentCount);

  lcd.setCursor(0, 3);
  lcd.print("LAST:");
  lcd.print(statusMsg);
}

// ======================================================
// SMART LCD MESSAGE SETTER
// ======================================================
void setLCDMessage(const char* msg) {
  lcdLastMessage = msg;
  lcdMessageTime = millis();
  updateLCD(msg);
}

// ======================================================
// Utility Functions
// ======================================================
void resetSequence() {
  seqCount = 0;
}

void printSequence() {
  DBG_PRINT("[SEQ] ");
  if (seqCount == 0) {
    DBG_PRINTLN("[empty]");
    return;
  }

  for (int i = 0; i < seqCount; i++) {
    DBG_PRINT(sequence[i]);
    if (i < seqCount - 1) DBG_PRINT(" -> ");
  }
  DBG_PRINTLN("");
}

void addToSequence(char sensor) {
  if (seqCount < 2) {
    sequence[seqCount++] = sensor;
  } else {
    // Shift left and add latest
    sequence[0] = sequence[1];
    sequence[1] = sensor;
  }
  lastSequenceTime = millis();
  printSequence();
}

// ======================================================
// GATE STATUS HELPERS
// ======================================================
void markGate1Active() {
  if (gate1Status != "active") {
    gate1Status = "active";
    dataChanged = true;
    DBG_PRINTLN("[GATE1] ACTIVE");
  }
  gate1LastActiveTime = millis();
}

void markGate2Active() {
  if (gate2Status != "active") {
    gate2Status = "active";
    dataChanged = true;
    DBG_PRINTLN("[GATE2] ACTIVE");
  }
  gate2LastActiveTime = millis();
}

void handleGateStatusTimeouts() {
  unsigned long now = millis();
  bool changed = false;

  if (gate1Status == "active" && (now - gate1LastActiveTime >= GATE_ACTIVE_HOLD_MS)) {
    gate1Status = "inactive";
    changed = true;
    DBG_PRINTLN("[GATE1] INACTIVE");
  }

  if (gate2Status == "active" && (now - gate2LastActiveTime >= GATE_ACTIVE_HOLD_MS)) {
    gate2Status = "inactive";
    changed = true;
    DBG_PRINTLN("[GATE2] INACTIVE");
  }

  if (changed) {
    dataChanged = true;
  }
}

// ======================================================
// LCD AUTO-RESTORE TO READY
// ======================================================
void handleLCDMessageTimeout() {
  if (lcdLastMessage != "READY" && (millis() - lcdMessageTime >= LCD_MESSAGE_HOLD_MS)) {
    lcdLastMessage = "READY";
    updateLCD("READY");
  }
}

// ======================================================
// SEND SYNC COUNTS TO SENDER
// ======================================================
void sendSyncToSender() {
  syncPacket.packetType = 'S';
  syncPacket.eventType = '-';
  syncPacket.eventId = 0;
  syncPacket.currentCount = currentCount;
  syncPacket.inCount = inCount;
  syncPacket.outCount = outCount;

  esp_err_t result = esp_now_send(senderMAC, (uint8_t *)&syncPacket, sizeof(syncPacket));

  if (result == ESP_OK) {
    DBG_PRINTLN("[ESP-NOW] Sync packet queued to sender");
  } else {
    DBG_PRINT("[ESP-NOW] Sync send failed. Error = ");
    DBG_PRINTLN(result);
  }
}

// ======================================================
// Send FINAL TOTAL COUNTS to Google Sheets (ONLY ON EVENT)
// IMPORTANT: Called ONLY from loop(), never from ESP-NOW callback
// ======================================================
bool sendLiveDataToGoogleSheets() {
  if (WiFi.status() != WL_CONNECTED) {
    DBG_PRINTLN("[GOOGLE SHEETS] WiFi not connected. Skipping.");
    return false;
  }

  HTTPClient http;

  http.begin(GOOGLE_SCRIPT_URL);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/json");

  // Derived values
  int roomCapacity = 40;
  int occupancyPercent = (roomCapacity > 0) ? (currentCount * 100 / roomCapacity) : 0;
  int availableSpace = roomCapacity - currentCount;
  if (availableSpace < 0) availableSpace = 0;

  // Build JSON payload matching your Apps Script
  String jsonPayload = "{";
  jsonPayload += "\"deviceId\":\"roomA_2gate\",";
  jsonPayload += "\"roomName\":\"Seminar Hall\",";
  jsonPayload += "\"gateType\":2,";
  jsonPayload += "\"roomCapacity\":40,";
  jsonPayload += "\"currentCount\":" + String(currentCount) + ",";
  jsonPayload += "\"inCount\":" + String(inCount) + ",";
  jsonPayload += "\"outCount\":" + String(outCount) + ",";
  jsonPayload += "\"gate1Status\":\"" + gate1Status + "\",";
  jsonPayload += "\"gate2Status\":\"" + gate2Status + "\",";
  jsonPayload += "\"deviceStatus\":\"online\",";
  jsonPayload += "\"lastUpdateMs\":" + String(millis()) + ",";
  jsonPayload += "\"occupancyPercent\":" + String(occupancyPercent) + ",";
  jsonPayload += "\"availableSpace\":" + String(availableSpace);
  jsonPayload += "}";

  DBG_PRINTLN("==================================");
  DBG_PRINTLN("[GOOGLE SHEETS] Sending EVENT data...");
  DBG_PRINT("[GOOGLE SHEETS] Payload: ");
  DBG_PRINTLN(jsonPayload);

  int httpResponseCode = http.POST(jsonPayload);
  String response = http.getString();

  DBG_PRINT("[GOOGLE SHEETS] HTTP Code: ");
  DBG_PRINTLN(httpResponseCode);
  DBG_PRINT("[GOOGLE SHEETS] Response: ");
  DBG_PRINTLN(response);

  http.end();

  if (httpResponseCode > 0) {
    DBG_PRINTLN("[GOOGLE SHEETS] Row appended successfully!");
    DBG_PRINTLN("==================================");
    return true;
  } else {
    DBG_PRINTLN("[GOOGLE SHEETS] Failed to append row.");
    DBG_PRINTLN("==================================");
    return false;
  }
}

// ======================================================
// FINAL COUNTER UPDATE
// source = "LOCAL" or "REMOTE"
// IMPORTANT: NO HTTP CALL HERE
// ======================================================
void applyEvent(char eventType, const char* source) {
  if (eventType == 'I') {
    inCount++;
    currentCount++;

    DBG_PRINT(">>> ");
    DBG_PRINT(source);
    DBG_PRINTLN(" ENTRY DETECTED");

    setLCDMessage("ENTRY DETECTED");
  }
  else if (eventType == 'O') {
    outCount++;
    if (currentCount > 0) currentCount--;

    DBG_PRINT(">>> ");
    DBG_PRINT(source);
    DBG_PRINTLN(" EXIT DETECTED");

    setLCDMessage("EXIT DETECTED");
  }
  else {
    DBG_PRINTLN("[ERROR] Invalid event type in applyEvent()");
    return;
  }

  DBG_PRINT("Current Inside = ");
  DBG_PRINTLN(currentCount);
  DBG_PRINT("Total IN       = ");
  DBG_PRINTLN(inCount);
  DBG_PRINT("Total OUT      = ");
  DBG_PRINTLN(outCount);
  DBG_PRINTLN("-----------------------------------");

  // Send synced totals to sender (mirror LCD)
  sendSyncToSender();

  // Mark Firebase dirty
  dataChanged = true;

  // Schedule Google Sheets append (deferred to loop)
  googleSheetsPending = true;
}

// ======================================================
// Send FINAL TOTAL COUNTS to Firebase (roomA_2gate)
// ======================================================
bool sendLiveDataToFirebase() {
  if (!firebaseReady) {
    DBG_PRINTLN("[ERROR] Firebase not ready. Skipping update.");
    return false;
  }

  FirebaseJson json;

  // Room metadata
  json.set("deviceId", "roomA_2gate");
  json.set("roomName", "Seminar Hall");
  json.set("gateType", 2);
  json.set("roomCapacity", 40);

  // Live counts
  json.set("currentCount", currentCount);
  json.set("inCount", inCount);
  json.set("outCount", outCount);

  // REAL-TIME gate statuses
  json.set("gate1Status", gate1Status);
  json.set("gate2Status", gate2Status);

  // Device status
  json.set("status", "online");
  json.set("lastUpdateMs", millis());

  DBG_PRINTLN("==================================");
  DBG_PRINTLN("[FIREBASE] Sending synced counts to /devices/roomA_2gate ...");

  bool ok = Firebase.RTDB.updateNode(&fbdo, "/devices/roomA_2gate", &json);

  if (ok) {
    DBG_PRINTLN("[SUCCESS] Firebase updated!");
    DBG_PRINT("currentCount = ");
    DBG_PRINTLN(currentCount);
    DBG_PRINT("inCount      = ");
    DBG_PRINTLN(inCount);
    DBG_PRINT("outCount     = ");
    DBG_PRINTLN(outCount);
    DBG_PRINT("gate1Status  = ");
    DBG_PRINTLN(gate1Status);
    DBG_PRINT("gate2Status  = ");
    DBG_PRINTLN(gate2Status);
  } else {
    DBG_PRINTLN("[ERROR] Firebase update failed.");
    DBG_PRINT("Reason: ");
    DBG_PRINTLN(fbdo.errorReason());
  }

  DBG_PRINTLN("==================================");
  return ok;
}

// ======================================================
// Try Google Sheets update only when pending
// IMPORTANT: Runs in loop() context (safe)
// ======================================================
void handleGoogleSheetsUpdate() {
  if (!googleSheetsPending) return;

  if (WiFi.status() != WL_CONNECTED) {
    DBG_PRINTLN("[GOOGLE SHEETS] WiFi disconnected. Pending retained.");
    return;
  }

  if (millis() - lastGoogleSheetsPush < GOOGLE_SHEETS_MIN_GAP) {
    return;
  }

  if (sendLiveDataToGoogleSheets()) {
    lastGoogleSheetsPush = millis();
    googleSheetsPending = false;
  } else {
    // keep pending true -> retry in next loop
    DBG_PRINTLN("[GOOGLE SHEETS] Will retry...");
  }
}

// ======================================================
// ESP-NOW RECEIVE CALLBACK
// Receive event packet from sender
// IMPORTANT: Keep callback lightweight, no HTTP/Firebase calls
// ======================================================
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataBytes, int len) {
  if (len != sizeof(struct_packet)) {
    DBG_PRINTLN("[ESP-NOW] Invalid packet size received");
    return;
  }

  memcpy(&incomingPacket, incomingDataBytes, sizeof(incomingPacket));

  if (incomingPacket.packetType != 'E') {
    DBG_PRINTLN("[ESP-NOW] Non-event packet ignored on receiver");
    return;
  }

  DBG_PRINTLN("==================================");
  DBG_PRINTLN("[ESP-NOW] Event received from sender");
  DBG_PRINT("Event Type = ");
  DBG_PRINTLN(incomingPacket.eventType);
  DBG_PRINT("Event ID   = ");
  DBG_PRINTLN(incomingPacket.eventId);

  // Gate 2 active immediately when packet arrives
  markGate2Active();

  // Duplicate protection
  if (incomingPacket.eventId <= lastRemoteEventId) {
    DBG_PRINTLN("[ESP-NOW] Duplicate/old event ignored");
    DBG_PRINTLN("==================================");
    return;
  }

  lastRemoteEventId = incomingPacket.eventId;

  if (incomingPacket.eventType == 'I' || incomingPacket.eventType == 'O') {
    applyEvent(incomingPacket.eventType, "REMOTE");
  } else {
    DBG_PRINTLN("[ESP-NOW] Invalid event type");
  }

  DBG_PRINTLN("==================================");
}

// ======================================================
// ESP-NOW Setup
// IMPORTANT: Call AFTER WiFi connect
// ======================================================
void setupEspNow() {
  Serial.println("[INFO] Initializing ESP-NOW...");
  setLCDMessage("ESP-NOW Init...");

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed!");
    setLCDMessage("ESP-NOW FAIL");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, senderMAC, 6);
  peerInfo.channel = 0;   // use current channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERROR] Failed to add sender peer!");
    setLCDMessage("PEER ADD FAIL");
    return;
  }

  Serial.println("[SUCCESS] ESP-NOW Receiver initialized");
  setLCDMessage("ESP-NOW Ready");
}

// ======================================================
// Handle local sensors (non-blocking edge detection)
// ======================================================
void handleLocalSensors() {
  unsigned long now = millis();

  bool s1Triggered = (digitalRead(S1) == SENSOR_TRIGGERED);
  bool s2Triggered = (digitalRead(S2) == SENSOR_TRIGGERED);

  // Rising edge for S1
  if (s1Triggered && !s1PrevState) {
    if (now - lastS1Trigger > SENSOR_DEBOUNCE) {
      DBG_PRINTLN("[SENSOR] S1 Triggered");
      markGate1Active();
      addToSequence('1');
      lastS1Trigger = now;
      processLocalSequence();
    }
  }

  // Rising edge for S2
  if (s2Triggered && !s2PrevState) {
    if (now - lastS2Trigger > SENSOR_DEBOUNCE) {
      DBG_PRINTLN("[SENSOR] S2 Triggered");
      markGate1Active();
      addToSequence('2');
      lastS2Trigger = now;
      processLocalSequence();
    }
  }

  // Save current states
  s1PrevState = s1Triggered;
  s2PrevState = s2Triggered;

  // Sequence timeout
  if (seqCount > 0 && (now - lastSequenceTime > SEQUENCE_TIMEOUT)) {
    DBG_PRINTLN("[TIMEOUT] Local sequence timeout. Resetting sequence.");
    resetSequence();
  }
}

// ======================================================
// LOCAL GATE SEQUENCE PROCESSING
// S1 -> S2 = ENTRY
// S2 -> S1 = EXIT
// ======================================================
void processLocalSequence() {
  if (seqCount < 2) return;

  char first = sequence[0];
  char second = sequence[1];

  if (first == '1' && second == '2') {
    applyEvent('I', "LOCAL");
    resetSequence();
  }
  else if (first == '2' && second == '1') {
    applyEvent('O', "LOCAL");
    resetSequence();
  }
  else {
    DBG_PRINTLN("[LOCAL] Invalid sequence pattern. Keeping latest sensor only.");
    sequence[0] = sequence[1];
    seqCount = 1;
    printSequence();
  }
}

// ======================================================
// WiFi Connect
// ======================================================
void connectToWiFi() {
  Serial.println("==================================");
  Serial.println("[WIFI] Connecting...");
  Serial.print("[WIFI] SSID: ");
  Serial.println(WIFI_SSID);

  setLCDMessage("WiFi Connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retryCount = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retryCount++;

    if (retryCount >= 40) {
      Serial.println();
      Serial.println("[ERROR] WiFi connection failed.");
      setLCDMessage("WiFi FAILED");
      return;
    }
  }

  Serial.println();
  Serial.println("[SUCCESS] WiFi Connected!");
  Serial.print("[INFO] Receiver IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[INFO] Receiver MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("[INFO] WiFi Channel: ");
  Serial.println(WiFi.channel());
  Serial.println("==================================");

  setLCDMessage("WiFi Connected");
}

// ======================================================
// Firebase Setup (Anonymous Auth)
// ======================================================
void setupFirebase() {
  Serial.println("[INFO] Initializing Firebase...");
  setLCDMessage("Firebase Init...");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  // Anonymous Sign In
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("[SUCCESS] Firebase anonymous auth OK");
  } else {
    Serial.print("[ERROR] Firebase signUp failed: ");
    Serial.println(config.signer.signupError.message.c_str());
    setLCDMessage("Firebase Auth FAIL");
    firebaseReady = false;
    return;
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("[INFO] Waiting for Firebase authentication...");

  unsigned long startTime = millis();
  while (!Firebase.ready() && millis() - startTime < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (Firebase.ready()) {
    firebaseReady = true;
    Serial.println();
    Serial.println("[SUCCESS] Firebase is ready!");
    setLCDMessage("Firebase Ready");
  } else {
    Serial.println();
    Serial.println("[ERROR] Firebase not ready.");
    setLCDMessage("Firebase FAIL");
    firebaseReady = false;
  }
}

// ======================================================
// Try Firebase update only when data changed
// NOTE: Google Sheets is NOT called here
// ======================================================
void handleFirebaseUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    DBG_PRINTLN("[WARNING] WiFi disconnected.");
    return;
  }

  if (!firebaseReady) {
    return;
  }

  // Only push when data changed and minimum gap passed
  if (dataChanged && (millis() - lastFirebasePush >= FIREBASE_MIN_GAP)) {
    if (sendLiveDataToFirebase()) {
      lastFirebasePush = millis();
      dataChanged = false;
    }
  }
}

// ======================================================
// Setup
// ======================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==================================================");
  Serial.println(" RECEIVER = GATE 1 + FIREBASE + ESP-NOW + LOCAL ");
  Serial.println("==================================================");

  // LCD INIT
  Wire.begin(21, 22);
  int status = lcd.begin(20, 4);

  if (status) {
    Serial.print("LCD init failed, status = ");
    Serial.println(status);
    while (1);
  }

  lcd.backlight();
  setLCDMessage("LCD OK");

  pinMode(S1, INPUT_PULLUP);
  pinMode(S2, INPUT_PULLUP);

  connectToWiFi();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[FINAL] WiFi not connected. Stopping.");
    setLCDMessage("SYSTEM STOPPED");
    return;
  }

  setupEspNow();
  setupFirebase();

  // Initial Firebase push only
  if (firebaseReady) {
    dataChanged = true;
    handleFirebaseUpdate();
  }

  // Initial sync to sender so both LCDs start same
  sendSyncToSender();

  setLCDMessage("READY");

  Serial.println();
  Serial.println("[READY] Receiver Ready");
  Serial.println("[LOCAL]  S1->S2 = ENTRY, S2->S1 = EXIT");
  Serial.println("[REMOTE] 'I' = ENTRY, 'O' = EXIT");
  Serial.println("[FIREBASE] Path = /devices/roomA_2gate");
  Serial.println("[GOOGLE SHEETS] Row append on ENTRY/EXIT event (LOCAL + REMOTE)");
  Serial.println("[SYNC] Receiver is single source of truth");
  Serial.println();
}

// ======================================================
// Main Loop
// ======================================================
void loop() {
  handleLocalSensors();         // Gate 1 local sensors
  handleGateStatusTimeouts();   // Auto set active -> inactive
  handleFirebaseUpdate();       // Push only when changed
  handleGoogleSheetsUpdate();   // Deferred Google Sheets append (SAFE)
  handleLCDMessageTimeout();    // Restore LCD READY after temporary msg
  delay(5);
}