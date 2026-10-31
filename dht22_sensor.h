#pragma once
#include "config.h"
#include <Arduino.h>
#include <DHT.h>

// ================= DHT22 (AM2302) AIR SENSOR MODULE =================
// NOTE: Only temperature is used. DHT22 humidity is NOT used (soil sensor handles moisture).
inline DHT dht(DHTPIN, DHTTYPE);

// ================= CALIBRATION PARAMETERS =================
// To calibrate: compare sensor reading vs a known reference thermometer
// Example: If sensor reads 26.5°C but reference shows 26.0°C → DHT_TEMP_OFFSET = -0.5
inline float DHT_TEMP_GAIN   = 1.00;  // Temperature multiplier (normally leave at 1.0)
inline float DHT_TEMP_OFFSET = 0.00;  // °C additive correction

// ================= SENSOR STATE & READINGS =================
inline float airTempCSmooth  = 0.0;
inline bool  dhtOK           = false;
inline int   dhtFailCount    = 0;
inline unsigned long lastDHTReadTime = 0;

// ================= OFFICIAL DHT22/AM2302 HARDWARE SPECS =================
// Source: Aosong AM2302/DHT22 Datasheet
//
// Parameter     | Range           | Accuracy  | Resolution
// --------------|-----------------|-----------|----------
// Temperature   | –40°C to +80°C  | ±0.5°C    | 0.1°C
// Sampling Rate | 0.5 Hz (every 2 seconds minimum)

// ---- PLAUSIBILITY VALIDATION RANGES ----
const float DHT_TEMP_VALID_MIN = -40.0;  // Sensor hardware minimum (datasheet)
const float DHT_TEMP_VALID_MAX = 80.0;   // Sensor hardware maximum (datasheet)

// ---- CONSECUTIVE FAIL THRESHOLD ----
const int DHT_FAIL_ZERO_THRESHOLD = 3;

// ================= HELPERS =================
inline void zeroDHTReadings() {
  airTempCSmooth = 0.0;
  dhtOK          = false;
}

// ================= INITIALIZATION =================
inline void setupDHT22() {
  pinMode(DHTPIN, INPUT_PULLUP);
  delay(50);
  dht.begin();
}

// ================= READ & CALIBRATE =================
inline void readDHT22() {
  // DHT22 requires minimum 2 seconds between reads (datasheet spec)
  if (millis() - lastDHTReadTime < 2500) {
    return;
  }
  lastDHTReadTime = millis();

  float airTempRaw = dht.readTemperature();

  // ---- CHECK FOR COMMUNICATION FAILURE ----
  if (isnan(airTempRaw)) {
    dhtFailCount++;

    if (dhtFailCount >= DHT_FAIL_ZERO_THRESHOLD) {
      zeroDHTReadings();
    } else {
      dhtOK = false;
    }
    return;
  }

  // ---- APPLY CALIBRATION ----
  float calibTemp = (airTempRaw * DHT_TEMP_GAIN) + DHT_TEMP_OFFSET;

  // ---- PLAUSIBILITY VALIDATION ----
  if (calibTemp < DHT_TEMP_VALID_MIN || calibTemp > DHT_TEMP_VALID_MAX) {
    dhtFailCount++;
    if (dhtFailCount >= DHT_FAIL_ZERO_THRESHOLD) {
      zeroDHTReadings();
    } else {
      dhtOK = false;
    }
    return;
  }

  // ---- CLAMP TO VALID OUTPUT RANGE ----
  calibTemp = max(-40.0f, min(80.0f, calibTemp));

  // ---- COMMIT CALIBRATED VALUE ----
  airTempCSmooth = calibTemp;
  dhtOK          = true;
  dhtFailCount   = 0;
}
