#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include "config.h"

// ================= BH1750 AMBIENT LIGHT SENSOR MODULE =================
inline BH1750 lightMeter;
inline bool bh1750Found = false;

// ================= CALIBRATION PARAMETERS =================
// Gain = multiplier, Offset = additive correction
// To calibrate: compare sensor reading vs a known reference lux meter
// Example: If sensor reads 1200 lx but reference shows 1000 lx → LUX_GAIN = 0.833
inline float LUX_GAIN   = 1.00;  // Lux multiplier (correct for enclosure/glass attenuation)
inline float LUX_OFFSET = 0.0;   // Lux additive correction

// ================= SENSOR STATE & READINGS =================
inline float luxSmooth   = 0.0;
inline bool  bh1750OK    = false;
inline int   luxFailCount = 0;

// ================= OFFICIAL BH1750FVI HARDWARE SPECS =================
// Source: ROHM BH1750FVI Datasheet
//
// Parameter        | Value
// ------------------|-------------------------------
// Measurement Range | 1 – 65,535 lx (High Res Mode)
// Resolution        | 1 lx (High Res), 0.5 lx (High Res Mode 2)
// Accuracy          | ±20% typical variation
// Spectral Response | Matches human eye (Vλ function)
// IR Rejection      | Built-in infrared filtering
// Flicker Rejection | 50Hz / 60Hz noise rejection
// I2C Address       | 0x23 (ADDR=LOW), 0x5C (ADDR=HIGH)

// ---- PLAUSIBILITY VALIDATION RANGES ----
const float LUX_VALID_MIN = 0.0;
const float LUX_VALID_MAX = 65535.0;  // 16-bit ADC ceiling (datasheet)

// ---- NEGATIVE READ = SENSOR ERROR ----
// BH1750 library returns negative values on I2C communication failure
const float LUX_ERROR_THRESHOLD = -1.0;

// ---- CONSECUTIVE FAIL THRESHOLD ----
const int   LUX_FAIL_ZERO_THRESHOLD = 3;

// ================= INITIALIZATION =================
inline void setupBH1750() {
  Wire.begin(BH1750_SDA, BH1750_SCL);
  Wire.beginTransmission(0x23);
  if (Wire.endTransmission() == 0) {
    if (lightMeter.begin()) {
      bh1750Found = true;
      bh1750OK    = true;
      Serial.println(F("[BH1750] Sensor ready on I2C (Address: 0x23)."));
      return;
    }
  }
  Serial.println(F("[BH1750] Sensor not detected on I2C (skipping)."));
}

// ================= READ & CALIBRATE =================
inline void readBH1750() {
  if (!bh1750Found) {
    luxSmooth = 0.0;
    bh1750OK  = false;
    return;
  }

  float rawLux = lightMeter.readLightLevel();

  // ---- CHECK FOR COMMUNICATION FAILURE ----
  // BH1750 library returns negative values when I2C read fails
  if (rawLux < LUX_ERROR_THRESHOLD) {
    luxFailCount++;

    if (luxFailCount >= LUX_FAIL_ZERO_THRESHOLD) {
      luxSmooth = 0.0;
      bh1750OK  = false;
    }
    return;
  }

  // ---- APPLY CALIBRATION ----
  float calibLux = (rawLux * LUX_GAIN) + LUX_OFFSET;

  // ---- CLAMP TO VALID OUTPUT RANGE ----
  if (calibLux < LUX_VALID_MIN) calibLux = LUX_VALID_MIN;
  if (calibLux > LUX_VALID_MAX) calibLux = LUX_VALID_MAX;

  // ---- COMMIT CALIBRATED VALUE ----
  luxSmooth    = calibLux;
  bh1750OK     = true;
  luxFailCount = 0;
}
