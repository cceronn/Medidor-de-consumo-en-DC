# Medidor de consumo DC

Medidor de voltaje, corriente, potencia y energía para un montaje DC de hasta **12 V**, con un ESP32 DevKit V1, un sensor INA219 y un MOSFET de corte. El firmware toma lecturas, aplica las protecciones localmente, ofrece una **CLI por UART** y envía mediciones y eventos al monitor web por MQTT.

## Monitor web

- [Abrir el monitor en GitHub Pages](https://cceronn.github.io/Medidor-de-consumo-en-DC/)
- [Ver el código del monitor en GitHub](https://github.com/cceronn/Medidor-de-consumo-en-DC/blob/main/docs/index.html)
- [Ver el repositorio del proyecto](https://github.com/cceronn/Medidor-de-consumo-en-DC)

Escribe el mismo `DEVICE_ID` que devuelve la CLI con `ver` y pulsa **Conectar**. La página muestra lecturas en vivo, gráficas, estado de conexión y eventos. Desde la página puedes ajustar límites, silenciar el buzzer, rearmar la protección, habilitar o deshabilitar la carga y reiniciar la energía acumulada y el máximo de corriente. **Modo demo** permite explorar la página con datos simulados. También puedes abrir [web/index.html](web/index.html) en un navegador con acceso a internet.

La versión de GitHub Pages se sirve desde [docs/index.html](docs/index.html). Para publicar cambios en el monitor, sube ese archivo a la rama **main** y verifica que la fuente de **Settings → Pages** sea **Deploy from a branch → main → /docs**.

## Puesta en marcha

1. Extrae el ZIP y abre en VS Code la carpeta que contiene `platformio.ini`. Instala la extensión **PlatformIO IDE** si hace falta.
2. Conecta el ESP32 con un cable USB de datos. En PlatformIO selecciona **principal → Upload**. Este es el entorno de operación normal. **calibracion** se usa para ajustar el INA219 con un multímetro.
3. Cierra el monitor serie de VS Code antes de abrir la CLI. Abre **CMD** en la carpeta del proyecto. Si usas la terminal integrada, elige el perfil **Símbolo del sistema**.
4. Identifica el puerto COM, configura el Wi-Fi y asigna el ID de tu medidor siguiendo los comandos de abajo. La primera carga del firmware instala la CLI en el ESP32. Los cambios posteriores de Wi-Fi e ID se hacen en caliente.

## CLI por UART en Windows

### Abrir la consola interactiva

Una **CLI (Command Line Interface)** recibe comandos escritos y devuelve resultados en texto. En este proyecto, se abre en CMD, mantiene un solo puerto COM conectado y muestra el prompt `medidor>`. Cada línea que escribes ahí se interpreta como un comando del medidor.

Ejecuta estas líneas en **CMD** desde la carpeta con `platformio.ini`. Sustituye `COM5` por el puerto que aparezca al conectar la placa.

```cmd
set "PYPIO=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
"%PYPIO%" tools\medidor_cli.py puertos
"%PYPIO%" tools\medidor_cli.py --puerto COM5
```

Luego, **dentro de la consola** `medidor>`, escribe los comandos de ejemplo siguientes, uno por línea, sin copiar el prompt.

```text
ayuda
wifi
id medidor-equipo-01
ver
estado
medir
monitor
```

`wifi` solicita el SSID y luego la contraseña con entrada oculta. En `monitor`, **Ctrl+C** vuelve al prompt y conserva el COM abierto. Escribe `salir` para cerrar la sesión y liberar el puerto.

Para automatizar una consulta puedes ejecutar un comando directamente desde CMD, por ejemplo `"%PYPIO%" tools\medidor_cli.py --puerto COM5 medir`. Si usas otro Python 3.9 o posterior, instala pyserial con `python -m pip install pyserial` y usa `python` en lugar de `"%PYPIO%"`.

| Comando en `medidor>` | Función |
| --- | --- |
| `ayuda` / `comando -h` | Lista los comandos o explica los argumentos de uno. |
| `puertos` | Lista los puertos COM disponibles. |
| `ping` | Comprueba que el firmware principal responde por UART. |
| `wifi` | Guarda el SSID de 2,4 GHz y la contraseña. Vuelve a escribir el valor actual del campo que quieras conservar. Para una red abierta, pulsa Enter al pedir la contraseña. |
| `id <id>` | Cambia el identificador MQTT. Acepta de 1 a 64 caracteres entre letras, números, guiones y guiones bajos. |
| `ver` | Muestra el SSID, el ID y si hay una contraseña guardada. |
| `estado` / `medir` | Consulta protecciones y sensor, u obtiene la última lectura. |
| `monitor` / `monitor datos` / `monitor eventos` | Muestra el flujo en vivo. El primer comando incluye datos, eventos y mensajes de red. Los otros dos filtran la salida. |
| `flujo off` / `flujo on` | Pausa o reactiva los datos periódicos por UART. La lectura manual y los eventos permanecen disponibles. |
| `lim <iAlarma> <iCorte> <vMin> <vMax>` | Cambia los límites de protección. Usa amperios, voltios y punto decimal. |
| `carga on` / `carga off` | Habilita o deshabilita la carga. El estado de corte tiene prioridad. |
| `rearmar` / `silenciar` | Rearma un corte o silencia el buzzer. |
| `cero` | Ajusta el offset del sensor mediante una desconexión temporal de la carga. |
| `reset_energia` | Reinicia la energía acumulada y la corriente máxima. |
| `salir` | Cierra la consola y libera el puerto COM. |

El ESP32 guarda Wi-Fi e ID en memoria no volátil (**NVS**) y los recupera al encender. Cuando cambias las credenciales, la tarea de red vuelve a conectarse. Si cambias el ID, escribe el nuevo en el monitor web. En una instalación nueva, el medidor inicia la medición local mientras espera la configuración de red. Si conservas un `include/secrets.h` de una versión anterior, el firmware puede usarlo como configuración inicial mientras no haya una guardada por UART.

### Protocolo y terminal serie

El cable de datos comunica el COM del PC con **UART0** del ESP32 por el puente USB‑serie de la placa. Configura el terminal a **115200 baud, 8N1**, sin control de flujo y con fin de línea **LF** o **CRLF**. Puedes usar el monitor serie de PlatformIO a esa velocidad. El puerto admite un cliente a la vez.

| Comando enviado por UART | Acción en el firmware principal |
| --- | --- |
| `ayuda` | Muestra los comandos. |
| `ping` | Comprueba que responde el firmware principal. Devuelve `[CLI] OK Medidor DC`. |
| `config wifi ["Mi Red","mi-clave"]` | Guarda SSID y clave. El segundo texto puede ser `""` para una red abierta. Se recomienda introducir la clave con la CLI de CMD para ocultar su escritura. |
| `config id medidor-equipo-01` / `config ver` | Cambia el ID o consulta la configuración de red. |
| `medir` / `estado` | Obtiene una lectura puntual o consulta estado y límites. |
| `datos on` / `datos off` | Activa o pausa las lecturas periódicas por serie. |
| `lim <iAlarma> <iCorte> <vMin> <vMax>` | Cambia límites de corriente en A y de voltaje en V. Ejemplo `lim 0.60 0.75 10 14`. |
| `carga on` / `carga off` | Habilita o deshabilita la carga. El estado de corte tiene prioridad. |
| `rearmar` | Libera el corte si el sensor responde. Si persiste la condición, la protección vuelve a actuar. |
| `cero` | Abre temporalmente el circuito de carga y ajusta el offset de corriente si el sensor acepta la lectura. |
| `silenciar` / `reset_energia` | Silencia el buzzer o pone a cero los acumuladores de energía y corriente máxima. |

| Prefijo de respuesta | Qué contiene |
| --- | --- |
| `[DATOS]` / `[MEDIDA]` | Voltaje del bus y de la carga, mV del shunt, corriente cruda y calibrada, potencia, energía en Wh, máxima corriente, estado y carga. Se indica cuando la lectura es inválida. |
| `[EVENTO]` | Tipo, motivo y valores de tensión y corriente al generarse un evento, como una alarma, un corte o un rearme. |
| `[ESTADO]` | Estado de protección, motivo, límites y condición del sensor. |
| `[CLI] OK` / `[CLI] ERROR` | Confirmación del comando o causa de rechazo. |
| `[RED]` | Mensajes de Wi-Fi y MQTT, independientes de la medición local. |

## Alimentación y uso sin PC

1. Configura el ESP32 inicialmente por USB. Después desconéctalo del PC y conecta su puerto USB a un **cargador regulado de 5 V** o a una batería externa USB.
2. Conecta la fuente del circuito que quieres medir a **J1** y la carga a **J2**, según la polaridad del montaje. Los 12 V del circuito medido van a J1. Nunca apliques esa tensión a USB, 5V/VIN o 3V3 del ESP32.
3. Confirma desde el teléfono que el monitor web muestra el mismo equipo en línea. El ESP32 se conecta al Wi-Fi guardado, y el navegador puede estar en otra red con internet. Si falla Wi-Fi o el broker, el ESP32 sigue leyendo el INA219 y aplicando la protección local.

## Mediciones y protecciones

- **Voltaje de bus** (`V_bus`) es el voltaje en **VIN+** del INA219 respecto a GND. El firmware obtiene ese valor sumando la caída del shunt al voltaje en **VIN−** (`V_carga`).
- **Corriente** se calcula con el voltaje del shunt y la calibración. **Potencia de carga** se calcula con `V_carga × I_cal`. La **energía** se acumula en Wh durante la operación. El firmware también registra la corriente máxima.
- El firmware principal programa lecturas cada **100 ms** e imprime por UART y publica por MQTT aproximadamente cada **1 s** en funcionamiento normal.
- El rango de corriente configurado es **800 mA nominales** con el shunt de **0,1 Ω**. Los límites iniciales son **0,60 A** de alarma, **0,75 A** de corte y **10–14 V** como ventana de protección. Los límites de voltaje se evalúan con `V_carga`. Los límites guardados se recuperan al arrancar.

| Estado | Condición | Comportamiento |
| --- | --- | --- |
| **NORMAL** | Lectura válida y sin condiciones de alarma o corte | La carga sigue habilitada si así se configuró. |
| **ALARMA** | Corriente igual o superior al umbral de alarma, o voltaje por debajo del mínimo y por encima de 1 V | El LED parpadea y el buzzer avisa. La alarma vuelve a normal cuando la magnitud se recupera con histéresis. |
| **CORTE** | Corriente igual o superior al umbral de corte, voltaje igual o superior al máximo, saturación o lectura inválida | GPIO27 pasa a bajo, se desconecta la carga y el corte queda pendiente de rearme. |

Con el firmware principal, una pulsación corta de **SW1** rearma un corte o silencia el buzzer. Una pulsación de **3 segundos** solicita el ajuste de cero.

## Pines del ESP32 DevKit V1

| Señal | GPIO | Conexión en el montaje |
| --- | --- | --- |
| SDA | 21 | SDA del INA219 (alimentación lógica a 3,3 V) |
| SCL | 22 | SCL del INA219 (I²C a 100 kHz, dirección `0x40`) |
| Corte de carga | 27 | Control de Q2 IRLZ44N por R4 de 100 Ω. Alto habilita la carga si la protección lo permite. |
| Buzzer | 26 | Control de Q3 y BZ1 por R8 de 1 kΩ |
| LED de alarma | 25 | Control de Q4 y D5 por R10 de 1 kΩ |
| Botón SW1 | 33 | Pulsador a GND, con pull‑up interno |
| TX0 / RX0 | 1 / 3 | UART0 usada por el puente USB‑serie integrado |

## Calibración del INA219 por UART

1. Carga **calibracion** desde PlatformIO y abre el monitor serie a **115200 baud**. Escribe `h` para ver la ayuda.
2. Con `z` toma el offset a corriente cero. Usa `k` para habilitar la carga y toma varios puntos `i <A>` con el multímetro en serie. Para ajustar voltaje, toma puntos `v <V>` con el multímetro entre **VIN− / J2+** y GND.
3. Usa `c` para calcular y revisar los errores, y `g` para guardar la calibración en memoria. `m` toma una lectura promediada, `s` alterna lectura continua, `p` muestra la calibración activa, `b` borra los puntos tomados y `r` restaura valores nominales en RAM. En este modo, SW1 solicita una lectura rápida.
4. Carga de nuevo **principal**. El medidor usa la calibración guardada al arrancar. La CLI de configuración Wi‑Fi está en el entorno **principal**.

## MQTT y archivos del proyecto

El ESP32 se conecta a `broker.hivemq.com:1883`. El navegador usa `wss://broker.hivemq.com:8884/mqtt`. La base es `medidordc/<DEVICE_ID>`, con los tópicos `/datos`, `/estado`, `/evento` y `/config` del equipo, y `/cmd` para los controles enviados por la web. El ID forma parte del tópico en un broker público y es un identificador de equipo.

| Ruta | Función |
| --- | --- |
| [platformio.ini](platformio.ini) | Entornos `principal` y `calibracion`, placa y dependencias de PlatformIO. |
| [include/hardware_config.h](include/hardware_config.h) | Pines, parámetros de I²C, shunt y rango del sensor. |
| [src/principal/](src/principal/) | Medición, protecciones, parser UART, configuración NVS y tarea Wi-Fi/MQTT. |
| [src/calibracion/](src/calibracion/) | Firmware de calibración por serie. |
| [lib/INA219Simple/](lib/INA219Simple/) y [lib/CalibradorINA219/](lib/CalibradorINA219/) | Lectura del sensor, calibración, botón, LED y buzzer. |
| [tools/medidor_cli.py](tools/medidor_cli.py) | Consola interactiva y cliente de comandos individuales por UART. |
| [docs/index.html](docs/index.html) y [web/index.html](web/index.html) | Monitor para GitHub Pages y copia para abrir localmente. |

## Diagnóstico rápido

| Síntoma | Comprobación |
| --- | --- |
| El COM no aparece | Prueba un cable USB de datos y compara la lista de puertos con la placa desconectada y conectada. |
| Puerto ocupado | Cierra el monitor de PlatformIO o sal de `monitor` con **Ctrl+C**. |
| El COM existe pero la CLI no responde | Confirma `COMx` y que cargaste **principal**. Con un terminal serie a 115200 baud, envía `ping`. |
| Hay lecturas por UART pero la web muestra el equipo fuera de línea | Comprueba el SSID de 2,4 GHz, el acceso a internet e igualdad exacta del ID. Busca `[RED] MQTT conectado` en `monitor`. |
| El monitor web enlaza pero no hay lecturas válidas | En la CLI revisa `estado` y `medir`. Si el sensor falla, comprueba SDA, SCL, 3,3 V y GND. |
