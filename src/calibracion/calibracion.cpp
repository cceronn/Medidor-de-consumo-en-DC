// =============================================================
//  PROGRAMA EXTRA: CALIBRACIÓN DEL INA219
//  ESP32 DevKit V1 – Medidor DC con protecciones
//
//  Se maneja con comandos desde el Monitor Serie (115200 baudios).
//  Escribe "h" + Enter para ver la ayuda.
//
//  IMPORTANTE: la carga solo recibe corriente cuando Q2 conduce
//  (GPIO27 en ALTO). Este programa arranca con la carga
//  DESCONECTADA; usa "k" para conectarla.
// =============================================================
#include <Arduino.h>
#include "formato_si.h"
#include <Wire.h>
#include "INA219Simple.h"
#include "Calibration.h"
#include "hardware_config.h"

// ---------------- Parámetros ---------------------------------
constexpr INA219Promedio PROMEDIO_CAL = INA219Promedio::x128;  // máxima estabilidad
constexpr int MUESTRAS_PUNTO = 10;      // ciclos promediados por punto (~1,4 s)
constexpr int MUESTRAS_CERO  = 20;      // ciclos para el cero (~2,8 s)
constexpr int MAX_PUNTOS     = 10;
constexpr uint32_t PERIODO_STREAM_MS = 1000;

INA219Simple ina(INA219_DIRECCION);
CalibracionINA219 cal;          // calibración activa (RAM)
float rangoMax_mA = 800.0f;

struct PuntoI { float vsh_mV; float iRef; };
struct PuntoV { float vMed;   float vRef; };
PuntoI puntosI[MAX_PUNTOS]; int nI = 0;
PuntoV puntosV[MAX_PUNTOS]; int nV = 0;

bool inaOk = false;
bool cargaOn = false;
bool streaming = false;
uint32_t tStream = 0;

// ---------------- Carga (Q2) ---------------------------------
void ponerCarga(bool on) {
  cargaOn = on;
  digitalWrite(PIN_CORTE, on ? HIGH : LOW);
}

// ---------------- Medición -----------------------------------
struct Medida {
  float vsh_mV = 0;   // voltaje en el shunt (crudo)
  float vbus_V = 0;   // voltaje de bus (crudo, sin factor)
  bool  ok = false;
  bool  saturada = false;
};

// Promedia n ciclos. Si detecta saturación, desconecta la carga.
Medida medir(int n) {
  Medida m;
  double sumaSh = 0, sumaBus = 0;
  int validas = 0;
  for (int k = 0; k < n; k++) {
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
    delay(ina.tiempoCiclo_ms());
    INA219Medida r = ina.leer();
    if (!r.valida) continue;
    if (r.saturada || r.overflow) {
      m.saturada = true;
      ponerCarga(false);
      Serial.println("PROTECCIÓN: corriente fuera de rango, carga DESCONECTADA.");
      break;
    }
    sumaSh += r.shunt_mV;
    sumaBus += r.bus_V;
    validas++;
  }
  digitalWrite(PIN_LED, LOW);
  if (validas > 0 && !m.saturada) {
    m.vsh_mV = sumaSh / validas;
    m.vbus_V = sumaBus / validas;
    m.ok = true;
  }
  return m;
}

float corrienteCal_mA(float vsh_mV) { return (vsh_mV - cal.offset_mV) / cal.rShunt_ohm; }
float corrienteNom_mA(float vsh_mV) { return vsh_mV / SHUNT_NOMINAL_OHM; }
float voltajeCal(float vbus_V)      { return vbus_V * cal.kV; }

void imprimirMedida(const Medida &m) {
  if (!m.ok) { Serial.println("Sin lectura válida."); return; }
  float i = corrienteCal_mA(m.vsh_mV);
  float v = voltajeCal(m.vbus_V);
  Serial.printf("Vbus=%7.3f V | Vcarga=%7.3f V | Vshunt=%8.3f mV | I=%s (sin cal: %s) | P=%s | carga %s\n",
                v + m.vsh_mV / 1000.0f, v, m.vsh_mV, formatoSI(i / 1000.0f, "A").c_str(),
                formatoSI(corrienteNom_mA(m.vsh_mV) / 1000.0f, "A").c_str(), formatoSI(v * i / 1000.0f, "W").c_str(), cargaOn ? "ON" : "OFF");
}

