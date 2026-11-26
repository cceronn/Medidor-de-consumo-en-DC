@echo off
setlocal
title Medidor DC - Consola UART
pushd "%~dp0" || exit /b 1

set "MEDIDOR_PY=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
if exist "%MEDIDOR_PY%" (
    "%MEDIDOR_PY%" -c "import sys, serial, serial.tools.list_ports; assert sys.version_info >= (3, 9); assert hasattr(serial,'Serial')" >nul 2>&1
    if not errorlevel 1 goto platformio
)

py -3 -c "import sys, serial, serial.tools.list_ports; assert sys.version_info >= (3, 9); assert hasattr(serial,'Serial')" >nul 2>&1
if not errorlevel 1 goto pylauncher
python -c "import sys, serial, serial.tools.list_ports; assert sys.version_info >= (3, 9); assert hasattr(serial,'Serial')" >nul 2>&1
if not errorlevel 1 goto pythonpath

echo No se encontro Python con pyserial.
echo Instala PlatformIO IDE o ejecuta: py -3 -m pip install pyserial
set "MEDIDOR_EXIT=1"
goto fin

:platformio
"%MEDIDOR_PY%" "tools\medidor_cli.py" %*
goto resultado

:pylauncher
py -3 "tools\medidor_cli.py" %*
goto resultado

:pythonpath
python "tools\medidor_cli.py" %*

:resultado
set "MEDIDOR_EXIT=%ERRORLEVEL%"

:fin
popd
if not "%MEDIDOR_EXIT%"=="0" pause
exit /b %MEDIDOR_EXIT%
