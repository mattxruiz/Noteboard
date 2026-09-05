@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
  echo [BREAK] cmake not found
  echo Install CMake, then re-run build.bat
  exit /b 1
)

if not exist build mkdir build
cmake -S . -B build -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo [OK] build\Release\ProjectBoard.exe
echo Data folder: next to exe as data\
exit /b 0