void mostrarCal() {
  Serial.println("------ Calibración activa ------");
  Serial.printf("  Rango            : ±%.0f mV en el shunt = %s\n", ina.rangoShunt_mV(), formatoSI(rangoMax_mA / 1000.0f, "A").c_str());
  Serial.printf("  R shunt efectiva : %.6f ohm (nominal %.3f, diferencia %+.2f %%)\n",
                cal.rShunt_ohm, SHUNT_NOMINAL_OHM, (cal.rShunt_ohm / SHUNT_NOMINAL_OHM - 1) * 100);
  Serial.printf("  Offset del shunt : %+.4f mV (= %s)\n", cal.offset_mV, formatoSI(cal.offset_mV / cal.rShunt_ohm / 1000.0f, "A").c_str());
  Serial.printf("  Factor de voltaje: %.5f\n", cal.kV);
  Serial.printf("  Puntos tomados   : %d de corriente, %d de voltaje | carga %s\n", nI, nV, cargaOn ? "ON" : "OFF");
  Serial.println("--------------------------------");
}

// ---------------- Cálculo ------------------------------------
void calcular() {
  if (nI == 0 && nV == 0) {
    Serial.println("No hay puntos. Usa 'i <amperios>' o 'v <voltios>' primero.");
    return;
  }
  if (nI > 0) {
    // Mínimos cuadrados V = R * I (recta por el origen, tras restar el offset)
    double sVI = 0, sII = 0;
    for (int k = 0; k < nI; k++) {
      double v = puntosI[k].vsh_mV - cal.offset_mV;        // mV
      double iA = puntosI[k].iRef;                          // A
      sVI += v * iA;
      sII += iA * iA;
    }
    float r = (sVI / sII) / 1000.0;                         // mV/A -> ohm
    if (r < SHUNT_NOMINAL_OHM * 0.8f || r > SHUNT_NOMINAL_OHM * 1.2f) {
      Serial.printf("RECHAZADO: R calculada = %.5f ohm (más de 20 %% del nominal). Revisa los puntos.\n", r);
    } else {
      cal.rShunt_ohm = r;
    }
    Serial.println("\n  #   I multímetro   I sin cal    error    I calibrada   error");
    for (int k = 0; k < nI; k++) {
      float ir = puntosI[k].iRef;
      float in = corrienteNom_mA(puntosI[k].vsh_mV) / 1000.0f;
      float ic = corrienteCal_mA(puntosI[k].vsh_mV) / 1000.0f;
      Serial.printf("  %d   %12s   %12s  %+6.2f%%   %12s  %+6.2f%%\n",
                    k + 1, formatoSI(ir, "A").c_str(), formatoSI(in, "A").c_str(), (in / ir - 1) * 100, formatoSI(ic, "A").c_str(), (ic / ir - 1) * 100);
    }
  }
  if (nV > 0) {
    double sRM = 0, sMM = 0;
    for (int k = 0; k < nV; k++) {
      sRM += puntosV[k].vRef * puntosV[k].vMed;
      sMM += puntosV[k].vMed * puntosV[k].vMed;
    }
    float kv = sRM / sMM;
    if (kv < 0.9f || kv > 1.1f) {
      Serial.printf("RECHAZADO: factor de voltaje %.4f (más de 10 %%). Revisa dónde mides.\n", kv);
    } else {
      cal.kV = kv;
    }
    Serial.println("\n  #   V multímetro   V sin cal   V calibrado   error");
    for (int k = 0; k < nV; k++) {
      float vc = voltajeCal(puntosV[k].vMed);
      Serial.printf("  %d   %9.3f V   %8.3f V   %9.3f V  %+6.2f%%\n",
                    k + 1, puntosV[k].vRef, puntosV[k].vMed, vc, (vc / puntosV[k].vRef - 1) * 100);
    }
  }
  Serial.println();
  mostrarCal();
  Serial.println("Si los errores calibrados son pequeños (< 1 %), escribe 'g' para guardar.");
}

