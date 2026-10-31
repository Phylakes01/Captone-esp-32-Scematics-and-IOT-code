#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "mbedtls/base64.h"
#include "mbedtls/gcm.h"
#include "config.h"
#include "dht22_sensor.h"
#include "bh1750_sensor.h"
#include "soil_sensor.h"

// ================= STORAGE & PREFERENCES =================
inline Preferences preferences;
inline bool deviceClaimed = false; // Edge security lock
inline String pairingToken = "";

// ================= HARDWARE MODE SWITCH & BLUETOOTH STATE =================
inline bool btModeActive = false; // true = Bluetooth + LAN Mode | false = AP Mode Only
inline bool btRunning = false;    // Tracks if Bluetooth stack is initialized and running

inline volatile bool btConnected = false;
inline volatile unsigned long lastBtActivityTime = 0;
inline volatile unsigned long btSessionStart = 0;
inline volatile bool btResetParser = false;
inline volatile bool btRefreshPending = false;
inline volatile unsigned long btRefreshAt = 0;

// Reset button state tracking
inline unsigned long buttonPressStart = 0;
inline bool buttonHeld = false;

// ================= MAC ADDRESS HELPERS =================
inline String getHardwareMac() {
  uint64_t chipid = ESP.getEfuseMac();
  char mac[18];
  sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
          (uint8_t)(chipid >> 40), (uint8_t)(chipid >> 32),
          (uint8_t)(chipid >> 24), (uint8_t)(chipid >> 16),
          (uint8_t)(chipid >> 8),  (uint8_t)chipid);
  return String(mac);
}

inline String getShortHardwareMac() {
  String fullMacAddress = getHardwareMac();
  fullMacAddress.replace(":", "");
  return fullMacAddress.substring(fullMacAddress.length() - 6);
}

// ================= STATUS & SENSOR JSON =================
inline String getStatus() {
  if (!dhtOK) return "NO_DATA";
  if (airTempCSmooth >= 35.0) return "HOT";
  return "NORMAL";
}

inline String createSensorJson() {
  static char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"soil\":{\"moisture\":%.1f,\"temperature\":%.1f,\"ph\":%.1f,"
           "\"ec\":%d,\"nitrogen\":%d,\"phosphorus\":%d,\"potassium\":%d},"
           "\"environment\":{\"light\":%d,\"airTemperature\":%.1f,"
           "\"status\":\"%s\"},"
           "\"system\":{\"sensorOK\":%s,\"dhtOK\":%s,\"lightOK\":%s}}",
           humiditySmooth, soilTempSmooth, phSmooth, ecSmooth, nSmooth, pSmooth,
           kSmooth, (int)luxSmooth, airTempCSmooth,
           getStatus().c_str(),
           sensorOK ? "true" : "false", dhtOK ? "true" : "false",
           bh1750OK ? "true" : "false");
  return String(buf);
}

// ================= CRYPTOGRAPHY: AES-256 GCM =================
inline void hexToBytes(const String &hex, uint8_t *bytes) {
  for (int i = 0; i < hex.length(); i += 2) {
    String byteString = hex.substring(i, i + 2);
    bytes[i / 2] = (uint8_t)strtol(byteString.c_str(), NULL, 16);
  }
}

inline String encryptAESGCM(String plaintext) {
  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);

  uint8_t key[32];
  hexToBytes(pairingToken, key);
  mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);

  uint8_t iv[12];
  esp_fill_random(iv, 12);

  size_t plaintext_len = plaintext.length();
  size_t package_len = 12 + plaintext_len + 16;

  uint8_t *package = (uint8_t *)malloc(package_len);
  if (!package) {
    mbedtls_gcm_free(&ctx);
    Serial.printf("[Crypto] encryptAESGCM: malloc failed (free heap=%u)\n", (unsigned)ESP.getFreeHeap());
    return "";
  }

  uint8_t tag[16];
  memcpy(package, iv, 12);

  mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, plaintext_len, iv, 12,
                            NULL, 0, (const unsigned char *)plaintext.c_str(),
                            package + 12, 16, tag);

  memcpy(package + 12 + plaintext_len, tag, 16);

  size_t b64_len = 0;
  mbedtls_base64_encode(NULL, 0, &b64_len, package, package_len);
  unsigned char *b64_buffer = (unsigned char *)malloc(b64_len + 1);

  String finalResult = "";
  if (b64_buffer) {
    size_t actual_b64_len = 0;
    mbedtls_base64_encode(b64_buffer, b64_len + 1, &actual_b64_len, package, package_len);
    b64_buffer[actual_b64_len] = '\0';
    finalResult = String((char *)b64_buffer);
    free(b64_buffer);
  } else {
    Serial.printf("[Crypto] encryptAESGCM: b64 malloc failed (free heap=%u)\n", (unsigned)ESP.getFreeHeap());
  }

  free(package);
  mbedtls_gcm_free(&ctx);
  return finalResult;
}

