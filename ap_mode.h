#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include "config.h"
#include "system_utils.h"

// ================= AP MODE & WEB SERVER MODULE =================
inline WebServer server(80);
inline String apSSID;
inline const char *apPASS = "Sili_D8CBB0mQnVBs52";

// Wi-Fi connection states
inline bool isTryingSTA = false;
inline unsigned long wifiConnectStart = 0;
inline int wifiRetryCount = 0;
inline unsigned long wifiDisconnectTime = 0;
inline unsigned long lastRetryTime = 0;

// mDNS & Deferred events
inline bool mdnsStarted = false;
inline volatile int pendingChannelSave = 0;
inline volatile bool pendingMdnsStart = false;

// Boot network channel cache
struct BootNetwork {
  String ssid;
  int channel;
};
inline const int MAX_BOOT_NETWORKS = 16;
inline BootNetwork bootNetworks[MAX_BOOT_NETWORKS];
inline int bootNetworkCount = 0;

// Forward declaration of Bluetooth control
void stopBluetoothService();

// ================= WIFI RADIO COEXISTENCE & POWER TUNING =================
inline void applyWiFiRadioTuning() {
  if (btModeActive) {
    // BLUETOOTH + LAN MODE:
    // 1. Max WiFi TX power (+19.5 dBm) for maximum range reaching the router
    WiFi.setTxPower(WIFI_STA_TX_POWER);
    // 2. Minimum modem-sleep allows ESP32 PTA coexistence arbiter to interleave BT packets cleanly
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    // 3. Balanced radio scheduling ensures both WiFi and Bluetooth stay fast & stable
    esp_coex_preference_set(BT_COEX_PREFERENCE);
    // 4. Lock Bluetooth Classic to maximum +9 dBm
    esp_bredr_tx_power_set(BT_TX_POWER_MIN, BT_TX_POWER_MAX);
  } else {
    // AP MODE ONLY (Bluetooth is completely OFF):
    // 1. Disable modem-sleep (WIFI_PS_NONE) for zero packet latency & instant HTTP responses
    esp_wifi_set_ps(WIFI_PS_NONE);
    // 2. Full +19.5 dBm maximum TX power for maximum SoftAP range and strongest signal bars
    WiFi.setTxPower(WIFI_AP_TX_POWER);
    // 3. 100% RF radio priority devoted to WiFi
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
  }
}

inline void applyWiFiBandwidth() {
#if WIFI_FORCE_HT20
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
#endif
}

// Start ESP32 SoftAP
inline void startAccessPoint(int channel = 1) {
  if (btModeActive) {
    Serial.println(F("[AP] Cannot start SoftAP while Bluetooth Mode is active."));
    return;
  }
  WiFi.mode(WIFI_AP);

  apSSID = "SiliGuide_IoT_" + getShortHardwareMac();
  WiFi.softAP(apSSID.c_str(), apPASS, channel);
  applyWiFiRadioTuning();
  applyWiFiBandwidth();

  Serial.println("[AP] Started: " + apSSID + " on Channel " + String(channel));
  Serial.println("[AP] IP Address: " + WiFi.softAPIP().toString());
}

