// =============================================================
//  MEDIDOR DC CON PROTECCIONES – PROGRAMA PRINCIPAL
//  ESP32 DevKit V1 + INA219 + MQTT (broker público HiveMQ)
//  Esquemático: "Medidor de 12 V sin optoacoplador" (montaje de prueba)
//
//  Arquitectura:
//   - loop() (núcleo 1): medición, protecciones, LED, buzzer, botón.
//     NUNCA se bloquea por la red: la protección sigue aunque no
//     haya WiFi o el broker no responda.
//   - tareaRed (núcleo 0): WiFi + MQTT. Se comunica con loop() por
//     colas y por una "instantánea" protegida.
// =============================================================
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "INA219Simple.h"
#include "Calibration.h"
#include "hardware_config.h"
#include "secrets.h"
#include "formato_si.h"

// =============================================================
//  1) CONFIGURACIÓN
// =============================================================
const char *MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char *TOPIC_BASE = "medidordc";

// El rango del sensor (RANGO_SENSOR) está en include/hardware_config.h

// Límites por defecto (también se cambian desde la web o por serie)
constexpr float LIM_I_ALARMA = 0.60f;   // A  -> alarma (LED + buzzer)
constexpr float LIM_I_CORTE  = 0.75f;   // A  -> corte de la carga (Q2)
constexpr float LIM_V_MIN    = 10.0f;   // V  -> alarma por voltaje bajo
constexpr float LIM_V_MAX    = 14.0f;   // V  -> corte por sobrevoltaje
constexpr bool  FORZAR_LIMITES_DEL_CODIGO = false;  // true = ignora los guardados

// La carga se conecta sola al arrancar si el sensor responde
constexpr bool CARGA_HABILITADA_AL_ARRANCAR = true;

constexpr uint32_t PERIODO_LECTURA_MS     = 100;
constexpr uint32_t PERIODO_IMPRESION_MS   = 1000;
constexpr uint32_t PERIODO_PUBLICACION_MS = 1000;
constexpr uint32_t PERIODO_REINTENTO_SENSOR_MS = 2000;

// =============================================================
//  2) OBJETOS Y ESTADO
// =============================================================
INA219Simple ina(INA219_DIRECCION);
CalibradorINA219 calibrador(PIN_BOTON, PIN_LED, SHUNT_NOMINAL_OHM, PIN_BUZZER);

struct Limites { float iAlarma, iCorte, vMin, vMax; };
enum Estado : uint8_t { NORMAL, ALARMA, CORTE };

// --- Estado propio de loop() ---
Limites lim;
Estado estado = NORMAL;
char motivo[48] = "";
bool cargaHabilitada = CARGA_HABILITADA_AL_ARRANCAR;
float iMaxSensor_A = 0.8f;
LecturaCalibrada lec;
float V = 0, I = 0, P = 0, iMax = 0;
double energiaWh = 0;
uint32_t tLectura = 0, tImpresion = 0, tReintentoSensor = 0, tUltimaLectura = 0;

// --- Instantánea compartida loop() -> tareaRed ---
struct Instantanea {
  float v, vCarga, i, p, imax;
  double eWh;
  Estado estado;
  char motivo[48];
  bool carga, sensor;
  Limites lim;
  float iMaxSensor;
};
Instantanea instantanea;
portMUX_TYPE muxInstantanea = portMUX_INITIALIZER_UNLOCKED;
volatile bool configCambiada = true;

// --- Colas ---
enum TipoComando : uint8_t { CMD_LIMITES, CMD_REARMAR, CMD_SILENCIAR, CMD_CARGA, CMD_RESET_ENERGIA };
struct Comando { TipoComando tipo; float iAlarma, iCorte, vMin, vMax; bool on; };
struct Evento  { char tipo[12]; char msg[96]; float v, vCarga, i; };
QueueHandle_t colaComandos;   // tareaRed -> loop()
QueueHandle_t colaEventos;    // loop()   -> tareaRed

// --- Red (solo la usa tareaRed) ---
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
String tDatos, tEstado, tEvento, tConfig, tCmd;

// =============================================================
//  3) UTILIDADES
// =============================================================
const char *estadoTexto(Estado e) {
  return e == CORTE ? "CORTE" : (e == ALARMA ? "ALARMA" : "NORMAL");
}