inline String decryptAESGCM(String base64Payload) {
  size_t expected_len = 0;
  mbedtls_base64_decode(NULL, 0, &expected_len,
                        (const unsigned char *)base64Payload.c_str(),
                        base64Payload.length());

  if (expected_len <= 28) {
    Serial.println(F("Decryption Error: Payload too short."));
    return "";
  }

  uint8_t *rawPackage = (uint8_t *)malloc(expected_len);
  if (!rawPackage) return "";

  size_t actual_len = 0;
  mbedtls_base64_decode(rawPackage, expected_len, &actual_len,
                        (const unsigned char *)base64Payload.c_str(),
                        base64Payload.length());

  size_t ciphertext_len = actual_len - 12 - 16;
  uint8_t iv[12];
  uint8_t tag[16];
  uint8_t *ciphertext = (uint8_t *)malloc(ciphertext_len);

  if (!ciphertext) {
    free(rawPackage);
    return "";
  }

  memcpy(iv, rawPackage, 12);
  memcpy(ciphertext, rawPackage + 12, ciphertext_len);
  memcpy(tag, rawPackage + 12 + ciphertext_len, 16);

  mbedtls_gcm_context ctx;
  mbedtls_gcm_init(&ctx);

  uint8_t key[32];
  hexToBytes(pairingToken, key);
  mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 256);

  uint8_t *plaintext = (uint8_t *)malloc(ciphertext_len + 1);
  String finalResult = "";

  if (plaintext) {
    int result = mbedtls_gcm_auth_decrypt(&ctx, ciphertext_len, iv, 12, NULL, 0,
                                          tag, 16, ciphertext, plaintext);
    if (result == 0) {
      plaintext[ciphertext_len] = '\0';
      finalResult = String((char *)plaintext);
    } else {
      Serial.println(F("CRITICAL SECURITY ERROR: GCM Auth Tag Verification Failed."));
    }
    free(plaintext);
  }

  free(rawPackage);
  free(ciphertext);
  mbedtls_gcm_free(&ctx);
  return finalResult;
}

// ================= JSON PARSER HELPERS =================
inline bool extractJsonString(const String &json, const String &key, String &output) {
  int keyIdx = json.indexOf("\"" + key + "\"");
  if (keyIdx == -1) return false;

  int colonIdx = json.indexOf(':', keyIdx + key.length() + 2);
  if (colonIdx == -1) return false;

  int valStart = json.indexOf('"', colonIdx);
  if (valStart == -1) return false;

  int valEnd = json.indexOf('"', valStart + 1);
  if (valEnd == -1) return false;

  output = json.substring(valStart + 1, valEnd);
  return true;
}

inline bool parseWiFiCredentials(const String &json, String &ssid, String &password) {
  if (!extractJsonString(json, "ssid", ssid)) {
    return false;
  }
  if (!extractJsonString(json, "pass", password)) {
    if (!extractJsonString(json, "password", password)) {
      password = "";
    }
  }
  return (ssid.length() > 0);
}

// ================= STORAGE FUNCTIONS =================
inline void loadOrCreateToken() {
  preferences.begin("pair", false);
  pairingToken = preferences.getString("token", "");

  if (pairingToken == "") {
    pairingToken = "";
    for (int i = 0; i < 8; i++) {
      char hex[9];
      sprintf(hex, "%08X", esp_random());
      pairingToken += String(hex);
    }
    preferences.putString("token", pairingToken);
  }

  deviceClaimed = preferences.getBool("claimed", false);
  preferences.end();
}