// ---------------- Comandos -----------------------------------
void ayuda() {
  Serial.println(R"(
=============== CALIBRACIÓN INA219 ===============
 k            Conectar / desconectar la carga (Q2, GPIO27)
 m            Una medición promediada
 s            Medición continua cada 1 s (on/off)
 z            Cero: desconecta la carga, mide y guarda el offset
 i <A>        Punto de corriente. Ej: i 0.256
              (lo que marca el multímetro EN SERIE, carga ON)
 v <V>        Punto de voltaje. Ej: v 11.98
              (multímetro entre VIN- / J2+ y GND)
 c            Calcular la calibración con los puntos
 g            Guardar en memoria (la usa el programa principal)
 p            Ver la calibración activa
 b            Borrar los puntos tomados
 r            Volver a valores nominales (0,1 ohm)
 h            Esta ayuda
 Botón SW1 (GPIO33): una medición rápida
==================================================)");
}

void procesar(char *cmd) {
  while (*cmd == ' ') cmd++;
  for (char *q = cmd; *q; q++) if (*q == ',') *q = '.';   // acepta coma decimal
  if (*cmd == '\0') return;
  char c = tolower(cmd[0]);
  float valor = NAN;
  if (cmd[1] != '\0') {
    char *fin = nullptr;
    float x = strtof(cmd + 1, &fin);
    if (fin != cmd + 1) valor = x;          // hubo un número válido
  }

  if (!inaOk && c != 'h') {
    Serial.printf("ERROR: INA219 no detectado. Revisa SDA=%d, SCL=%d, VCC=3V3, GND.\n", PIN_SDA, PIN_SCL);
    return;
  }

  switch (c) {
    case 'h': ayuda(); break;
    case 'k':
      ponerCarga(!cargaOn);
      Serial.println(cargaOn ? "Carga CONECTADA (Q2 ON)" : "Carga DESCONECTADA (Q2 OFF)");
      break;
    case 'm': Serial.println("Midiendo..."); imprimirMedida(medir(MUESTRAS_PUNTO)); break;
    case 's':
      streaming = !streaming;
      Serial.println(streaming ? "Medición continua ACTIVADA ('s' para parar)" : "Medición continua DETENIDA");
      break;
    case 'z': {
      bool estaba = cargaOn;
      ponerCarga(false);
      Serial.println("Cero: carga desconectada, midiendo...");
      delay(300);
      Medida m = medir(MUESTRAS_CERO);
      ponerCarga(estaba);
      if (!m.ok) { Serial.println("ERROR de lectura."); break; }
      if (fabsf(m.vsh_mV) > 0.5f) {
        Serial.printf("RECHAZADO: %.3f mV (%s) sin carga. ¿Hay otro camino de corriente?\n",
                      m.vsh_mV, formatoSI(m.vsh_mV / SHUNT_NOMINAL_OHM / 1000.0f, "A").c_str());
        break;
      }
      cal.offset_mV = m.vsh_mV;
      Serial.printf("OK: offset = %+.4f mV (usa 'g' para guardar)\n", cal.offset_mV);
      break;
    }
    case 'i': {
      if (isnan(valor) || valor < 0.01f) { Serial.println("Uso: i 0.256  (amperios del multímetro, mínimo 0.01)"); break; }
      if (!cargaOn) { Serial.println("La carga está desconectada: usa 'k' primero."); break; }
      if (valor * 1000.0f > rangoMax_mA) { Serial.printf("Ese valor supera el rango (%s).\n", formatoSI(rangoMax_mA / 1000.0f, "A").c_str()); break; }
      if (nI >= MAX_PUNTOS) { Serial.println("Máximo de puntos alcanzado. 'b' para borrar."); break; }
      Serial.println("Midiendo punto de corriente...");
      Medida m = medir(MUESTRAS_PUNTO);
      if (!m.ok) { Serial.println("ERROR: lectura inválida o saturada."); break; }
      puntosI[nI++] = {m.vsh_mV, valor};
      Serial.printf("Punto %d: multímetro = %s | INA219 sin cal = %s\n",
                    nI, formatoSI(valor, "A").c_str(), formatoSI(corrienteNom_mA(m.vsh_mV) / 1000.0f, "A").c_str());
      break;
    }
    case 'v': {
      if (isnan(valor) || valor < 0.5f) { Serial.println("Uso: v 11.98  (voltios del multímetro)"); break; }
      if (nV >= MAX_PUNTOS) { Serial.println("Máximo de puntos alcanzado. 'b' para borrar."); break; }
      Serial.println("Midiendo punto de voltaje...");
      Medida m = medir(MUESTRAS_PUNTO);
      if (!m.ok) { Serial.println("ERROR de lectura."); break; }
      puntosV[nV++] = {m.vbus_V, valor};
      Serial.printf("Punto %d: multímetro = %.3f V | INA219 = %.3f V\n", nV, valor, m.vbus_V);
      break;
    }
    case 'c': calcular(); break;
    case 'g':
      CalibradorINA219::guardarEnMemoria(cal);
      Serial.println("OK: calibración guardada. El programa principal la usará al arrancar.");
      break;
    case 'p': mostrarCal(); break;
    case 'b': nI = nV = 0; Serial.println("Puntos borrados."); break;
    case 'r':
      cal = {SHUNT_NOMINAL_OHM, 0.0f, 1.0f};
      Serial.println("Valores nominales restaurados (usa 'g' para guardarlos).");
      break;
    default:
      Serial.println("Comando no reconocido. Escribe 'h' para ver la ayuda.");
  }
}

