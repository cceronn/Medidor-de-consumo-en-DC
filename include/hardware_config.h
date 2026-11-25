#pragma once
// =============================================================
//  CONFIGURACIÓN DE HARDWARE – única fuente de verdad
//  Corresponde al esquemático "Medidor de 12 V sin optoacoplador"
//  (montaje de prueba). La usan los dos programas:
//    env:principal   -> src/principal/main.cpp
//    env:calibracion -> src/calibracion/calibracion.cpp
// =============================================================
#include <Arduino.h>
#include "Calibration.h"   // RangoINA219

// ---------------- Pines del ESP32 DevKit V1 ------------------
constexpr uint8_t PIN_SDA    = 21;  // U2 INA219 SDA (I2C)
constexpr uint8_t PIN_SCL    = 22;  // U2 INA219 SCL (I2C)
constexpr uint8_t PIN_CORTE  = 27;  // R4 100 Ω -> gate de Q2 IRLZ44N. HIGH = carga conectada
constexpr uint8_t PIN_BUZZER = 26;  // R8 1 kΩ -> base Q3 -> BZ1. HIGH = suena
constexpr uint8_t PIN_LED    = 25;  // R10 1 kΩ -> base Q4 -> D5. HIGH = encendido
constexpr uint8_t PIN_BOTON  = 33;  // R11 330 Ω -> SW1 a GND (C4 100 nF). Pull-up interno

// ---------------- Sensor INA219 ------------------------------
constexpr uint8_t INA219_DIRECCION = 0x40;      // A0 = A1 = GND
constexpr float   SHUNT_NOMINAL_OHM = 0.100f;   // shunt "R100" del módulo
constexpr uint32_t I2C_FRECUENCIA_HZ = 100000;  // 100 kHz: robusto con cables largos

// Tensión máxima que admite el INA219 en el bus (dato del fabricante)
constexpr float V_MAX_INA219 = 26.0f;

// Rango de medida del INA219 (ganancia del PGA). Lo usan los dos
// programas, así la calibración se hace en el mismo rango en que se mide.
//   RANGO_400MA | RANGO_800MA | RANGO_1600MA | RANGO_3200MA  (shunt 0,1 Ω)
constexpr RangoINA219 RANGO_SENSOR = RANGO_800MA;