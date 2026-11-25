#pragma once
#include <Arduino.h>
#include <math.h>

inline String formatoSI(float valor, const char *unidad) {
  const bool mili = fabsf(valor) < 1.0f;
  return String(mili ? valor * 1000.0f : valor, 2) + (mili ? " m" : " ") + unidad;
}