// ---------------- Setup / Loop -------------------------------
bool iniciarSensor() {
  INA219Rango pga = CalibradorINA219::rangoPGA(RANGO_SENSOR);
  if (!ina.begin(Wire, pga, PROMEDIO_CAL)) return false;
  rangoMax_mA = ina.rangoShunt_mV() / SHUNT_NOMINAL_OHM;
  return true;
}

void setup() {
  pinMode(PIN_CORTE, OUTPUT);
  ponerCarga(false);                 // arranca con la carga desconectada
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BOTON, INPUT_PULLUP);

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nMedidor DC - Modo CALIBRACIÓN INA219");

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_FRECUENCIA_HZ);

  inaOk = iniciarSensor();
  if (inaOk) Serial.printf("INA219 detectado en 0x%02X (promedio 128 muestras).\n", ina.direccion());
  else Serial.printf("ERROR: INA219 NO detectado. Revisa SDA=%d, SCL=%d, VCC=3V3, GND.\n", PIN_SDA, PIN_SCL);

  bool valida = false;
  cal = CalibradorINA219::cargarDeMemoria(SHUNT_NOMINAL_OHM, &valida);
  if (!valida) Serial.println("Sin calibración guardada: se usan valores nominales.");
  mostrarCal();
  ayuda();
}

void loop() {
  // Reintento del sensor
  static uint32_t tReintento = 0;
  if (!inaOk && millis() - tReintento > 3000) {
    tReintento = millis();
    inaOk = iniciarSensor();
    if (inaOk) Serial.println("INA219 detectado. Listo.");
  }

  // Comandos por serie (no bloquea)
  static char linea[48];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (n) { linea[n] = '\0'; procesar(linea); n = 0; }
    } else if (n < sizeof(linea) - 1) {
      linea[n++] = ch;
    }
  }

  // Medición continua
  if (streaming && inaOk && millis() - tStream >= PERIODO_STREAM_MS) {
    tStream = millis();
    imprimirMedida(medir(1));
  }

  // Vigilancia mínima con la carga conectada (aunque no se esté midiendo)
  static uint32_t tVigila = 0;
  if (inaOk && cargaOn && !streaming && millis() - tVigila > 200) {
    tVigila = millis();
    INA219Medida r = ina.leer();
    if (r.valida && (r.saturada || r.overflow)) {
      ponerCarga(false);
      Serial.println("PROTECCIÓN: corriente fuera de rango, carga DESCONECTADA.");
    }
  }

  // Botón: medición rápida
  static bool btnAnterior = HIGH;
  static uint32_t tBtn = 0;
  bool b = digitalRead(PIN_BOTON);
  if (b != btnAnterior && millis() - tBtn > 50) {
    tBtn = millis();
    btnAnterior = b;
    if (b == LOW && inaOk) {
      Serial.println("[Botón] Medición:");
      imprimirMedida(medir(MUESTRAS_PUNTO));
    }
  }
}