void publicarEvento(const char *tipo, const char *msg) {
  Serial.printf("[EVENTO] %s: %s (Bus=%.3f V, Carga=%.3f V, I=%s)\n", tipo, msg, lec.voltajeBus_V, V, formatoSI(I, "A").c_str());
  Evento ev;
  strlcpy(ev.tipo, tipo, sizeof(ev.tipo));
  strlcpy(ev.msg, msg, sizeof(ev.msg));
  ev.v = lec.voltajeBus_V;
  ev.vCarga = V;
  ev.i = I;
  xQueueSend(colaEventos, &ev, 0);   // si la cola está llena se descarta (no bloquea)
}

void aplicarCarga() {
  bool conectar = cargaHabilitada && estado != CORTE && calibrador.sensorOk();
  digitalWrite(PIN_CORTE, conectar ? HIGH : LOW);
}

void actualizarInstantanea() {
  portENTER_CRITICAL(&muxInstantanea);
  instantanea.v = lec.voltajeBus_V;
  instantanea.vCarga = V;
  instantanea.i = I;
  instantanea.p = P;
  instantanea.imax = iMax;
  instantanea.eWh = energiaWh;
  instantanea.estado = estado;
  memcpy(instantanea.motivo, motivo, sizeof(motivo));
  instantanea.carga = cargaHabilitada;
  instantanea.sensor = calibrador.sensorOk();
  instantanea.lim = lim;
  instantanea.iMaxSensor = iMaxSensor_A;
  portEXIT_CRITICAL(&muxInstantanea);
}

// =============================================================
//  4) LÍMITES
// =============================================================
bool limitesValidos(const Limites &l) {
  return l.iAlarma > 0 && l.iAlarma < l.iCorte && l.iCorte <= iMaxSensor_A &&
         l.vMin >= 0 && l.vMin < l.vMax && l.vMax <= V_MAX_INA219;
}

void guardarLimites() {
  Preferences prefs;
  prefs.begin("lim", false);
  prefs.putFloat("ia", lim.iAlarma);
  prefs.putFloat("ic", lim.iCorte);
  prefs.putFloat("vmin", lim.vMin);
  prefs.putFloat("vmax", lim.vMax);
  prefs.end();
}

void cargarLimites() {
  Limites codigo = {LIM_I_ALARMA, LIM_I_CORTE, LIM_V_MIN, LIM_V_MAX};
  lim = codigo;
  if (!FORZAR_LIMITES_DEL_CODIGO) {
    Preferences prefs;
    prefs.begin("lim", true);
    lim.iAlarma = prefs.getFloat("ia", lim.iAlarma);
    lim.iCorte  = prefs.getFloat("ic", lim.iCorte);
    lim.vMin    = prefs.getFloat("vmin", lim.vMin);
    lim.vMax    = prefs.getFloat("vmax", lim.vMax);
    prefs.end();
  }
  if (!limitesValidos(lim)) lim = codigo;
  if (!limitesValidos(lim)) {
    // Los del código no caben en el rango elegido: se ajustan con margen
    lim.iCorte = iMaxSensor_A * 0.90f;
    lim.iAlarma = iMaxSensor_A * 0.75f;
    Serial.println("AVISO: los límites del código superan el rango del sensor; se ajustaron.");
  }
}

// =============================================================
//  5) PROTECCIÓN
// =============================================================
void cortar(const char *m) {
  estado = CORTE;
  strlcpy(motivo, m, sizeof(motivo));
  aplicarCarga();
  publicarEvento("corte", m);
}

void rearmar() {
  if (estado != CORTE) return;
  if (!calibrador.sensorOk()) {
    publicarEvento("error", "No se puede rearmar: el sensor no responde");
    return;
  }
  estado = NORMAL;
  motivo[0] = '\0';
  aplicarCarga();
  publicarEvento("rearme", "Sistema rearmado");
}

void evaluarProtecciones() {
  if (estado == CORTE) return;

  // Estado seguro: sin sensor no hay protección -> se corta la carga
  if (!lec.valida) { cortar("Falla del sensor INA219"); return; }
  if (lec.saturada) { cortar("Corriente fuera del rango del sensor"); return; }
  if (I >= lim.iCorte) { cortar("Sobrecorriente"); return; }
  if (V >= lim.vMax)   { cortar("Sobrevoltaje"); return; }

  bool iAlta = I >= lim.iAlarma;
  bool vBaja = V > 1.0f && V < lim.vMin;
  if (estado == NORMAL && (iAlta || vBaja)) {
    estado = ALARMA;
    strlcpy(motivo, iAlta ? "Corriente alta" : "Voltaje bajo", sizeof(motivo));
    publicarEvento("alarma", motivo);
  } else if (estado == ALARMA) {
    bool iOk = I < lim.iAlarma * 0.95f;                 // histéresis 5 %
    bool vOk = V <= 1.0f || V > lim.vMin * 1.02f;       // histéresis 2 %
    if (iOk && vOk) {
      estado = NORMAL;
      motivo[0] = '\0';
      publicarEvento("normal", "Valores normales");
    }
  }
}

