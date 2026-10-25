#include "config.h"
#include "dht22_sensor.h"
#include "bh1750_sensor.h"
#include "soil_sensor.h"
#include "system_utils.h"
#include "ap_mode.h"
#include "bluetooth_mode.h"

// Non-blocking sensor polling timer (1-second interval)
unsigned long lastSensorRead = 0;

// ================= DIAGNOSTIC HEARTBEAT =================
#if DEBUG_HEARTBEAT
inline void handleHeartbeat() {
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 10000UL) {
    lastHeartbeat = millis();
    Serial.printf("[HB] up=%lus heap=%u minHeap=%u maxBlock=%u "
                  "loopStackFree=%u mode=%s btRun=%d btConn=%d wifi=%d ch=%d\n",
                  millis() / 1000UL, (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)uxTaskGetStackHighWaterMark(NULL),
                  btModeActive ? "BT+LAN" : "AP_ONLY",
                  (int)btRunning, (int)btConnected,
                  (int)WiFi.status(), (int)WiFi.channel());
  }
}
#else
inline void handleHeartbeat() {}
#endif

// ================= SETUP =================
void setup() {
  // Reclaim unused BLE controller memory back to system heap (~25-30 KB recovered)
  esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

#if DISABLE_BROWNOUT_DETECTOR
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif

  Serial.begin(115200);
  Serial.printf("\n[BOOT] reset reason = %s | brownout detector %s | TEST_BT_ONLY=%d\n",
                resetReasonStr(esp_reset_reason()),
                DISABLE_BROWNOUT_DETECTOR ? "OFF" : "ON", (int)TEST_BT_ONLY);

  // Buzzer: Beeps if a hardware recovery reset was triggered
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  beepIfResetPending();

  // Hardware Reset Button (Hold for 5s to wipe NVS credentials)
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // Pre-boot Wi-Fi scan: caches nearby router channels once at boot
  scanBootNetworks();

#if TEST_BT_ONLY
  WiFi.mode(WIFI_OFF);
  Serial.println(F("*** TEST_BT_ONLY: WiFi radio is OFF, only Bluetooth is running ***"));
#endif

  // Hardware Mode Switch (Pin 33)
  pinMode(MODE_SWITCH_PIN, INPUT_PULLUP);
  delay(10); // Let internal pullup stabilize
  btModeActive = readModeSwitch();

  if (btModeActive) {
    Serial.println(F("[BOOT] MODE SWITCH: ON (Pin 33 LOW) -> Starting in BLUETOOTH + LAN MODE"));
    startBluetoothService();
  } else {
    Serial.println(F("[BOOT] MODE SWITCH: OFF (Pin 33 HIGH) -> Starting in AP MODE ONLY (Bluetooth is OFF)"));
    btRunning = false;
  }

  randomSeed(esp_random());
  loadOrCreateToken();
  setupWiFiEvents();

  // Setup Sensor Hardware Modules
  setupBH1750();
  setupDHT22();
  setupSoilSensor();

  // Initial Radio configuration based on Mode Switch
  int bootCh = 1;
  String sSsid, sPass;
  if (loadWiFi(sSsid, sPass, bootCh)) {
    if (bootCh < 1 || bootCh > 13) bootCh = 1;
  } else {
    // Check if known network is in nearby boot cache
    for (int i = 0; i < bootNetworkCount; i++) {
      String trimmed = bootNetworks[i].ssid;
      trimmed.trim();
      if (trimmed.equalsIgnoreCase("TEMPORADO")) {
        bootCh = bootNetworks[i].channel;
        Serial.printf("Boot AP: Matched TEMPORADO from cache -> starting on Channel %d\n", bootCh);
        break;
      }
    }
  }

#if !TEST_BT_ONLY
  if (!btModeActive) {
    // AP Mode: Start SoftAP on boot channel
    startAccessPoint(bootCh);
  } else {
    // Bluetooth / LAN Mode: SoftAP OFF and prepare STA for router
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    applyWiFiBandwidth();
    applyWiFiRadioTuning();
    Serial.println(F("[MODE SWITCH] SoftAP is OFF — Bluetooth + LAN Mode active."));
  }
#endif

  lastRetryTime = millis();

  // In Bluetooth / LAN mode, auto-connect to router if credentials exist
  if (btModeActive && sSsid.length() > 0) {
    Serial.println("Boot LAN Mode: Auto-connecting to saved router [" + sSsid +
                   "] on Channel " + String(bootCh));
    connectToWiFi(sSsid, sPass);
  }

  // Setup HTTP WebServer API routes
  setupWebServer();

  Serial.println(F("=== DEVICE READY ==="));
  Serial.println("SSID: " + apSSID);
  Serial.println("Token: " + pairingToken);
}

// ================= MAIN LOOP =================
void loop() {
  // 1. Process incoming HTTP WebServer requests
  handleWebServer();

  // 2. Process incoming Bluetooth stream commands
  if (btRunning) {
    handleBluetooth();
  }

  // 3. Hot-check Hardware Mode Switch (Pin 33)
  bool targetMode = false;
  if (checkModeSwitchChanged(targetMode)) {
    btModeActive = targetMode;
    if (btModeActive) {
      switchToBluetoothMode();
    } else {
      switchToAPMode();
    }
  }

  // 4. Bluetooth visibility watchdog
  handleBluetoothWatchdog();

  // 5. Wi-Fi deferred event tasks & automatic LAN reconnect engine
  handleWiFiReconnectEngine();

  // 6. Check hardware recovery reset button (Pin 13)
  handleResetButton();

  // 7. Sensor Sampling & Real-Time Data Refresh (1-Second Interval, No Excess Delay!)
  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = millis();

    readBH1750();
    readSoilSensor();
    readDHT22();
  }

  // 8. Diagnostic heartbeat (every 10 seconds)
  handleHeartbeat();

  delay(1);
}