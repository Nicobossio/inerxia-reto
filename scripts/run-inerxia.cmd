@echo off
REM ============================================================
REM  run-inerxia.cmd — arrancar/apagar el servidor desde cmd (nativo Windows).
REM  Requiere: inerxia_server.exe compilado (build-windows.cmd) y PostgreSQL
REM  local en el puerto 5432 con role/db 'inerxia' (ver docs/INSTALL.md -4-A).
REM
REM  Uso:
REM      scripts\run-inerxia.cmd start    arranca el servidor + asegura usuario demo
REM      scripts\run-inerxia.cmd status   /api/health
REM      scripts\run-inerxia.cmd hosts    comprueba inerxia.local en el fichero hosts
REM      scripts\run-inerxia.cmd stop     cierra el servidor
REM ============================================================
@setlocal
set ROOT=%~dp0..
cd /d "%ROOT%"

REM ---- configuración (sobrescribible antes de llamar) ----
if "%PGUSER%"==""                set PGUSER=inerxia
if "%PGPASSWORD%"==""            set PGPASSWORD=inerxia_secret
if "%PGDATABASE%"==""            set PGDATABASE=inerxia
if "%MIKROTIK_BASE_URL%"==""     set MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest
if "%MIKROTIK_USER%"==""         set MIKROTIK_USER=lab-admin
if "%MIKROTIK_PASSWORD%"==""     set MIKROTIK_PASSWORD=lab-router-password-2026
if "%CANONICAL_DOMAIN%"==""      set CANONICAL_DOMAIN=inerxia.local:8484
set API_URL=http://127.0.0.1:8484

set SERVER=build-vs\src\api\Debug\inerxia_server.exe
if not exist "%SERVER%" set SERVER=build\src\api\Debug\inerxia_server.exe
if not exist "%SERVER%" (
  echo ERROR: no encuentro inerxia_server.exe. Primero ejecuta scripts\build-windows.cmd
  exit /b 1
)

if "%~1"=="" goto usage
if /i "%~1"=="start"  goto start
if /i "%~1"=="status" goto status
if /i "%~1"=="hosts"  goto hosts
if /i "%~1"=="stop"   goto stop
:usage
echo uso: run-inerxia.cmd [start^|status^|hosts^|stop]
exit /b 2

:start
echo [api] arrancando inerxia_server.exe...
start "inerxia_server" cmd /k "%SERVER%"
echo [api] esperando a /api/health...
for /L %%i in (1,1,30) do (
  curl -s -o nul "%API_URL%/api/health" && goto healthy
  timeout /t 1 /nobreak >nul
)
echo [api] ATENCION: /api/health no responde (revisa PostgreSQL y el log de la ventana).
exit /b 1

:healthy
curl -s -o nul -X POST "%API_URL%/api/auth/register" -H "Content-Type: application/json" -d "{\"username\":\"demo\",\"password\":\"demo2026\"}"
echo [api] LISTO.
echo   Dashboard : http://127.0.0.1:8484/ui
echo   Usuario   : demo
echo   Contrasena: demo2026
echo   Log       : ventana "inerxia_server"
exit /b 0

:status
curl -s "%API_URL%/api/health"
echo.
if errorlevel 1 echo [api] servidor no responde.
exit /b 0

:hosts
findstr /C:"inerxia.local" "%WINDIR%\System32\drivers\etc\hosts" >nul 2>&1
if errorlevel 1 (
  echo inerxia.local NO esta en hosts. Como administrador, anade:
  echo.
  echo   127.0.0.1 inerxia.local  ^(en %WINDIR%\System32\drivers\etc\hosts^)
) else (
  echo inerxia.local ya esta en hosts. Abre http://inerxia.local:8484/ui
)
exit /b 0

:stop
taskkill /im inerxia_server.exe /f >nul 2>&1
echo [api] servidor detenido.
exit /b 0