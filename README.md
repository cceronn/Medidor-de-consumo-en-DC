# Medidor DC con protecciones – ESP32 DevKit V1 + INA219 + MQTT

Firmware para el montaje "Medidor de 12 V sin optoacoplador" (montaje de prueba).

```
medidor_dc_CORREGIDO/
├── platformio.ini                 dos entornos: principal y calibracion
├── include/
│   ├── hardware_config.h          pines, dirección I2C, shunt y rango (única fuente)
│   └── secrets.example.h                  plantilla de configuración WiFi y DEVICE_ID
├── lib/
│   ├── INA219Simple/              driver del INA219 (sin librerías externas)
│   └── CalibradorINA219/          calibración, LED, buzzer y botón
├── src/
│   ├── principal/main.cpp         medidor + protecciones + MQTT
│   └── calibracion/calibracion.cpp  programa de calibración
└── web/index.html                 página de monitoreo (abrir con doble clic)
```

## Pines (según el esquemático)

| Señal | GPIO | Hacia |
|---|---|---|
| SDA | 21 | U2 INA219 SDA |
| SCL | 22 | U2 INA219 SCL |
| Corte de carga | 27 | R4 100 Ω → gate de Q2 IRLZ44N (R5 10 kΩ a GND) |
| Buzzer | 26 | R8 1 kΩ → base de Q3 → BZ1 |
| LED de alarma | 25 | R10 1 kΩ → base de Q4 → D5 |
| Botón | 33 | R11 330 Ω → SW1 a GND (C4 100 nF), pull-up interno |

## Uso rápido

1. Copia la plantilla de configuración y edita el archivo local con tu red WiFi de 2,4 GHz y un ID único. Git ignora el archivo local.
```powershell
Copy-Item include/secrets.example.h include/secrets.h
```

2. Calibrar: entorno **env:calibracion** → Upload → Monitor. Comandos: `k` (carga ON/OFF), `z`, `i <A>`, `c`, `g`.
3. Medir: entorno **env:principal** → Upload → Monitor.
4. Página: abrir `web/index.html`, mismo `DEVICE_ID`, **Conectar**.

## Monitor serie del programa principal

`estado` · `lim <iAlarma> <iCorte> <vMin> <vMax>` · `rearmar` · `cero` · `carga on` · `carga off`

## Botón SW1 (programa principal)

- Pulsación corta: rearma si hubo corte; si no, silencia el buzzer.
- 3 segundos: cero del sensor (desconecta la carga un momento, mide y guarda).

## Estados

| Estado | Condición | Acción |
|---|---|---|
| NORMAL | dentro de límites | carga conectada |
| ALARMA | I ≥ alarma o V < Vmin | LED parpadea, bip cada 1 s, carga sigue |
| CORTE | I ≥ corte, V ≥ Vmax, saturación o falla del sensor | GPIO27 en bajo (carga desconectada) hasta rearmar |

## MQTT (broker público HiveMQ)

- ESP32: `broker.hivemq.com:1883` · Página: `wss://broker.hivemq.com:8884/mqtt`
- Base: `medidordc/<DEVICE_ID>` → `/datos`, `/estado`, `/evento`, `/config` (ESP32 → web) y `/cmd` (web → ESP32).
