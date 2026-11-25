#include "INA219Simple.h"

namespace {
constexpr uint8_t REG_CONFIG = 0x00;
constexpr uint8_t REG_SHUNT  = 0x01;
constexpr uint8_t REG_BUS    = 0x02;
constexpr uint16_t BIT_RESET = 0x8000;
constexpr uint16_t BIT_BRNG_32V = 0x2000;
constexpr uint16_t MODO_CONTINUO = 0x0007;   // shunt y bus, continuo
constexpr float LSB_SHUNT_mV = 0.01f;        // 10 µV
constexpr float LSB_BUS_V = 0.004f;          // 4 mV
}

float INA219Simple::rangoEn_mV(INA219Rango r) {
  switch (r) {
    case INA219Rango::mV40:  return 40.0f;
    case INA219Rango::mV80:  return 80.0f;
    case INA219Rango::mV160: return 160.0f;
    default:                 return 320.0f;
  }
}

uint16_t INA219Simple::construirConfig(INA219Rango r, INA219Promedio p) {
  uint16_t pg = static_cast<uint16_t>(r) & 0x3;
  uint16_t adc = static_cast<uint16_t>(p) & 0xF;
  return BIT_BRNG_32V | (pg << 11) | (adc << 7) | (adc << 3) | MODO_CONTINUO;
}

uint32_t INA219Simple::tiempoConversion_us(INA219Promedio p) {
  switch (p) {
    case INA219Promedio::x1:   return 532;
    case INA219Promedio::x2:   return 1060;
    case INA219Promedio::x4:   return 2130;
    case INA219Promedio::x8:   return 4260;
    case INA219Promedio::x16:  return 8510;
    case INA219Promedio::x32:  return 17020;
    case INA219Promedio::x64:  return 34050;
    default:                   return 68100;
  }
}

bool INA219Simple::begin(TwoWire &wire, INA219Rango rango, INA219Promedio promedio) {
  _wire = &wire;
  _rango = rango;
  // Un ciclo = conversión de shunt + conversión de bus
  _tCiclo_ms = (2 * tiempoConversion_us(promedio)) / 1000 + 1;
  if (!conectado()) return false;
  if (!escribirReg(REG_CONFIG, BIT_RESET)) return false;
  delay(2);
  uint16_t cfg = construirConfig(rango, promedio);
  if (!escribirReg(REG_CONFIG, cfg)) return false;
  uint16_t leido = 0;
  return leerReg(REG_CONFIG, leido) && leido == cfg;
}

bool INA219Simple::conectado() {
  if (_wire == nullptr) return false;
  _wire->beginTransmission(_addr);
  return _wire->endTransmission() == 0;
}

INA219Medida INA219Simple::leer() {
  INA219Medida m;
  uint16_t rawShunt, rawBus;
  if (!leerReg(REG_SHUNT, rawShunt) || !leerReg(REG_BUS, rawBus)) return m;   // valida = false

  int16_t sh = static_cast<int16_t>(rawShunt);
  m.shunt_mV = sh * LSB_SHUNT_mV;
  m.bus_V = (rawBus >> 3) * LSB_BUS_V;
  m.overflow = (rawBus & 0x0001) != 0;
  // Saturación: el valor llega al 99 % del rango del PGA
  m.saturada = fabsf(m.shunt_mV) >= 0.99f * rangoEn_mV(_rango);
  m.valida = true;
  return m;
}

bool INA219Simple::leerReg(uint8_t reg, uint16_t &valor) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission() != 0) return false;
  if (_wire->requestFrom(static_cast<int>(_addr), 2) != 2) return false;
  uint8_t msb = _wire->read();
  uint8_t lsb = _wire->read();
  valor = (static_cast<uint16_t>(msb) << 8) | lsb;
  return true;
}

bool INA219Simple::escribirReg(uint8_t reg, uint16_t valor) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(static_cast<uint8_t>(valor >> 8));
  _wire->write(static_cast<uint8_t>(valor & 0xFF));
  return _wire->endTransmission() == 0;
}