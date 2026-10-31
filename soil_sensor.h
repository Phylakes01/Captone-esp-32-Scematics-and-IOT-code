#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include "config.h"

// ================= SOIL SENSOR (RS485 MODBUS 7-IN-1) MODULE =================
inline HardwareSerial RS485Serial(2);
inline ModbusMaster node;

// ================= CALIBRATION (UNCHANGED) =================

inline float N_GAIN           = 1.00; 
inline int   N_OFFSET         = 0;
inline float P_GAIN           = 1.00; 
inline int   P_OFFSET         = 0;
inline float K_GAIN           = 1.00; 
inline int   K_OFFSET         = 0;
inline float SOIL_TEMP_OFFSET = 0.0;
inline float PH_OFFSET        = 0.0;
inline float EC_GAIN          = 1.00;
inline int   EC_OFFSET        = 0;

// ================= DATA =================
inline float humiditySmooth = 0;
inline float soilTempSmooth = 0;
inline float phSmooth       = 0;
inline int   ecSmooth       = 0;
inline int   nSmooth        = 0;
inline int   pSmooth        = 0;
inline int   kSmooth        = 0;

inline bool sensorOK = false;

// ================= HELPERS =================
inline int16_t toSigned16(uint16_t value) {
  return (int16_t)value;
}

// ================= RS485 DIRECTION CONTROL =================
inline void preTransmission() {
  digitalWrite(RE_PIN, HIGH);
  digitalWrite(DE_PIN, HIGH);
}

inline void postTransmission() {
  digitalWrite(RE_PIN, LOW);
  digitalWrite(DE_PIN, LOW);
}

// Forward declarations for modbus idle yield
void handleBluetooth();
void handleWebServer();

inline void modbusIdle() {
  handleBluetooth();
  handleWebServer();
  yield();
}

// ================= INITIALIZATION =================
inline void setupSoilSensor() {
  pinMode(RE_PIN, OUTPUT);
  pinMode(DE_PIN, OUTPUT);
  digitalWrite(RE_PIN, LOW);
  digitalWrite(DE_PIN, LOW);

  RS485Serial.begin(4800, SERIAL_8N1, RS485_RX, RS485_TX);
  RS485Serial.setTimeout(80);

  node.begin(1, RS485Serial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  node.idle(modbusIdle);

  Serial.println(F("[SoilSensor] RS485 Modbus initialized (Original calibration)."));
}

// ================= READ & CALIBRATE (ORIGINAL CODE) =================
inline void readSoilSensor() {
  uint8_t result = node.readHoldingRegisters(0x0000, 7);

  if (result == node.ku8MBSuccess) {
    humiditySmooth = node.getResponseBuffer(0) / 10.0;
    soilTempSmooth = (toSigned16(node.getResponseBuffer(1)) / 10.0) + SOIL_TEMP_OFFSET;
    ecSmooth       = node.getResponseBuffer(2);
    phSmooth       = (node.getResponseBuffer(3) / 10.0) + PH_OFFSET;
    nSmooth        = node.getResponseBuffer(4);
    pSmooth        = node.getResponseBuffer(5);
    kSmooth        = node.getResponseBuffer(6);
    sensorOK       = true;
  } else {
    sensorOK       = false;
  }
}
