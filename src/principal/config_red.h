#pragma once

struct ConfigRed {
  char ssid[33];
  char clave[65];
  char id[65];
};

bool wifiValida(const char *ssid, const char *clave);
bool idValido(const char *id);
ConfigRed cargarConfigRed();
bool guardarConfigRed(const ConfigRed &config);