// Associate with Wi-Fi router (LAN Mode / STA)
inline void connectToWiFi(String ssid, String password) {
  if (TEST_BT_ONLY) {
    Serial.println(F("WiFi: TEST_BT_ONLY is ON - WiFi request ignored."));
    return;
  }
  ssid.trim();
  password.trim();

  if (ssid.length() == 0) {
    Serial.println(F("WiFi: SSID is empty, aborting."));
    return;
  }

  // Deduplication: If already connected to this network, do nothing
  if (WiFi.status() == WL_CONNECTED &&
      (WiFi.SSID() == ssid || WiFi.SSID().startsWith(ssid))) {
    Serial.println("WiFi: Already connected to [" + WiFi.SSID() + "], ignoring duplicate CONFIGURE.");
    return;
  }

  // Deduplication: If already in the middle of connecting, do not tear down
  if (isTryingSTA && (millis() - wifiConnectStart < WIFI_TIMEOUT)) {
    Serial.println(F("WiFi: Connection attempt already in progress, ignoring duplicate CONFIGURE."));
    return;
  }

  WiFi.setAutoReconnect(false);
  if (btModeActive) {
    WiFi.mode(WIFI_STA); // LAN Mode: Station ONLY, NO SoftAP!
  } else {
    WiFi.mode(WIFI_AP_STA);
  }
  esp_wifi_set_country_code("PH", true);

  Serial.println("\n--- WiFi Connecting to: [" + ssid + "] ---");

  int targetChannel = 0;
  String exactSSID = ssid;
  String searchTarget = ssid;
  searchTarget.trim();

  // 1. Check saved Wi-Fi in NVS first
  int savedCh = 0;
  String savedSSID = "", savedPass = "";
  if (loadWiFi(savedSSID, savedPass, savedCh)) {
    String savedTrimmed = savedSSID;
    savedTrimmed.trim();
    if (savedCh > 0 &&
        (savedSSID == ssid || savedTrimmed.equalsIgnoreCase(searchTarget))) {
      targetChannel = savedCh;
      exactSSID = savedSSID;
      Serial.printf("WiFi: Found in NVS -> [%s] on Channel %d\n", exactSSID.c_str(), targetChannel);
    }
  }

  // 2. If not in NVS, check boot-time network cache
  if (targetChannel == 0) {
    for (int i = 0; i < bootNetworkCount; i++) {
      String cachedTrimmed = bootNetworks[i].ssid;
      cachedTrimmed.trim();
      if (bootNetworks[i].ssid == ssid || cachedTrimmed.equalsIgnoreCase(searchTarget)) {
        targetChannel = bootNetworks[i].channel;
        exactSSID = bootNetworks[i].ssid;
        Serial.printf("WiFi: Matched from boot cache -> [%s] on Channel %d\n", exactSSID.c_str(), targetChannel);
        break;
      }
    }
  }

  // Save the matched network to NVS
  saveWiFi(exactSSID, password, targetChannel);
  applyWiFiBandwidth();

  // 3. Align SoftAP only if channel is different (AP Mode only)
  if (!btModeActive) {
    if (targetChannel > 0 && WiFi.channel() != targetChannel) {
      Serial.printf("WiFi: Aligning SoftAP to Channel %d...\n", targetChannel);
      WiFi.softAP(apSSID.c_str(), apPASS, targetChannel);
      delay(100);
    }
  } else {
    WiFi.softAPdisconnect(true);
  }

  if (targetChannel > 0) {
    Serial.printf("WiFi: Associating STA to [%s] on Channel %d...\n", exactSSID.c_str(), targetChannel);
    if (password.length() > 0) {
      WiFi.begin(exactSSID.c_str(), password.c_str(), targetChannel);
    } else {
      WiFi.begin(exactSSID.c_str(), NULL, targetChannel);
    }
  } else {
    Serial.printf("WiFi: Direct connect to [%s]...\n", exactSSID.c_str());
    WiFi.setScanMethod(WIFI_FAST_SCAN);
    if (password.length() > 0) {
      WiFi.begin(exactSSID.c_str(), password.c_str());
    } else {
      WiFi.begin(exactSSID.c_str());
    }
  }
  Serial.println(F("----------------------------------------\n"));

  applyWiFiRadioTuning();
  isTryingSTA = true;
  wifiRetryCount = 0;
  wifiConnectStart = millis();
}

// Switchover to AP Mode Only
inline void switchToAPMode() {
  Serial.println(F("\n======================================================="));
  Serial.println(F("[MODE SWITCH] >>> SWITCHOVER: AP MODE ONLY ACTIVE <<<"));
  Serial.println(F("======================================================="));

  // 1. Stop Bluetooth so AP has 100% antenna access
  stopBluetoothService();

  // 2. Disconnect STA Wi-Fi
  WiFi.disconnect(false, false);
  isTryingSTA = false;

  // 3. Start SoftAP
  startAccessPoint(1);
  Serial.println(F("[MODE SWITCH] AP Mode active (SiliGuide_IoT_...). Bluetooth is OFF."));
}

