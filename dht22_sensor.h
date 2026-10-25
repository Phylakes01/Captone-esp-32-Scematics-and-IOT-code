#pragma once
#include <Arduino.h>
#include <DHT.h>
#include "config.h"

// ================= DHT22 SENSOR MODULE =================
inline DHT dht(DHTPIN, DHTTYPE);

// Calibration offsets
inline float DHT_TEMP_GAIN = 1.00;
inline float DHT_TEMP_OFFSET = 0.00;

// Sensor state & smoothed values
inline float airTempCSmooth = 0.0;
inline bool dhtOK = false;

// Initialize DHT22 hardware
inline void setupDHT22() {
  dht.begin();
  Serial.println(F("[DHT22] Initialized on Pin 27."));
}

// Read temperature from DHT22 with calibration
inline void readDHT22() {
  float airTempRaw = dht.readTemperature();
  if (!isnan(airTempRaw)) {
    airTempCSmooth = (airTempRaw * DHT_TEMP_GAIN) + DHT_TEMP_OFFSET;
    dhtOK = true;
  } else {
    dhtOK = false;
  }
}
