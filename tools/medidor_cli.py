"""Consola del Medidor DC por UART."""

import argparse
from contextlib import ExitStack
import getpass
import json
import math
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("Instala pyserial o usa el Python de PlatformIO (ver README.md).")


class ParserConsola(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError(message)


def recibir(puerto):
    return puerto.readline().decode("utf-8", errors="replace").rstrip("\r\n")


def intercambiar(puerto, comando, prefijo, espera=3, mostrar_eventos=False):
    # Descarta lecturas antiguas antes de pedir una respuesta nueva. El monitor
    # lee continuamente y no usa esta función.
    puerto.reset_input_buffer()
    puerto.write(("\n" + comando + "\n").encode("utf-8"))
    limite = time.monotonic() + espera
    while time.monotonic() < limite:
        linea = recibir(puerto)
        if linea.startswith("[CLI] ERROR"):
            raise ValueError(linea.removeprefix("[CLI] ERROR ").strip())
        if linea.startswith(prefijo):
            return linea
        if mostrar_eventos and linea.startswith("[EVENTO]"):
            print(linea)
    raise TimeoutError("El ESP32 no respondió. Revisa el COM y el firmware principal.")


def sincronizar(puerto):
    # Algunos puentes USB-UART reinician la placa al abrir el COM.
    for _ in range(3):
        try:
            intercambiar(puerto, "ping", "[CLI] OK Medidor DC", espera=1)
            return
        except TimeoutError:
            continue
    raise TimeoutError("No se encontró el firmware principal en este puerto.")


def numero(valor):
    try:
        x = float(valor)
    except ValueError as error:
        raise argparse.ArgumentTypeError("usa un número con punto decimal") from error
    if not math.isfinite(x):
        raise argparse.ArgumentTypeError("usa un número finito")
    return x


def agregar_comandos(parser, interactiva):
    comandos = parser.add_subparsers(dest="accion", required=interactiva)
    if interactiva:
        comandos.add_parser("ayuda", help="Ver comandos disponibles")
        comandos.add_parser("salir", help="Cerrar la conexión")
    else:
        comandos.add_parser("consola", help="Abrir la consola interactiva (predeterminado)")
    comandos.add_parser("puertos", help="Listar puertos COM")
    comandos.add_parser("wifi", help="Guardar SSID y clave Wi-Fi con entrada oculta")
    comandos.add_parser("id", help="Guardar el DEVICE_ID de MQTT").add_argument("valor")
    comandos.add_parser("ver", help="Mostrar SSID e ID")
    comandos.add_parser("estado", help="Consultar estado y límites de protección")
    comandos.add_parser("medir", help="Consultar la última lectura")
    comandos.add_parser("ping", help="Verificar la conexión UART")
    comandos.add_parser("flujo", help="Activar o pausar los datos periódicos").add_argument(
        "modo", choices=("on", "off")
    )
    comandos.add_parser("monitor", help="Recibir el flujo en vivo hasta Ctrl+C").add_argument(
        "filtro", nargs="?", choices=("todo", "datos", "eventos"), default="todo"
    )
    limites = comandos.add_parser("lim", help="Cambiar límites (A, A, V, V)")
    for nombre in ("iAlarma", "iCorte", "vMin", "vMax"):
        limites.add_argument(nombre, type=numero)
    comandos.add_parser("carga", help="Habilitar o deshabilitar la carga").add_argument(
        "modo", choices=("on", "off")
    )
    comandos.add_parser("rearmar", help="Liberar un corte si el sensor responde")
    comandos.add_parser("silenciar", help="Silenciar el buzzer")
    comandos.add_parser("cero", help="Ajustar el cero del sensor")
    comandos.add_parser("reset_energia", help="Reiniciar la energía y la corriente máxima")


def argumentos():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--puerto", help="Puerto serie manual; por defecto se detecta el medidor")
    agregar_comandos(parser, interactiva=False)
    args = parser.parse_args()
    if args.accion is None:
        args.accion = "consola"
    return args


def listar_puertos():
    for puerto in sorted(list_ports.comports()):
        print(f"{puerto.device}: {puerto.description}")


def abrir_puerto(conexiones, nombre):
    puerto = conexiones.enter_context(serial.Serial(baudrate=115200, timeout=0.2, write_timeout=1))
    puerto.dtr = False
    puerto.rts = False
    puerto.port = nombre
    puerto.open()
    return puerto


def elegir_medidor(medidores):
    if len(medidores) == 1:
        return medidores[0]
    if not sys.stdin.isatty():
        raise ValueError("Hay varios medidores conectados. Especifica --puerto COMx.")

    print("Se detectaron varios medidores:")
    for indice, puerto in enumerate(medidores, start=1):
        print(f"  {indice}. {puerto.port}")
    while True:
        opcion = input("Elige un medidor: ").strip()
        try:
            indice = int(opcion)
        except ValueError:
            continue
        if 1 <= indice <= len(medidores):
            return medidores[indice - 1]


def detectar_medidor(conexiones):
    # Identificadores de los puentes USB-UART más comunes en placas ESP32.
    fabricantes = {0x10C4, 0x1A86, 0x0403, 0x067B, 0x303A}
    candidatos = [p for p in sorted(list_ports.comports()) if p.vid in fabricantes]
    if not candidatos:
        raise ValueError("No encontré un puente USB-serial de ESP32. Revisa el cable de datos y el driver. "
                         "Para otro adaptador usa --puerto COMx.")
    print("Buscando el Medidor DC...", flush=True)
    medidores = []
    fallos = []
    for info in candidatos:
        with ExitStack() as prueba:
            try:
                puerto = abrir_puerto(prueba, info.device)
                sincronizar(puerto)
            except (OSError, ValueError) as error:
                fallos.append(f"{info.device}: {error}")
            else:
                conexiones.enter_context(prueba.pop_all())
                medidores.append(puerto)

    if not medidores:
        detalle = "\\n".join(fallos)
        raise ValueError(f"Ningún puerto respondió como Medidor DC. Cierra el monitor serie "
                         f"y comprueba que cargaste el firmware principal.\\n{detalle}")
    elegido = elegir_medidor(medidores)
    for puerto in medidores:
        if puerto is not elegido:
            puerto.close()
    return elegido


def monitor(puerto, filtro):
    prefijo = {"todo": "", "datos": "[DATOS]", "eventos": "[EVENTO]"}[filtro]
    print(f"Monitor: {filtro}. Pulsa Ctrl+C para detenerlo.")
    while True:
        linea = recibir(puerto)
        if linea and linea.startswith(prefijo):
            print(linea, flush=True)


def ejecutar(puerto, args):
    accion = args.accion
    if accion == "puertos":
        listar_puertos()
        return
    if accion == "monitor":
        monitor(puerto, args.filtro)
        return
    if accion == "wifi":
        if not sys.stdin.isatty():
            raise ValueError("Ejecuta wifi desde una consola interactiva.")
        ssid = input("SSID de la red Wi-Fi: ")
        clave = getpass.getpass("Clave Wi-Fi (Enter si la red es abierta): ")
        comando = "config wifi " + json.dumps([ssid, clave], ensure_ascii=False)
        prefijo = "[CLI] OK guardado en NVS"
    elif accion == "id":
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", args.valor):
            raise ValueError("ID de 1 a 64 caracteres: letras, números, guion o guion bajo.")
        comando = "config id " + args.valor
        prefijo = "[CLI] OK guardado en NVS"
    elif accion == "flujo":
        comando = "datos " + args.modo
        prefijo = "[CLI] OK flujo actualizado"
    elif accion == "lim":
        comando = f"lim {args.iAlarma} {args.iCorte} {args.vMin} {args.vMax}"
        prefijo = "[CLI] OK limites"
    elif accion == "carga":
        comando = "carga " + args.modo
        prefijo = "[CLI] OK carga"
    else:
        comando = "config ver" if accion == "ver" else accion
        prefijo = {
            "ver": "[CLI] OK SSID=",
            "estado": "[ESTADO]",
            "medir": "[MEDIDA]",
            "ping": "[CLI] OK Medidor DC",
            "rearmar": "[CLI] OK rearme",
            "silenciar": "[CLI] OK silenciado",
            "cero": "[CLI] OK cero",
            "reset_energia": "[CLI] OK energia",
        }[accion]
    print(intercambiar(puerto, comando, prefijo, espera=10 if accion == "cero" else 3,
                       mostrar_eventos=True))


def consola(puerto):
    parser = ParserConsola(prog="medidor", description="Comandos de la consola UART")
    agregar_comandos(parser, interactiva=True)
    print(f"Conectado a {puerto.port} a 115200 baud. Escribe ayuda o salir.")
    while True:
        try:
            palabras = input("medidor> ").split()
            if not palabras:
                continue
            args = parser.parse_args(palabras)
            if args.accion == "salir":
                return
            if args.accion == "ayuda":
                parser.print_help()
            else:
                ejecutar(puerto, args)
        except EOFError:
            return
        except KeyboardInterrupt:
            print()
        except (TimeoutError, ValueError) as error:
            print(f"Error: {error}", file=sys.stderr)
        except SystemExit:
            # argparse imprime la ayuda de "comando -h"; la sesión sigue abierta.
            continue


def main():
    args = argumentos()
    if args.accion == "puertos":
        listar_puertos()
        return 0

    try:
        with ExitStack() as conexiones:
            if args.puerto:
                puerto = abrir_puerto(conexiones, args.puerto)
                if args.accion != "monitor":
                    sincronizar(puerto)
            else:
                puerto = detectar_medidor(conexiones)
            if args.accion == "consola":
                consola(puerto)
            else:
                ejecutar(puerto, args)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, EOFError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