// mDNS service
inline void startMDNS() {
  String host = "siliguide-" + getShortHardwareMac();
  MDNS.end();
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS started: http://" + host + ".local");
  } else {
    Serial.println(F("mDNS FAILED"));
  }
}

// Quick pre-boot Wi-Fi scan: caches nearby router channels once at boot
inline void scanBootNetworks() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_country_code("PH", true);
  Serial.println(F("Wi-Fi Boot Scan: Caching nearby router channels..."));
  int n = WiFi.scanNetworks(false, false, false, 80);
  bootNetworkCount = (n > 0) ? min(n, MAX_BOOT_NETWORKS) : 0;
  for (int i = 0; i < bootNetworkCount; i++) {
    bootNetworks[i].ssid = WiFi.SSID(i);
    bootNetworks[i].channel = WiFi.channel(i);
    Serial.printf("  [Cached] %s (ch=%d)\n", bootNetworks[i].ssid.c_str(), bootNetworks[i].channel);
  }
  WiFi.scanDelete();
}

// ================= HTTP JSON PAYLOAD BUILDERS =================
inline String createInfoJson() {
  static char buf[512];
  const char *mode = (WiFi.status() == WL_CONNECTED) ? "LAN" : "AP";
  String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  String macStr = getHardwareMac();
  const char *tokenVal = deviceClaimed ? "LOCKED_BY_OWNER" : pairingToken.c_str();

  snprintf(buf, sizeof(buf),
           "{\"device\":\"SiliGuide\","
           "\"hardwareMac\":\"%s\","
           "\"claimed\":%s,"
           "\"token\":\"%s\","
           "\"mode\":\"%s\","
           "\"ip\":\"%s\","
           "\"apSsid\":\"%s\","
           "\"firmware\":\"1.0.0\"}",
           macStr.c_str(), deviceClaimed ? "true" : "false", tokenVal, mode,
           ipStr.c_str(), apSSID.c_str());
  return String(buf);
}

inline String createWifiStatusJson() {
  static char buf[256];
  const char *status = (WiFi.status() == WL_CONNECTED)
                           ? "connected"
                           : (isTryingSTA ? "connecting" : "disconnected");
  snprintf(buf, sizeof(buf),
           "{\"connected\":%s,"
           "\"statusCode\":%d,"
           "\"status\":\"%s\","
           "\"ssid\":\"%s\","
           "\"ip\":\"%s\"}",
           (WiFi.status() == WL_CONNECTED) ? "true" : "false",
           (int)WiFi.status(), status, WiFi.SSID().c_str(),
           WiFi.localIP().toString().c_str());
  return String(buf);
}

// ================= HTTP API ENDPOINTS =================
inline bool isAuthorized() {
  return server.hasArg("token") && server.arg("token") == pairingToken;
}

inline void handleInfo() {
  Serial.println("[HTTP GET] /info from " + server.client().remoteIP().toString());
  server.send(200, "application/json", createInfoJson());
}

inline void handleWifiStatus() {
  Serial.println("[HTTP GET] /wifi-status from " + server.client().remoteIP().toString());
  server.send(200, "application/json", createWifiStatusJson());
}

inline void handleData() {
  Serial.println("[HTTP GET] /data from " + server.client().remoteIP().toString());
  String jsonPayload = createSensorJson();

  if (deviceClaimed) {
    String scrambled = encryptAESGCM(jsonPayload);
    server.send(200, "text/plain", scrambled);
  } else {
    server.send(200, "application/json", jsonPayload);
  }
}

inline void handleClaim() {
  if (!isAuthorized()) {
    server.send(401, "application/json", "{\"error\":\"invalid_pairing_token\"}");
    return;
  }

  preferences.begin("pair", false);
  preferences.putBool("claimed", true);
  preferences.end();
  deviceClaimed = true;

  server.send(200, "application/json", "{\"status\":\"claimed_successfully\"}");
  Serial.println(F("== HARDWARE LIFE-CYCLE LOCKED: SECURE MODE =="));
}

