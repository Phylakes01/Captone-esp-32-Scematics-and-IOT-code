#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include "config.h"

// ================= SOIL SENSOR (RS485 MODBUS 7-IN-1) MODULE =================
inline HardwareSerial RS485Serial(2);
inline ModbusMaster node;

// Calibration parameters
inline float N_GAIN = 1.00;
inline int N_OFFSET = 0;
inline float P_GAIN = 1.00;
inline int P_OFFSET = 0;
inline float K_GAIN = 1.00;
inline int K_OFFSET = 0;
inline float SOIL_TEMP_OFFSET = 0.0;
inline float PH_OFFSET = 0.0;
inline float EC_GAIN = 1.00;
inline int EC_OFFSET = 0;

// Sensor smoothed readings
inline float humiditySmooth = 0;
inline float soilTempSmooth = 0;
inline float phSmooth = 0;
inline int ecSmooth = 0;
inline int nSmooth = 0;
inline int pSmooth = 0;
inline int kSmooth = 0;
inline bool sensorOK = false;
inline int modbusFailCount = 0;

// Number helpers
inline float clampFloat(float v, float mn, float mx) {
  return (v < mn) ? mn : (v > mx) ? mx : v;
}

inline int16_t toSigned16(uint16_t value) {
  return (int16_t)value;
}

// RS485 direction control callbacks
inline void preTransmission() {
  digitalWrite(RE_PIN, HIGH);
  digitalWrite(DE_PIN, HIGH);
}

inline void postTransmission() {
  digitalWrite(RE_PIN, LOW);
  digitalWrite(DE_PIN, LOW);
}

// Forward declarations of server and bluetooth handlers for modbus idle yield
void handleBluetooth();
void handleWebServer();

inline void modbusIdle() {
  handleBluetooth();
  handleWebServer();
  yield();
}

// Initialize RS485 and ModbusMaster
inline void setupSoilSensor() {
  pinMode(RE_PIN, OUTPUT);
  pinMode(DE_PIN, OUTPUT);
  digitalWrite(RE_PIN, LOW);
  digitalWrite(DE_PIN, LOW);

  RS485Serial.begin(4800, SERIAL_8N1, RS485_RX, RS485_TX);
  RS485Serial.setTimeout(80); // Quick 80ms timeout: eliminates blocking stalls

  node.begin(1, RS485Serial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  node.idle(modbusIdle);

  Serial.println(F("[SoilSensor] RS485 Modbus initialized (RX:16, TX:17, RE:4, DE:5)."));
}

// Read Modbus 7-in-1 Soil Sensor registers
inline void readSoilSensor() {
  uint8_t result = node.readHoldingRegisters(0x0000, 7);

  if (result == node.ku8MBSuccess) {
    float humidity = node.getResponseBuffer(0) / 10.0;
    float soilTemp = (toSigned16(node.getResponseBuffer(1)) / 10.0) + SOIL_TEMP_OFFSET;
    int ec = node.getResponseBuffer(2);
    float ph = (node.getResponseBuffer(3) / 10.0) + PH_OFFSET;
    int n = node.getResponseBuffer(4);
    int p = node.getResponseBuffer(5);
    int k = node.getResponseBuffer(6);

    // Apply calibration gains & offsets
    ec = (int)(ec * EC_GAIN) + EC_OFFSET;
    n = (int)(n * N_GAIN) + N_OFFSET;
    p = (int)(p * P_GAIN) + P_OFFSET;
    k = (int)(k * K_GAIN) + K_OFFSET;

    humiditySmooth = clampFloat(humidity, 0, 100);
    soilTempSmooth = clampFloat(soilTemp, -40, 80);
    ecSmooth = ec;
    phSmooth = ph;
    nSmooth = n;
    pSmooth = p;
    kSmooth = k;

    sensorOK = true;
    modbusFailCount = 0;
  } else {
    sensorOK = false;
    modbusFailCount++;
  }
}
