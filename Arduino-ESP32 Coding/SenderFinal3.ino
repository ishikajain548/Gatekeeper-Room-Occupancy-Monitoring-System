#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

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
// GATE 2 SENSORS
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
// Timing Settings
// ======================================================
const unsigned long SENSOR_DEBOUNCE = 120;
const unsigned long SEQUENCE_TIMEOUT = 2000;

// ======================================================
// Sensor State Tracking (Non-blocking)
// ======================================================
bool s1PrevState = false;
bool s2PrevState = false;

unsigned long lastS1Trigger = 0;
unsigned long lastS2Trigger = 0;

// ======================================================
// Sequence Buffer
// We only need last 2 sensor events for direction
// '1' then '2' = ENTRY
// '2' then '1' = EXIT
// ======================================================
char sequence[2];
int seqCount = 0;
unsigned long lastSequenceTime = 0;

// ======================================================
// FINAL COUNTS (ONLY FROM RECEIVER SYNC)
// Sender no longer owns truth
// ======================================================
volatile int currentCount = 0;
volatile int inCount = 0;
volatile int outCount = 0;

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

struct_packet outgoingPacket;
struct_packet incomingPacket;

// ======================================================
// Stable Event Counter
// ======================================================
unsigned long eventCounter = 0;

// ======================================================
// REPLACE WITH RECEIVER ESP32 MAC ADDRESS
// Example: 1C:C3:AB:B4:5A:FC
// ======================================================
uint8_t receiverMAC[] = {0x1C, 0xC3, 0xAB, 0xB4, 0x5A, 0xFC};

// ======================================================
// LCD UPDATE FUNCTION
// ======================================================
void updateLCD(const char* statusMsg = "READY") {
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
// WiFi Connect (recommended for same channel)
// ======================================================
void connectToWiFi() {
  Serial.println("[WIFI] Connecting sender to hotspot...");
  updateLCD("WiFi Connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retryCount = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    retryCount++;

    if (retryCount >= 30) {
      Serial.println();
      Serial.println("[WARNING] Sender WiFi connect failed.");
      Serial.println("[WARNING] ESP-NOW may fail if channel mismatched.");
      updateLCD("WiFi FAILED");
      return;
    }
  }

  Serial.println();
  Serial.println("[SUCCESS] Sender WiFi Connected!");
  Serial.print("[INFO] Sender IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[INFO] Sender MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("[INFO] Sender WiFi Channel: ");
  Serial.println(WiFi.channel());

  updateLCD("WiFi Connected");
}

// ======================================================
// ESP-NOW Send Callback (Core 3.x)
// ======================================================
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  DBG_PRINT("[ESP-NOW] Send Status: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    DBG_PRINTLN("SUCCESS");
  } else {
    DBG_PRINTLN("FAILED");
  }
  DBG_PRINTLN("-----------------------------------");
}

// ======================================================
// ESP-NOW Receive Callback (Sync from Receiver)
// ======================================================
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingDataBytes, int len) {
  if (len != sizeof(struct_packet)) {
    DBG_PRINTLN("[ESP-NOW] Invalid packet size received");
    return;
  }

  memcpy(&incomingPacket, incomingDataBytes, sizeof(incomingPacket));

  if (incomingPacket.packetType != 'S') {
    DBG_PRINTLN("[ESP-NOW] Non-sync packet ignored on sender");
    return;
  }

  currentCount = incomingPacket.currentCount;
  inCount = incomingPacket.inCount;
  outCount = incomingPacket.outCount;

  DBG_PRINTLN("==================================");
  DBG_PRINTLN("[SYNC] Synced counts received from receiver");
  DBG_PRINT("Current = ");
  DBG_PRINTLN(currentCount);
  DBG_PRINT("IN      = ");
  DBG_PRINTLN(inCount);
  DBG_PRINT("OUT     = ");
  DBG_PRINTLN(outCount);
  DBG_PRINTLN("==================================");

  updateLCD("SYNC OK");
}

// ======================================================
// Send Event to Receiver
// ======================================================
void sendEventToReceiver(char eventType) {
  outgoingPacket.packetType = 'E';
  outgoingPacket.eventType = eventType;
  outgoingPacket.eventId = ++eventCounter;

  // not used in event packet, but keep clean
  outgoingPacket.currentCount = 0;
  outgoingPacket.inCount = 0;
  outgoingPacket.outCount = 0;

  esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)&outgoingPacket, sizeof(outgoingPacket));

  DBG_PRINT("[SEND] Event = ");
  DBG_PRINTLN(eventType);
  DBG_PRINT("[SEND] Event ID = ");
  DBG_PRINTLN(outgoingPacket.eventId);

  if (result == ESP_OK) {
    DBG_PRINTLN("[ESP-NOW] Event packet queued successfully");
    updateLCD(eventType == 'I' ? "ENTRY SENT" : "EXIT SENT");
  } else {
    DBG_PRINT("[ESP-NOW] Send Error Code: ");
    DBG_PRINTLN(result);
    updateLCD("SEND FAIL");
  }
}