// =============================================================
//  6) COMANDOS (web y monitor serie) – se ejecutan en loop()
// =============================================================
void ejecutarComando(const Comando &c) {
  switch (c.tipo) {
    case CMD_LIMITES: {
      Limites n = lim;
      if (!isnan(c.iAlarma)) n.iAlarma = c.iAlarma;
      if (!isnan(c.iCorte))  n.iCorte  = c.iCorte;
      if (!isnan(c.vMin))    n.vMin    = c.vMin;
      if (!isnan(c.vMax))    n.vMax    = c.vMax;
      if (limitesValidos(n)) {
        lim = n;
        guardarLimites();
        publicarEvento("config", "Límites actualizados");
      } else {
        char m[96];
        snprintf(m, sizeof(m), "Límites rechazados: alarma < corte <= %s y Vmin < Vmax <= %.0f V",
                 formatoSI(iMaxSensor_A, "A").c_str(), V_MAX_INA219);
        publicarEvento("error", m);
      }
      configCambiada = true;
      break;
    }
    case CMD_REARMAR:
      if (estado == CORTE) rearmar();
      else calibrador.silenciar();
      break;
    case CMD_SILENCIAR:
      calibrador.silenciar();
      break;
    case CMD_CARGA:
      cargaHabilitada = c.on;
      aplicarCarga();
      publicarEvento("carga", cargaHabilitada ? "Carga habilitada" : "Carga deshabilitada");
      configCambiada = true;
      break;
    case CMD_RESET_ENERGIA:
      energiaWh = 0;
      iMax = 0;
      publicarEvento("info", "Energía y corriente máxima reiniciadas");
      break;
  }
}

void hacerCero() {
  if (!calibrador.sensorOk()) return;
  Serial.println("Haciendo cero: carga desconectada temporalmente...");
  digitalWrite(PIN_CORTE, LOW);
  delay(300);                       // deja caer la corriente de la carga
  bool ok = calibrador.hacerCero(ina);
  aplicarCarga();
  publicarEvento(ok ? "info" : "error", ok ? "Cero del sensor guardado" : "Cero rechazado (pasa corriente)");
}

// Lector de líneas del monitor serie que NUNCA bloquea
void revisarSerie() {
  static char linea[64];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') {
      if (n < sizeof(linea) - 1) linea[n++] = ch;
      continue;
    }
    if (n == 0) continue;
    linea[n] = '\0';
    n = 0;

    Comando c{CMD_LIMITES, NAN, NAN, NAN, NAN, false};
    if (strncmp(linea, "lim", 3) == 0) {
      if (sscanf(linea + 3, "%f %f %f %f", &c.iAlarma, &c.iCorte, &c.vMin, &c.vMax) == 4) ejecutarComando(c);
      else Serial.println("Uso: lim <iAlarma> <iCorte> <vMin> <vMax>   ej: lim 0.6 0.75 10 14");
    } else if (strcmp(linea, "rearmar") == 0) {
      rearmar();
    } else if (strcmp(linea, "cero") == 0) {
      hacerCero();
    } else if (strcmp(linea, "carga on") == 0 || strcmp(linea, "carga off") == 0) {
      c.tipo = CMD_CARGA;
      c.on = strcmp(linea, "carga on") == 0;
      ejecutarComando(c);
    } else if (strcmp(linea, "estado") == 0) {
      Serial.printf("Estado %s | %s | carga %s | sensor %s | límites: %s/%s, %.1f-%.1f V | rango %s\n",
                    estadoTexto(estado), motivo, cargaHabilitada ? "ON" : "OFF",
                    calibrador.sensorOk() ? "OK" : "FALLA", formatoSI(lim.iAlarma, "A").c_str(), formatoSI(lim.iCorte, "A").c_str(), lim.vMin, lim.vMax, formatoSI(iMaxSensor_A, "A").c_str());
    } else {
      Serial.println("Comandos: estado | lim <iA> <iC> <vMin> <vMax> | rearmar | cero | carga on | carga off");
    }
  }
}

