#pragma once
#include <Arduino.h>
#include <BluetoothSerial.h>
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "config.h"
#include "system_utils.h"
#include "ap_mode.h"

// ================= BLUETOOTH MODULE =================
inline BluetoothSerial SerialBT;

// Bluetooth Commands
inline const String BT_GET_DATA = "GET_DATA";
inline const String BT_PING = "PING";
inline const String BT_GET_INFO = "GET_INFO";
inline const String BT_CLAIM = "CLAIM";
inline const String BT_CONFIGURE = "CONFIGURE";
inline const String BT_GET_WIFI_STATUS = "GET_WIFI_STATUS";
inline const String BT_RESET = "RESET";

inline void btSend(const String &msg) {
  SerialBT.println(msg);
}

// SPP Status & Connection Callback
inline void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    esp_bredr_tx_power_set(BT_TX_POWER_MIN, BT_TX_POWER_MAX);

    int dropped = 0;
    while (dropped < 2048 && SerialBT.available()) {
      SerialBT.read();
      dropped++;
    }
    btResetParser = true;
    btConnected = true;
    lastBtActivityTime = millis();
    btSessionStart = millis();
    Serial.printf("[Bluetooth] Client connected (status=%d, free heap=%u, TX=+9dBm)\n",
                  (int)param->srv_open.status, (unsigned)ESP.getFreeHeap());
    if (dropped > 0) {
      Serial.printf("[Bluetooth] Dropped %d stale byte(s) from previous session\n", dropped);
    }
  } else if (event == ESP_SPP_CLOSE_EVT) {
    btConnected = false;
    btResetParser = true;
    lastBtActivityTime = millis();
    Serial.printf("[Bluetooth] Client disconnected: session lasted %lu ms (status=%d, free heap=%u)\n",
                  millis() - btSessionStart, (int)param->close.status, (unsigned)ESP.getFreeHeap());
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
  }
}

// Start Bluetooth SPP Stack
inline void startBluetoothService() {
  if (btRunning) {
    esp_bredr_tx_power_set(BT_TX_POWER_MIN, BT_TX_POWER_MAX);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    return;
  }

  String bluetoothName = "SiliGuide_" + getShortHardwareMac();
  SerialBT.register_callback(btCallback);

  bool ok = false;
  for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
    ok = SerialBT.begin(bluetoothName.c_str());
    if (!ok) {
      Serial.printf("[Bluetooth] begin() failed attempt %d/3, retrying...\n", attempt);
      SerialBT.end();
      delay(200);
    }
  }

  if (ok) {
    btRunning = true;
    esp_bredr_tx_power_set(BT_TX_POWER_MIN, BT_TX_POWER_MAX);
    esp_coex_preference_set(BT_COEX_PREFERENCE);
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    Serial.println("[Bluetooth] Started successfully: " + bluetoothName);
  } else {
    Serial.println(F("[Bluetooth] FAILED to start dynamically! Recovering via restart..."));
    delay(400);
    ESP.restart();
  }
}

// Stop Bluetooth Stack to free radio for 100% WiFi
inline void stopBluetoothService() {
  if (!btRunning) return;

  Serial.println(F("[Bluetooth] Stopping Bluetooth service..."));
  btConnected = false;
  btResetParser = true;
  SerialBT.end();
  btRunning = false;
  Serial.println(F("[Bluetooth] Stopped. Radio resources fully dedicated to Wi-Fi."));
}

// Switchover to Bluetooth + LAN Mode
inline void switchToBluetoothMode() {
  Serial.println(F("\n======================================================="));
  Serial.println(F("[MODE SWITCH] >>> SWITCHOVER: BLUETOOTH + LAN MODE ACTIVE <<<"));
  Serial.println(F("======================================================="));

  // 1. Immediately turn OFF SoftAP so the AP disappears completely
  WiFi.softAPdisconnect(true);
  Serial.println(F("[MODE SWITCH] ESP32 AP turned OFF (SSID hidden from air)."));

  // 2. Set WiFi mode to Station only
  WiFi.mode(WIFI_STA);
  applyWiFiBandwidth();
  applyWiFiRadioTuning();

  // 3. Start Bluetooth service
  startBluetoothService();

  // 4. In LAN mode, auto-connect to saved router if credentials exist
  String sSsid, sPass;
  int ch = 0;
  if (loadWiFi(sSsid, sPass, ch)) {
    Serial.printf("[LAN Mode] Connecting to saved router [%s] on Channel %d...\n", sSsid.c_str(), ch);
    connectToWiFi(sSsid, sPass);
  } else {
    Serial.println(F("[LAN Mode] No saved WiFi credentials. Waiting for Bluetooth CONFIGURE."));
  }
}

