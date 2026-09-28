@echo off
REM ============================================================
REM  build-windows.cmd — compilar inerxia de forma NATIVA en cmd
REM  (Sin WSL). Requisitos previos:
REM     1) Git for Windows
REM     2) CMake >= 3.20  (choco install cmake --installargs 'ADD_CMAKE_TO_PATH=User')
REM     3) Visual Studio 2022 Build Tools con workload
REM        "Desarrollo para escritorio con C++" (MSVC + Windows SDK)
REM  Ejecutar en cmd:
REM       scripts\build-windows.cmd
REM ============================================================
@setlocal
set ROOT=%~dp0..
cd /d "%ROOT%"

echo [1/4] vcpkg (dependencias nativas: openssl, libpq, libcurl)...
if "%VCPKG_ROOT%"=="" (
  if not exist "%ROOT%\vcpkg" (
    if not exist vcpkg git clone --depth 1 https://github.com/microsoft/vcpkg vcpkg
    call vcpkg\bootstrap-vcpkg.bat -disableMetrics || exit /b 1
  )
  set "VCPKG_ROOT=%ROOT%\vcpkg"
)

call "%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows                           || exit /b 1
if not exist "%VCPKG_ROOT%\installed\x64-windows" echo [WARN] vcpkg triplet? revisa

echo [2/4] configurar CMake (Visual Studio 17 2022, x64)...
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DCMAKE_BUILD_TYPE=Debug                                                             || exit /b 1

echo [3/4] compilar...
cmake --build build-vs --config Debug -j                                              || exit /b 1

echo.
echo [4/4] LISTO. Binario: build-vs\src\api\Debug\inerxia_server.exe
echo        Siguiente paso: scripts\run-inerxia.cmd start
@endlocal
exit /b 0