// =============================================================
//  7) RED (tarea en el núcleo 0)
// =============================================================
void alRecibirMQTT(char *topic, byte *payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;
  const char *accion = doc["accion"] | "";
  Comando c{CMD_LIMITES, NAN, NAN, NAN, NAN, false};

  if (strcmp(accion, "limites") == 0) {
    c.tipo = CMD_LIMITES;
    c.iAlarma = doc["iAlarma"] | NAN;
    c.iCorte  = doc["iCorte"]  | NAN;
    c.vMin    = doc["vMin"]    | NAN;
    c.vMax    = doc["vMax"]    | NAN;
  } else if (strcmp(accion, "rearmar") == 0) {
    c.tipo = CMD_REARMAR;
  } else if (strcmp(accion, "silenciar") == 0) {
    c.tipo = CMD_SILENCIAR;
  } else if (strcmp(accion, "carga") == 0) {
    if (!doc["on"].is<bool>()) return;
    c.tipo = CMD_CARGA;
    c.on = doc["on"].as<bool>();
  } else if (strcmp(accion, "reset_energia") == 0) {
    c.tipo = CMD_RESET_ENERGIA;
  } else {
    return;
  }
  xQueueSend(colaComandos, &c, 0);
}

Instantanea copiarInstantanea() {
  Instantanea s;
  portENTER_CRITICAL(&muxInstantanea);
  s = instantanea;
  portEXIT_CRITICAL(&muxInstantanea);
  return s;
}

void publicarConfig() {
  Instantanea s = copiarInstantanea();
  JsonDocument doc;
  doc["iAlarma"]    = s.lim.iAlarma;
  doc["iCorte"]     = s.lim.iCorte;
  doc["vMin"]       = s.lim.vMin;
  doc["vMax"]       = s.lim.vMax;
  doc["carga"]      = s.carga;
  doc["iMaxSensor"] = s.iMaxSensor;
  char buf[256];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(tConfig.c_str(), (const uint8_t *)buf, n, true);   // retenido
}

void publicarDatos() {
  Instantanea s = copiarInstantanea();
  JsonDocument doc;
  doc["v"]      = serialized(String(s.v, 3));
  doc["vCarga"] = s.vCarga;
  doc["i"]      = s.i;
  doc["p"]      = s.p;
  doc["e"]      = serialized(String(s.eWh, 4));
  doc["imax"]   = serialized(String(s.imax, 4));
  doc["estado"] = estadoTexto(s.estado);
  doc["motivo"] = s.motivo;
  doc["carga"]  = s.carga;
  doc["sensor"] = s.sensor;
  doc["rssi"]   = WiFi.RSSI();
  doc["up"]     = millis() / 1000;
  char buf[384];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  mqtt.publish(tDatos.c_str(), (const uint8_t *)buf, n, false);
}

void publicarEventosPendientes() {
  Evento ev;
  while (xQueueReceive(colaEventos, &ev, 0) == pdTRUE) {
    JsonDocument doc;
    doc["tipo"] = ev.tipo;
    doc["msg"]  = ev.msg;
    doc["v"]    = serialized(String(ev.v, 3));
    doc["vCarga"] = ev.vCarga;
    doc["i"]    = ev.i;
    char buf[256];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    mqtt.publish(tEvento.c_str(), (const uint8_t *)buf, n, false);
  }
}

