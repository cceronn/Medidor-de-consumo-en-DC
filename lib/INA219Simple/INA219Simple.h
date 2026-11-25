#pragma once
// -------------------------------------------------------------
//  INA219Simple: driver mínimo para el INA219 sin librerías
//  externas. Lee directamente los registros de voltaje de shunt
//  y de bus, informa errores de I2C y detecta saturación.
//  La corriente la calculamos nosotros (I = Vshunt / Rshunt),
//  así la calibración queda completamente bajo nuestro control.
// -------------------------------------------------------------
#include <Arduino.h>
#include <Wire.h>

// Ganancia del PGA = rango de voltaje en el shunt.
// Con un shunt de 0,1 Ω: 40 mV -> 0,4 A | 80 mV -> 0,8 A | 160 mV -> 1,6 A | 320 mV -> 3,2 A
enum class INA219Rango : uint8_t { mV40 = 0, mV80 = 1, mV160 = 2, mV320 = 3 };

// Promedio interno del ADC (12 bits). Más muestras = menos ruido, más tiempo.
enum class INA219Promedio : uint8_t {
  x1 = 0x3, x2 = 0x9, x4 = 0xA, x8 = 0xB, x16 = 0xC, x32 = 0xD, x64 = 0xE, x128 = 0xF
};

struct INA219Medida {
  bool  valida = false;     // false si falló la comunicación I2C
  float shunt_mV = 0;       // voltaje en el shunt (con signo)
  float bus_V = 0;          // voltaje de VIN- respecto a GND
  bool  saturada = false;   // el shunt llegó al límite del rango elegido
  bool  overflow = false;   // bit OVF del chip
};

class INA219Simple {
public:
  explicit INA219Simple(uint8_t direccion = 0x40) : _addr(direccion) {}

  // Inicia y configura: bus 32 V, rango y promedio indicados, modo continuo.
  bool begin(TwoWire &wire, INA219Rango rango, INA219Promedio promedio);
  bool conectado();

  // Lectura completa (shunt + bus) con validación.
  INA219Medida leer();

  float rangoShunt_mV() const { return rangoEn_mV(_rango); }
  uint32_t tiempoCiclo_ms() const { return _tCiclo_ms; }
  uint8_t direccion() const { return _addr; }

  static float rangoEn_mV(INA219Rango r);
  static uint16_t construirConfig(INA219Rango r, INA219Promedio p);
  static uint32_t tiempoConversion_us(INA219Promedio p);

private:
  bool leerReg(uint8_t reg, uint16_t &valor);
  bool escribirReg(uint8_t reg, uint16_t valor);

  TwoWire *_wire = nullptr;
  uint8_t _addr;
  INA219Rango _rango = INA219Rango::mV320;
  uint32_t _tCiclo_ms = 2;
};