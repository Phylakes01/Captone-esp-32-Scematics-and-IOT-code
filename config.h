#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Preferences.h>
#include "esp_bt.h"
#include "esp_coexist.h"
#include "esp_gap_bt_api.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

// ================= SYSTEM & DIAGNOSTIC CONFIGURATION =================
#define DISABLE_BROWNOUT_DETECTOR 0

// 1 = WiFi radio completely OFF, Bluetooth only. Use it for diagnostics.
#define TEST_BT_ONLY 0

// 1 = print one status line every 10s: uptime, free heap, stack headroom, BT/WiFi state.
#define DEBUG_HEARTBEAT 1

// ================= MAXIMUM RF POWER & COEXISTENCE CONFIGURATION =================
// WiFi TX power: Full certified +19.5 dBm maximum (~89 mW) for maximum range & penetration
#define WIFI_AP_TX_POWER  WIFI_POWER_19_5dBm // Max +19.5 dBm for SoftAP mode (Bluetooth is OFF)
#define WIFI_STA_TX_POWER WIFI_POWER_19_5dBm // Max +19.5 dBm for LAN Station mode (Connected to router)

// Bluetooth Classic (BR/EDR) maximum TX power:
#define BT_TX_POWER_MIN   ESP_PWR_LVL_P9     // Full +9 dBm power for rock-solid RF link
#define BT_TX_POWER_MAX   ESP_PWR_LVL_P9     // Full +9 dBm power

// Balanced RF timeslots so BOTH WiFi and Bluetooth stay fast, strong, and responsive
#define BT_COEX_PREFERENCE ESP_COEX_PREFER_BALANCE

// 1 = Wi-Fi (STA + AP) forced to 20MHz channel width.
#define WIFI_FORCE_HT20 1

// Wait delay after WiFi event before refreshing Bluetooth visibility
#define BT_REFRESH_DELAY_MS 2500

// ================= HARDWARE PIN DEFINITIONS =================
// Mode Switch (Pin 33): Switch to GND with internal INPUT_PULLUP
// Switch ON  (LOW)  -> BLUETOOTH + LAN MODE (SoftAP is OFF)
// Switch OFF (HIGH) -> AP MODE ONLY (Bluetooth is OFF)
#define MODE_SWITCH_PIN 33

// Hardware Recovery Reset Button
#define RESET_BUTTON_PIN 13

// Hardware Buzzer
#define BUZZER_PIN 15    // Pin 15
#define BUZZER_PASSIVE 1 // 1 = passive buzzer (PWM tone), 0 = active buzzer
#define BUZZER_FREQ 2700 // 2.7 kHz resonant frequency

// DHT22 Air Temperature & Humidity Sensor
#define DHTPIN 27
#define DHTTYPE DHT22

// BH1750 Ambient Light Sensor (I2C)
#define BH1750_SDA 21
#define BH1750_SCL 18

// RS485 / Modbus Soil Sensor (7-in-1: Moisture, Temp, EC, pH, N, P, K)
#define RS485_RX 16
#define RS485_TX 17
#define RE_PIN 4
#define DE_PIN 5

// ================= SENSOR & DATA UPDATE INTERVAL =================
// Fast 1-second interval for reading and sending sensor data (removed excess delays)
#define SENSOR_INTERVAL 1000

// ================= WIFI TIMEOUTS =================
const unsigned long WIFI_TIMEOUT = 15000;
const int WIFI_MAX_RETRIES = 2;
const unsigned long WIFI_DISCONNECT_BT_GUARD = 10000; // 10s quiet window after disconnect
const unsigned long RETRY_INTERVAL = 10000;