inline void handleConfigure() {
  if (!deviceClaimed) {
    server.send(403, "application/json", "{\"error\":\"device_not_claimed\"}");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"missing_payload\"}");
    return;
  }

  String base64Payload = server.arg("plain");
  String decryptedJson = decryptAESGCM(base64Payload);

  if (decryptedJson == "") {
    server.send(401, "application/json", "{\"error\":\"decryption_failed_or_tampered\"}");
    return;
  }

  String ssid = "", password = "";
  if (parseWiFiCredentials(decryptedJson, ssid, password)) {
    saveWiFi(ssid, password);
    server.send(200, "application/json", "{\"status\":\"connecting\"}");
    connectToWiFi(ssid, password);
  } else {
    server.send(400, "application/json", "{\"error\":\"invalid_json_format\"}");
  }
}

inline void handleReset() {
  if (!deviceClaimed) {
    server.send(403, "application/json", "{\"error\":\"device_not_claimed\"}");
    return;
  }

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"missing_payload\"}");
    return;
  }

  String base64Payload = server.arg("plain");
  String decryptedJson = decryptAESGCM(base64Payload);

  if (decryptedJson == "") {
    server.send(401, "application/json", "{\"error\":\"unauthorized_wipe_request\"}");
    return;
  }

  softReset();
  server.send(200, "application/json", "{\"status\":\"resetting\"}");
  delay(1000);
  ESP.restart();
}

// Register all server routes and start HTTP daemon
inline void setupWebServer() {
  server.on("/", HTTP_GET, handleInfo);
  server.on("/info", handleInfo);
  server.on("/data", handleData);
  server.on("/claim", HTTP_POST, handleClaim);
  server.on("/configure", HTTP_POST, handleConfigure);
  server.on("/wifi-status", handleWifiStatus);
  server.on("/reset", HTTP_POST, handleReset);
  server.on("/connect-wifi", HTTP_POST, []() {
    String ssid, pass;
    if (loadWiFi(ssid, pass)) {
      server.send(200, "application/json", "{\"status\":\"connecting\"}");
      connectToWiFi(ssid, pass);
    } else {
      server.send(400, "application/json", "{\"error\":\"no_saved_wifi\"}");
    }
  });
  server.on("/disconnect-wifi", HTTP_POST, []() {
    WiFi.disconnect(false, false);
    isTryingSTA = false;
    wifiDisconnectTime = millis();
    lastRetryTime = millis();
    server.send(200, "application/json", "{\"status\":\"wifi_disconnected\"}");
  });

  server.onNotFound([]() {
    Serial.println("[HTTP " +
                   String(server.method() == HTTP_GET ? "GET" : "POST") + "] " +
                   server.uri() + " (not matched) from " +
                   server.client().remoteIP().toString());
    server.send(404, "application/json", "{\"error\":\"not_found\"}");
  });

  server.enableCORS(true);
  server.begin();
  Serial.println(F("[WebServer] HTTP Server started on port 80."));
}

inline void handleWebServer() {
  server.handleClient();
}

