#pragma once
// =============================================================
//  CalibradorINA219
//  - Aplica la calibración (R shunt efectiva, offset y factor de
//    voltaje) guardada en la memoria NVS del ESP32, namespace "cal".
//    La misma que escribe el programa de calibración.
//  - Entrega lecturas calibradas y marca lecturas inválidas o
//    saturadas.
//  - Maneja el LED de alerta, el buzzer y el botón:
//      pulsación corta  -> el main decide (silenciar / rearmar)
//      pulsación larga (3 s) -> solicitud de "cero" (el main
//                              desconecta la carga y lo ejecuta)
// =============================================================
#include <Arduino.h>
#include <Wire.h>
#include "INA219Simple.h"

// Rango de corriente = rango del PGA / R shunt (con 0,1 Ω nominal)
enum RangoINA219 : uint8_t {
  RANGO_400MA,    // PGA ±40 mV  -> 0,4 A  (máxima resolución)
  RANGO_800MA,    // PGA ±80 mV  -> 0,8 A
  RANGO_1600MA,   // PGA ±160 mV -> 1,6 A
  RANGO_3200MA    // PGA ±320 mV -> 3,2 A  (rango completo del shunt)
};

struct CalibracionINA219 {
  float rShunt_ohm;   // resistencia efectiva del shunt
  float offset_mV;    // lectura del shunt sin corriente
  float kV;           // factor de corrección del voltaje de bus
};

struct LecturaCalibrada {
  bool  valida                = false;  // false = falló el I2C
  bool  saturada              = false;  // corriente fuera del rango del PGA
  float voltajeCarga_V = 0;
  float voltajeBus_V          = 0;
  float voltajeShunt_mV       = 0;
  float corrienteCruda_mA     = 0;      // con R shunt nominal, sin calibrar
  float corrienteCalibrada_mA = 0;      // con la calibración aplicada
  float potencia_mW           = 0;
  bool  hayAlerta             = false;
  const char *motivoAlerta    = "";
};

class CalibradorINA219 {
public:
  CalibradorINA219(uint8_t pinBoton, uint8_t pinLed, float shuntNominal_ohm, uint8_t pinBuzzer = 255);

  // Configura LED, buzzer y botón. Llamar una vez al arrancar.
  void configurarPines();

  // Inicia el sensor (no bloquea). false si no responde: se puede reintentar.
  bool iniciar(INA219Simple &ina, TwoWire &wire, RangoINA219 rango,
               INA219Promedio promedio = INA219Promedio::x16);

  // Botón, LED y buzzer. Llamar en cada loop(). No usa el I2C.
  void actualizar();

  // Lectura calibrada. Si falla, marca el sensor como no disponible.
  LecturaCalibrada leer(INA219Simple &ina);

  // Mide el cero SIN corriente y lo guarda. La carga debe estar desconectada.
  bool hacerCero(INA219Simple &ina);

  void silenciar() { _silenciado = true; }
  bool consumirPulsacion();       // true una vez por pulsación corta
  bool consumirSolicitudCero();   // true una vez por pulsación larga (3 s)
  void setAlertaExterna(bool activa) { _alertaExterna = activa; }

  bool sensorOk() const { return _sensorOk; }
  float rangoMax_mA() const { return _rango_mV / _cal.rShunt_ohm; }   // mV / ohm = mA
  const CalibracionINA219 &calibracion() const { return _cal; }

  // Memoria NVS compartida con el programa de calibración
  static CalibracionINA219 cargarDeMemoria(float shuntNominal_ohm, bool *eraValida = nullptr);
  static void guardarEnMemoria(const CalibracionINA219 &c);
  static INA219Rango rangoPGA(RangoINA219 r);

private:
  uint8_t _pinBoton, _pinLed, _pinBuzzer;
  float _shuntNominal;
  CalibracionINA219 _cal;
  float _rango_mV = 80.0f;

  bool _sensorOk = false;
  bool _alertaInterna = false;
  bool _alertaExterna = false;
  bool _silenciado = false;
  bool _pulsacion = false;
  bool _solicitudCero = false;

  bool _btnAnterior = HIGH;
  uint32_t _tCambio = 0, _tPresionado = 0;
  bool _largoHecho = false;
  uint32_t _tConfirmacion = 0;

  void escribirIndicadores();
};