inline void saveWiFi(String ssid, String password, int channel = 0) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", password);
  preferences.putInt("ch", channel > 0 ? channel : 0);
  preferences.end();
}

inline bool loadWiFi(String &ssid, String &password, int &channel) {
  preferences.begin("wifi", true);
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  channel = preferences.getInt("ch", 0);
  preferences.end();
  return ssid.length() > 0;
}

inline bool loadWiFi(String &ssid, String &password) {
  int ch = 0;
  return loadWiFi(ssid, password, ch);
}

inline void clearWiFi() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();
}

inline void softReset() {
  preferences.begin("wifi", false);
  preferences.clear();
  preferences.end();

  preferences.begin("pair", false);
  preferences.clear();
  preferences.end();

  deviceClaimed = false;
}

// ================= BUZZER =================
inline void buzzerOn() {
#if BUZZER_PASSIVE
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, BUZZER_FREQ, 8);
  ledcWrite(BUZZER_PIN, 128);
#else
  ledcSetup(0, BUZZER_FREQ, 8);
  ledcAttachPin(BUZZER_PIN, 0);
  ledcWrite(0, 128);
#endif
#else
  digitalWrite(BUZZER_PIN, HIGH);
#endif
}

inline void buzzerOff() {
#if BUZZER_PASSIVE
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcDetach(BUZZER_PIN);
#else
  ledcDetachPin(BUZZER_PIN);
#endif
#endif
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
}

inline void markResetBeepPending() {
  preferences.begin("sys", false);
  preferences.putBool("resetBeep", true);
  preferences.end();
}

inline void beepIfResetPending() {
  preferences.begin("sys", false);
  bool pending = preferences.getBool("resetBeep", false);
  if (pending) preferences.putBool("resetBeep", false);
  preferences.end();

  if (pending) {
    buzzerOn();
    delay(1000); // 1-second confirmation beep
    buzzerOff();
  }
}

// ================= HARDWARE MODE SWITCH =================
inline bool readModeSwitch() {
  return (digitalRead(MODE_SWITCH_PIN) == LOW);
}

// Debounced mode switch detector: requires reading to stay stable for > 80ms
inline bool checkModeSwitchChanged(bool &newModeOut) {
  static int lastPinReading = -1;
  static unsigned long lastDebounceTime = 0;
  static bool initialized = false;

  int reading = digitalRead(MODE_SWITCH_PIN);
  if (!initialized) {
    initialized = true;
    lastPinReading = reading;
    return false;
  }

  if (reading != lastPinReading) {
    lastDebounceTime = millis();
    lastPinReading = reading;
  }

  if ((millis() - lastDebounceTime) > 80) {
    bool debouncedMode = (reading == LOW);
    if (debouncedMode != btModeActive) {
      newModeOut = debouncedMode;
      return true;
    }
  }
  return false;
}

// ================= HARDWARE RESET BUTTON =================
inline void handleResetButton() {
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (!buttonHeld) {
      buttonHeld = true;
      buttonPressStart = millis();
    }
    if (millis() - buttonPressStart > 5000) {
      softReset();
      markResetBeepPending();
      Serial.println(F("=== SYSTEM RECOVERY RESET APPLIED FROM HARDWARE PIN ==="));
      delay(1000);
      ESP.restart();
    }
  } else {
    buttonHeld = false;
  }
}

// ================= RESET REASON STRING =================
inline const char *resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
  case ESP_RST_POWERON:   return "POWERON";
  case ESP_RST_EXT:       return "EXT_PIN";
  case ESP_RST_SW:        return "SOFTWARE";
  case ESP_RST_PANIC:     return "PANIC (crash)";
  case ESP_RST_INT_WDT:   return "INT_WDT";
  case ESP_RST_TASK_WDT:  return "TASK_WDT";
  case ESP_RST_WDT:       return "WDT";
  case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
  case ESP_RST_BROWNOUT:  return "BROWNOUT (supply dipped)";
  case ESP_RST_SDIO:      return "SDIO";
  default:                return "UNKNOWN";
  }
}