// ================= WIFI RECONNECT & BACKGROUND STATE MACHINE =================
inline void handleWiFiReconnectEngine() {
  // 1. Deferred work from WiFi event callbacks
  if (pendingChannelSave > 0) {
    int routerCh = pendingChannelSave;
    pendingChannelSave = 0;
    String savedS, savedP;
    int savedCh = 0;
    if (loadWiFi(savedS, savedP, savedCh) && savedCh != routerCh) {
      saveWiFi(savedS, savedP, routerCh);
    }
    if (!btModeActive && WiFi.channel() != routerCh) {
      Serial.printf("WiFi: Syncing SoftAP to router channel %d...\n", routerCh);
      WiFi.softAP(apSSID.c_str(), apPASS, routerCh);
      applyWiFiRadioTuning();
    }
  }

  if (pendingMdnsStart) {
    pendingMdnsStart = false;
    if (!mdnsStarted && WiFi.status() == WL_CONNECTED) {
      startMDNS();
      mdnsStarted = true;
    }
  }

  // 2. Active STA connection attempt monitor
  if (isTryingSTA) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi Connected: " + WiFi.localIP().toString());
      isTryingSTA = false;
      wifiRetryCount = 0;
    } else if (millis() - wifiConnectStart > WIFI_TIMEOUT) {
      if (wifiRetryCount < WIFI_MAX_RETRIES) {
        wifiRetryCount++;
        Serial.printf("WiFi: Attempt failed (status=%d). Auto-retry %d/%d...\n",
                      WiFi.status(), wifiRetryCount, WIFI_MAX_RETRIES);
        WiFi.disconnect(false);
        delay(100);
        String ssid, pass;
        int ch;
        if (loadWiFi(ssid, pass, ch)) {
          if (ch > 0) {
            WiFi.begin(ssid.c_str(), pass.c_str(), ch);
          } else {
            WiFi.begin(ssid.c_str(), pass.c_str());
          }
          applyWiFiRadioTuning();
          wifiConnectStart = millis();
        } else {
          isTryingSTA = false;
        }
      } else {
        Serial.printf("WiFi: All %d attempts failed. Pausing until next interval.\n", WIFI_MAX_RETRIES + 1);
        isTryingSTA = false;
        wifiRetryCount = 0;
        lastRetryTime = millis();
      }
    }
  }

  // 3. Background LAN Reconnect Engine (in Bluetooth + LAN mode)
  if (!TEST_BT_ONLY && btModeActive && WiFi.status() != WL_CONNECTED && !isTryingSTA) {
    bool btActive = btConnected || (millis() - lastBtActivityTime < 15000);
    bool recentlyDisconnected = (wifiDisconnectTime > 0) && (millis() - wifiDisconnectTime < WIFI_DISCONNECT_BT_GUARD);

    if (!btActive && !recentlyDisconnected && (millis() - lastRetryTime > 15000)) {
      lastRetryTime = millis();
      String savedSSID, savedPass;
      int savedCh = 0;
      if (loadWiFi(savedSSID, savedPass, savedCh)) {
        Serial.printf("[LAN Mode] Background reconnecting to [%s] on Channel %d...\n", savedSSID.c_str(), savedCh);
        if (savedCh > 0) {
          WiFi.begin(savedSSID.c_str(), savedPass.c_str(), savedCh);
        } else {
          WiFi.begin(savedSSID.c_str(), savedPass.c_str());
        }
        applyWiFiRadioTuning();
        isTryingSTA = true;
        wifiRetryCount = 0;
        wifiConnectStart = millis();
      }
    }
  }
}

// ================= WIFI EVENT HANDLER INITIALIZER =================
inline void setupWiFiEvents() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      uint8_t r = info.wifi_sta_disconnected.reason;
      const char *why = (r == 201) ? " (NO_AP_FOUND: router not seen on 2.4GHz)"
                        : (r == 36)  ? " (STA_LEAVING: stack channel hop)"
                        : (r == 202) ? " (AUTH_FAIL)"
                        : (r == 15 || r == 204)
                            ? " (wrong password / handshake timeout)"
                        : (r == 200) ? " (BEACON_TIMEOUT)"
                                     : "";
      Serial.printf("WiFi disconnect event; reason=%u%s\n", (unsigned)r, why);
      mdnsStarted = false;
      if (!isTryingSTA) {
        lastRetryTime = millis();
      }
      btRefreshPending = true;
      btRefreshAt = millis() + 500; // short delay - wifi teardown is quick
    } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      uint8_t ch = info.wifi_sta_connected.channel;
      Serial.printf("WiFi Event: STA Connected to Router on Channel %d\n", ch);
      if (ch > 0) {
        pendingChannelSave = ch;
      }
      applyWiFiRadioTuning();
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.printf("WiFi Event: Got IP %s (LAN Mode Ready)\n",
                    WiFi.localIP().toString().c_str());
      isTryingSTA = false;
      wifiRetryCount = 0;
      pendingMdnsStart = true;
      btRefreshPending = true;
      btRefreshAt = millis() + BT_REFRESH_DELAY_MS;
    }
  });
}

