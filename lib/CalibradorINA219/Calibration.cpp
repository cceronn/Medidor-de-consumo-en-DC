#include "Calibration.h"
#include "formato_si.h"
#include <Preferences.h>

namespace {
constexpr const char *NVS_NAMESPACE = "cal";
constexpr uint32_t PULSACION_LARGA_MS = 3000;
constexpr uint32_t ANTIRREBOTE_MS = 40;
constexpr float CORRIENTE_RUIDO_mA = 2.0f;   // por debajo se muestra 0
}

CalibradorINA219::CalibradorINA219(uint8_t pinBoton, uint8_t pinLed, float shuntNominal_ohm, uint8_t pinBuzzer)
    : _pinBoton(pinBoton), _pinLed(pinLed), _pinBuzzer(pinBuzzer), _shuntNominal(shuntNominal_ohm) {
  _cal = {shuntNominal_ohm, 0.0f, 1.0f};
}

// ---------------- Memoria NVS --------------------------------
CalibracionINA219 CalibradorINA219::cargarDeMemoria(float nominal, bool *eraValida) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, true);
  CalibracionINA219 c;
  c.rShunt_ohm = prefs.getFloat("rsh", nominal);
  c.offset_mV  = prefs.getFloat("off", 0.0f);
  c.kV         = prefs.getFloat("kv", 1.0f);
  prefs.end();

  // Valores fuera de lo razonable = memoria corrupta o calibración mala
  bool ok = (c.rShunt_ohm > nominal * 0.8f && c.rShunt_ohm < nominal * 1.2f) &&
            (fabsf(c.offset_mV) <= 1.0f) && (c.kV > 0.9f && c.kV < 1.1f);
  if (!ok) c = {nominal, 0.0f, 1.0f};
  if (eraValida) *eraValida = ok;
  return c;
}

void CalibradorINA219::guardarEnMemoria(const CalibracionINA219 &c) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putFloat("rsh", c.rShunt_ohm);
  prefs.putFloat("off", c.offset_mV);
  prefs.putFloat("kv", c.kV);
  prefs.end();
}

INA219Rango CalibradorINA219::rangoPGA(RangoINA219 r) {
  switch (r) {
    case RANGO_400MA:  return INA219Rango::mV40;
    case RANGO_800MA:  return INA219Rango::mV80;
    case RANGO_1600MA: return INA219Rango::mV160;
    default:           return INA219Rango::mV320;
  }
}

// ---------------- Inicialización -----------------------------
void CalibradorINA219::configurarPines() {
  pinMode(_pinLed, OUTPUT);
  digitalWrite(_pinLed, LOW);
  pinMode(_pinBoton, INPUT_PULLUP);
  if (_pinBuzzer != 255) {
    pinMode(_pinBuzzer, OUTPUT);
    digitalWrite(_pinBuzzer, LOW);
  }
}

bool CalibradorINA219::iniciar(INA219Simple &ina, TwoWire &wire, RangoINA219 rango, INA219Promedio promedio) {
  INA219Rango pga = rangoPGA(rango);
  _rango_mV = INA219Simple::rangoEn_mV(pga);
  _sensorOk = ina.begin(wire, pga, promedio);
  if (!_sensorOk) return false;

  bool calOk = false;
  _cal = cargarDeMemoria(_shuntNominal, &calOk);
  Serial.printf("INA219 OK en 0x%02X | rango ±%.0f mV (%s) | R shunt = %.6f ohm | offset = %+.4f mV | kV = %.5f%s\n",
                ina.direccion(), _rango_mV, formatoSI(rangoMax_mA()/1000.0f, "A").c_str(), _cal.rShunt_ohm, _cal.offset_mV, _cal.kV,
                calOk ? "" : "  [SIN CALIBRAR: valores nominales]");
  return true;
}

