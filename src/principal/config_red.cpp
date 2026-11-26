#include <Preferences.h>
#include <string.h>
#include "config_red.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {
constexpr char ESPACIO_NVS[] = "medidor-red";
constexpr char CLAVE_NVS[] = "config-v1";

bool textoWifiValido(const char *texto) {
  for (const auto *p = reinterpret_cast<const unsigned char *>(texto); *p; ++p) {
    if (*p < 32 || *p == 127) return false;
  }
  return true;
}

bool esHex(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}
}  // namespace

bool wifiValida(const char *ssid, const char *clave) {
  size_t nSsid = strlen(ssid);
  size_t nClave = strlen(clave);
  if (nSsid == 0 || nSsid > 32 || (nClave > 0 && nClave < 8) || nClave > 64) return false;
  if (!textoWifiValido(ssid) || !textoWifiValido(clave)) return false;
  if (nClave == 64) {
    for (const char *p = clave; *p; ++p) {
      if (!esHex(*p)) return false;
    }
  }
  return true;
}

bool idValido(const char *id) {
  size_t n = strlen(id);
  if (n == 0 || n > 64) return false;
  for (const char *p = id; *p; ++p) {
    char ch = *p;
    bool letra = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
    if (!letra && !(ch >= '0' && ch <= '9') && ch != '-' && ch != '_') return false;
  }
  return true;
}

ConfigRed cargarConfigRed() {
  ConfigRed config{};
  Preferences prefs;
  if (prefs.begin(ESPACIO_NVS, false)) {
    size_t leidos = prefs.getBytesLength(CLAVE_NVS) == sizeof(config)
        ? prefs.getBytes(CLAVE_NVS, &config, sizeof(config)) : 0;
    prefs.end();
    if (leidos == sizeof(config) && memchr(config.ssid, 0, sizeof(config.ssid)) &&
        memchr(config.clave, 0, sizeof(config.clave)) && memchr(config.id, 0, sizeof(config.id)) &&
        ((!config.ssid[0] && !config.clave[0]) || wifiValida(config.ssid, config.clave)) &&
        (!config.id[0] || idValido(config.id))) {
      return config;
    }
  }

  config = {};
#if defined(WIFI_SSID) && defined(WIFI_PASS) && defined(DEVICE_ID)
  if (wifiValida(WIFI_SSID, WIFI_PASS)) {
    strlcpy(config.ssid, WIFI_SSID, sizeof(config.ssid));
    strlcpy(config.clave, WIFI_PASS, sizeof(config.clave));
  }
  if (idValido(DEVICE_ID)) strlcpy(config.id, DEVICE_ID, sizeof(config.id));
#endif
  return config;
}

bool guardarConfigRed(const ConfigRed &config) {
  Preferences prefs;
  if (!prefs.begin(ESPACIO_NVS, false)) return false;
  bool guardado = prefs.putBytes(CLAVE_NVS, &config, sizeof(config)) == sizeof(config);
  prefs.end();
  return guardado;
}
