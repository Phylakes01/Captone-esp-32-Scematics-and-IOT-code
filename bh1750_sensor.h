#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include "config.h"

// ================= BH1750 AMBIENT LIGHT SENSOR MODULE =================
inline BH1750 lightMeter;
inline bool bh1750Found = false;
inline float luxSmooth = 0.0;

// Initialize BH1750 I2C sensor
inline void setupBH1750() {
  Wire.begin(BH1750_SDA, BH1750_SCL);
  Wire.beginTransmission(0x23);
  if (Wire.endTransmission() == 0) {
    if (lightMeter.begin()) {
      bh1750Found = true;
      Serial.println(F("[BH1750] Sensor ready on I2C (Address: 0x23)"));
      return;
    }
  }
  Serial.println(F("[BH1750] Sensor not detected on I2C (skipping)"));
}

// Read ambient light level in Lux
inline void readBH1750() {
  if (bh1750Found) {
    float lux = lightMeter.readLightLevel();
    if (lux < 0.0) lux = 0.0;
    if (lux > 65535.0) lux = 65535.0;
    luxSmooth = lux;
  } else {
    luxSmooth = 0.0;
  }
}