// ======================================================
// Process Gate Sequence
// S1 -> S2 = ENTRY
// S2 -> S1 = EXIT
// ======================================================
void processSequence() {
  if (seqCount < 2) return;

  char first = sequence[0];
  char second = sequence[1];

  // ENTRY
  if (first == '1' && second == '2') {
    DBG_PRINTLN("==================================");
    DBG_PRINTLN("[GATE 2] ENTRY DETECTED");
    DBG_PRINTLN("Sending event to receiver...");
    DBG_PRINTLN("==================================");

    sendEventToReceiver('I');
    resetSequence();
  }
  // EXIT
  else if (first == '2' && second == '1') {
    DBG_PRINTLN("==================================");
    DBG_PRINTLN("[GATE 2] EXIT DETECTED");
    DBG_PRINTLN("Sending event to receiver...");
    DBG_PRINTLN("==================================");

    sendEventToReceiver('O');
    resetSequence();
  }
  // Invalid / duplicate
  else {
    DBG_PRINTLN("[WARNING] Invalid sequence pattern. Keeping latest sensor only.");
    sequence[0] = sequence[1];
    seqCount = 1;
    printSequence();
  }
}

// ======================================================
// ESP-NOW Setup
// IMPORTANT: Run after WiFi connect
// ======================================================
void setupEspNow() {
  Serial.println("[INFO] Initializing ESP-NOW...");
  updateLCD("ESP-NOW Init...");

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed!");
    updateLCD("ESP-NOW FAIL");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;     // use current channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERROR] Failed to add receiver peer!");
    updateLCD("PEER ADD FAIL");
    return;
  }

  Serial.println("[SUCCESS] ESP-NOW initialized and receiver added.");
  updateLCD("ESP-NOW Ready");
}

// ======================================================
// Read Sensors (Non-blocking edge detection)
// ======================================================
void handleSensors() {
  unsigned long now = millis();

  bool s1Triggered = (digitalRead(S1) == SENSOR_TRIGGERED);
  bool s2Triggered = (digitalRead(S2) == SENSOR_TRIGGERED);

  // Rising edge for S1 (inactive -> active)
  if (s1Triggered && !s1PrevState) {
    if (now - lastS1Trigger > SENSOR_DEBOUNCE) {
      DBG_PRINTLN("[SENSOR] S1 Triggered");
      addToSequence('1');
      lastS1Trigger = now;
      processSequence();
    }
  }

  // Rising edge for S2
  if (s2Triggered && !s2PrevState) {
    if (now - lastS2Trigger > SENSOR_DEBOUNCE) {
      DBG_PRINTLN("[SENSOR] S2 Triggered");
      addToSequence('2');
      lastS2Trigger = now;
      processSequence();
    }
  }

  // Save current state for next loop
  s1PrevState = s1Triggered;
  s2PrevState = s2Triggered;

  // Sequence timeout
  if (seqCount > 0 && (now - lastSequenceTime > SEQUENCE_TIMEOUT)) {
    DBG_PRINTLN("[TIMEOUT] Sequence timeout. Resetting sequence.");
    resetSequence();
  }
}

// ======================================================
// Setup
// ======================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==========================================");
  Serial.println(" SENDER = GATE 2 + ESP-NOW + LCD (SLAVE) ");
  Serial.println("==========================================");

  // LCD INIT
  Wire.begin(21, 22);
  int status = lcd.begin(20, 4);

  if (status) {
    Serial.print("LCD init failed, status = ");
    Serial.println(status);
    while (1);
  }

  lcd.backlight();
  updateLCD("LCD OK");

  pinMode(S1, INPUT_PULLUP);
  pinMode(S2, INPUT_PULLUP);

  connectToWiFi();
  setupEspNow();

  updateLCD("WAITING SYNC");

  Serial.println("[READY] Sender Ready");
  Serial.println("[INFO] Sender sends events only, receiver owns final counts");
}

// ======================================================
// Loop
// ======================================================
void loop() {
  handleSensors();
  delay(5);
}