// Bluetooth Command Dispatcher
inline void processBluetoothCommand(String rawCommand) {
  rawCommand.trim();

  String command = rawCommand;
  String payload = "";

  int firstColon = rawCommand.indexOf(':');
  if (firstColon != -1) {
    command = rawCommand.substring(0, firstColon);
    payload = rawCommand.substring(firstColon + 1);
  }
  command.toUpperCase();

  Serial.println("[Bluetooth CMD] " + command);

  if (command == BT_GET_INFO) {
    btSend(createInfoJson());
    return;
  }
  if (command == BT_PING) {
    btSend("PONG");
    return;
  }
  if (command == BT_GET_WIFI_STATUS) {
    btSend(createWifiStatusJson());
    return;
  }

  if (command == BT_CLAIM) {
    if (payload == "" || payload != pairingToken) {
      btSend("{\"error\":\"invalid_pairing_token\"}");
      return;
    }

    preferences.begin("pair", false);
    preferences.putBool("claimed", true);
    preferences.end();
    deviceClaimed = true;

    btSend("{\"status\":\"claimed_successfully\"}");
    Serial.println(F("== DEVICE LOCKED: ENTERING SECURE OPERATIONAL MODE =="));
    return;
  }

  if (command == BT_GET_DATA) {
    String jsonPayload = createSensorJson();
    if (deviceClaimed) {
      btSend(encryptAESGCM(jsonPayload));
    } else {
      btSend(jsonPayload);
    }
    return;
  } else if (command == BT_CONFIGURE) {
    if (!deviceClaimed) {
      btSend("{\"error\":\"unauthorized_bluetooth_access\"}");
      return;
    }

    String decryptedJson = decryptAESGCM(payload);
    if (decryptedJson == "") {
      btSend("{\"error\":\"decryption_failed\"}");
      return;
    }

    String ssid = "", password = "";
    if (parseWiFiCredentials(decryptedJson, ssid, password)) {
      saveWiFi(ssid, password);
      btSend("{\"status\":\"connecting\"}");
      delay(200);
      connectToWiFi(ssid, password);
    } else {
      btSend("{\"error\":\"invalid_json_format\"}");
    }
  } else if (command == BT_RESET) {
    if (!deviceClaimed) {
      btSend("{\"error\":\"unauthorized_bluetooth_access\"}");
      return;
    }

    String decryptedJson = decryptAESGCM(payload);
    if (decryptedJson == "") {
      btSend("{\"error\":\"unauthorized_wipe_request\"}");
      return;
    }

    btSend("{\"status\":\"resetting\"}");
    delay(500);
    softReset();
    ESP.restart();
  } else if (command == "CONNECT_WIFI") {
    String ssid, pass;
    if (loadWiFi(ssid, pass)) {
      btSend("{\"status\":\"connecting\"}");
      delay(200);
      connectToWiFi(ssid, pass);
    } else {
      btSend("{\"error\":\"no_saved_wifi\"}");
    }
  } else if (command == "DISCONNECT_WIFI") {
    WiFi.disconnect(false, false);
    isTryingSTA = false;
    wifiDisconnectTime = millis();
    lastRetryTime = millis();
    btSend("{\"status\":\"wifi_disconnected\"}");
  } else {
    Serial.println("[Bluetooth UNKNOWN CMD] " + rawCommand);
    btSend("{\"error\":\"unknown_command\"}");
  }
}

// Non-blocking Bluetooth Stream Parser
inline void handleBluetooth() {
  static String btCommandBuffer = "";
  static unsigned long lastBtCharTime = 0;

  if (btResetParser) {
    btResetParser = false;
    btCommandBuffer = "";
  }

  if (!btConnected && btCommandBuffer.length() > 0) {
    btCommandBuffer = "";
  }

  while (SerialBT.available()) {
    char c = (char)SerialBT.read();
    lastBtCharTime = millis();
    lastBtActivityTime = millis();
    if (c == '\n' || c == '\r') {
      if (btCommandBuffer.length() > 0) {
        processBluetoothCommand(btCommandBuffer);
        btCommandBuffer = "";
      }
    } else {
      if (btCommandBuffer.length() < 1024) {
        btCommandBuffer += c;
      }
    }
  }

  unsigned long btQuietNeeded = (btCommandBuffer.indexOf(':') >= 0) ? 300UL : 40UL;
  if (btCommandBuffer.length() > 0 && (millis() - lastBtCharTime > btQuietNeeded)) {
    processBluetoothCommand(btCommandBuffer);
    btCommandBuffer = "";
  }
}

// Visibility Watchdog after WiFi transitions
inline void handleBluetoothWatchdog() {
  if (btRunning && btRefreshPending && !btConnected && (millis() >= btRefreshAt)) {
    btRefreshPending = false;
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    Serial.println(F("[Bluetooth] SPP visibility refreshed after WiFi transition."));
  }
}