// ---------------- Lectura ------------------------------------
LecturaCalibrada CalibradorINA219::leer(INA219Simple &ina) {
  LecturaCalibrada l;
  if (!_sensorOk) {
    l.hayAlerta = true;
    l.motivoAlerta = "Sensor no disponible";
    _alertaInterna = true;
    return l;
  }

  INA219Medida m = ina.leer();
  if (!m.valida) {
    _sensorOk = false;   // el main reintentará iniciar()
    l.hayAlerta = true;
    l.motivoAlerta = "Falla de comunicación con el INA219";
    _alertaInterna = true;
    return l;
  }

  l.valida                = true;
  l.saturada              = m.saturada || m.overflow;
  l.voltajeShunt_mV       = m.shunt_mV;
  l.voltajeCarga_V = m.bus_V * _cal.kV;
  l.voltajeBus_V = l.voltajeCarga_V + m.shunt_mV / 1000.0f;
  l.corrienteCruda_mA     = m.shunt_mV / _shuntNominal;
  l.corrienteCalibrada_mA = (m.shunt_mV - _cal.offset_mV) / _cal.rShunt_ohm;
  if (fabsf(l.corrienteCalibrada_mA) < CORRIENTE_RUIDO_mA) l.corrienteCalibrada_mA = 0;
  l.potencia_mW           = l.voltajeCarga_V * l.corrienteCalibrada_mA;   // V × mA = mW

  if (l.saturada) {
    l.hayAlerta = true;
    l.motivoAlerta = "Corriente fuera del rango del sensor";
  }
  _alertaInterna = l.hayAlerta;
  return l;
}

bool CalibradorINA219::hacerCero(INA219Simple &ina) {
  if (!_sensorOk) return false;
  double suma = 0;
  int validas = 0;
  for (int k = 0; k < 20; k++) {
    delay(ina.tiempoCiclo_ms());
    INA219Medida m = ina.leer();
    if (m.valida) { suma += m.shunt_mV; validas++; }
  }
  if (validas < 15) {
    Serial.println("Cero RECHAZADO: demasiadas lecturas fallidas");
    return false;
  }
  float off = suma / validas;
  if (fabsf(off) > 0.5f) {
    Serial.printf("Cero RECHAZADO: %.3f mV (= %s). ¿Sigue pasando corriente?\n", off, formatoSI(off / _shuntNominal / 1000.0f, "A").c_str());
    return false;
  }
  _cal.offset_mV = off;
  guardarEnMemoria(_cal);
  _tConfirmacion = millis();
  Serial.printf("Cero guardado: offset = %+.4f mV\n", _cal.offset_mV);
  return true;
}

// ---------------- Botón e indicadores ------------------------
bool CalibradorINA219::consumirPulsacion() {
  bool p = _pulsacion;
  _pulsacion = false;
  return p;
}

bool CalibradorINA219::consumirSolicitudCero() {
  bool s = _solicitudCero;
  _solicitudCero = false;
  return s;
}

void CalibradorINA219::actualizar() {
  uint32_t ahora = millis();
  bool b = digitalRead(_pinBoton);

  if (b != _btnAnterior && ahora - _tCambio > ANTIRREBOTE_MS) {
    _tCambio = ahora;
    _btnAnterior = b;
    if (b == LOW) {
      _tPresionado = ahora;
      _largoHecho = false;
    } else if (!_largoHecho) {
      _pulsacion = true;           // pulsación corta
    }
  }
  if (b == LOW && !_largoHecho && ahora - _tPresionado >= PULSACION_LARGA_MS) {
    _largoHecho = true;
    _solicitudCero = true;         // pulsación larga
  }

  // Cuando ya no hay alertas, el próximo aviso vuelve a sonar
  if (!_alertaInterna && !_alertaExterna) _silenciado = false;

  escribirIndicadores();
}

void CalibradorINA219::escribirIndicadores() {
  uint32_t t = millis();
  bool led = false, buz = false;

  if (!_sensorOk) {
    led = true;                                    // fijo: sensor ausente
  } else if (_tConfirmacion && t - _tConfirmacion < 900) {
    led = (t / 150) % 2;                           // parpadeo rápido: cero guardado
  } else if (_alertaInterna || _alertaExterna) {
    led = (t / 250) % 2 || _alertaInterna;         // parpadea en alarma, fijo si es interna
    buz = !_silenciado && (t % 1000) < 150;        // bip corto cada segundo
  }
  digitalWrite(_pinLed, led);
  if (_pinBuzzer != 255) digitalWrite(_pinBuzzer, buz);
}