void tareaRed(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(alRecibirMQTT);

  String clientId = String("esp32-") + DEVICE_ID + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  uint32_t tWifi = millis(), tMqtt = 0, tPub = 0;
  bool avisadoWifi = false;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      avisadoWifi = false;
      if (millis() - tWifi > 15000) {
        tWifi = millis();
        Serial.println("[RED] Reintentando WiFi...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    if (!avisadoWifi) {
      Serial.printf("[RED] WiFi conectado: %s (%d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      avisadoWifi = true;
    }

    if (!mqtt.connected()) {
      if (millis() - tMqtt > 5000) {
        tMqtt = millis();
        Serial.printf("[RED] Conectando a MQTT %s... ", MQTT_HOST);
        if (mqtt.connect(clientId.c_str(), nullptr, nullptr, tEstado.c_str(), 1, true, "offline")) {
          Serial.println("OK");
          mqtt.publish(tEstado.c_str(), "online", true);
          mqtt.subscribe(tCmd.c_str(), 1);
          configCambiada = true;
        } else {
          Serial.printf("falló (código %d)\n", mqtt.state());
        }
      }
    } else {
      mqtt.loop();
      publicarEventosPendientes();
      if (configCambiada) {
        configCambiada = false;
        publicarConfig();
      }
      if (millis() - tPub >= PERIODO_PUBLICACION_MS) {
        tPub = millis();
        publicarDatos();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =============================================================
//  8) SENSOR
// =============================================================
void intentarIniciarSensor() {
  if (calibrador.iniciar(ina, Wire, RANGO_SENSOR)) {
    publicarEvento("info", "Sensor INA219 en línea");
    aplicarCarga();
  } else {
    Serial.printf("ERROR: INA219 no responde en 0x%02X (SDA=%d, SCL=%d, VCC=3V3). Reintento en %lu s\n",
                  INA219_DIRECCION, PIN_SDA, PIN_SCL, (unsigned long)(PERIODO_REINTENTO_SENSOR_MS / 1000));
  }
}

void medir() {
  lec = calibrador.leer(ina);
  uint32_t ahora = millis();
  if (lec.valida) {
    V = lec.voltajeCarga_V;
    I = lec.corrienteCalibrada_mA / 1000.0f;
    P = lec.potencia_mW / 1000.0f;
    if (I > iMax) iMax = I;
    if (P > 0 && tUltimaLectura) energiaWh += P * ((ahora - tUltimaLectura) / 3600000.0);
    tUltimaLectura = ahora;
  } else {
    V = I = P = 0;
    tUltimaLectura = 0;
  }
}

void imprimir() {
  Serial.printf("V_bus: %6.3f V | V_carga: %6.3f V | V_shunt: %7.3f mV | I_cruda: %s | I_cal: %s | P: %s | %s",
                lec.voltajeBus_V, V, lec.voltajeShunt_mV, formatoSI(lec.corrienteCruda_mA / 1000.0f, "A").c_str(),
                formatoSI(I, "A").c_str(), formatoSI(P, "W").c_str(), estadoTexto(estado));
  if (motivo[0]) Serial.printf(" (%s)", motivo);
  if (!cargaHabilitada) Serial.print(" | carga OFF");
  Serial.println();
}

// =============================================================
//  9) SETUP / LOOP
// =============================================================
void setup() {
  pinMode(PIN_CORTE, OUTPUT);
  digitalWrite(PIN_CORTE, LOW);          // lo primero: carga desconectada
  calibrador.configurarPines();

  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Medidor DC - programa principal ===");

  colaComandos = xQueueCreate(8, sizeof(Comando));
  colaEventos  = xQueueCreate(12, sizeof(Evento));

  String base = String(TOPIC_BASE) + "/" + DEVICE_ID;
  tDatos  = base + "/datos";
  tEstado = base + "/estado";
  tEvento = base + "/evento";
  tConfig = base + "/config";
  tCmd    = base + "/cmd";

  // Rango nominal (con el shunt nominal) para validar límites
  iMaxSensor_A = INA219Simple::rangoEn_mV(CalibradorINA219::rangoPGA(RANGO_SENSOR)) / SHUNT_NOMINAL_OHM / 1000.0f;
  cargarLimites();

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_FRECUENCIA_HZ);
  intentarIniciarSensor();
  tReintentoSensor = millis();

  Serial.printf("Tópico MQTT: %s | Límites: alarma %s, corte %s, V %.1f-%.1f V | rango %s\n",
                base.c_str(), formatoSI(lim.iAlarma, "A").c_str(), formatoSI(lim.iCorte, "A").c_str(), lim.vMin, lim.vMax, formatoSI(iMaxSensor_A, "A").c_str());
  Serial.println("Escribe 'ayuda' para ver los comandos del monitor serie.");

  actualizarInstantanea();
  xTaskCreatePinnedToCore(tareaRed, "red", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
  uint32_t ahora = millis();

  calibrador.actualizar();

  // Botón: corta = rearmar (si hay corte) o silenciar; larga (3 s) = cero
  if (calibrador.consumirPulsacion()) {
    if (estado == CORTE) rearmar();
    else calibrador.silenciar();
  }
  if (calibrador.consumirSolicitudCero()) hacerCero();

  // Comandos llegados por MQTT
  Comando c;
  while (xQueueReceive(colaComandos, &c, 0) == pdTRUE) ejecutarComando(c);

  revisarSerie();

  // Reintento del sensor si no está
  if (!calibrador.sensorOk() && ahora - tReintentoSensor >= PERIODO_REINTENTO_SENSOR_MS) {
    tReintentoSensor = ahora;
    intentarIniciarSensor();
  }

  // Medición y protección
  if (ahora - tLectura >= PERIODO_LECTURA_MS) {
    tLectura = ahora;
    bool estabaOk = calibrador.sensorOk();
    medir();
    evaluarProtecciones();
    if (estabaOk && !calibrador.sensorOk()) tReintentoSensor = ahora;
    calibrador.setAlertaExterna(estado != NORMAL);
    actualizarInstantanea();

    if (ahora - tImpresion >= PERIODO_IMPRESION_MS) {
      tImpresion = ahora;
      imprimir();
    }
  }

  delay(1);   // cede tiempo al resto del sistema
}