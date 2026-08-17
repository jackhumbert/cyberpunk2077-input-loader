@echo off
REM ============================================================================
REM  Input Loader - install directly to game directory
REM ============================================================================
REM  This is a convenience script for developers who want to install straight
REM  into their Cyberpunk 2077 install without producing the zip files first.
REM
REM  Usage:
REM    install.bat                   installs to default Steam path
REM    install.bat "D:\games\Cyberpunk 2077"   installs to a custom path
REM
REM  Prereqs: run build.bat first.
REM ============================================================================

setlocal

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "ROOT=%ROOT:~0,-6%"
set "ROOT=%ROOT%"

set "GAME_DIR=%~1"
if "%GAME_DIR%"=="" (
  set "GAME_DIR=C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077"
)

if not exist "%ROOT%\game_dir\red4ext\plugins\input_loader\input_loader.dll" (
  echo [ERROR] Build output not found.
  echo         Run build.bat first from the repo root.
  exit /b 1
)

if not exist "%GAME_DIR%\bin\x64\Cyberpunk2077.exe" (
  echo [ERROR] Cyberpunk 2077 install not found at:
  echo         %GAME_DIR%
  echo.
  echo         Pass the game directory as the first argument:
  echo           install.bat "D:\games\Cyberpunk 2077"
  exit /b 1
)

echo [INFO] Installing Input Loader into:
echo        %GAME_DIR%
echo.

xcopy /s /d /y /i "%ROOT%\game_dir\*" "%GAME_DIR%\" >nul
if errorlevel 1 (
  echo [ERROR] xcopy failed.
  exit /b 1
)

echo.
echo [OK] Install complete. Launch Cyberpunk 2077 to verify.
echo     Check %GAME_DIR%\red4ext\logs\input_loader.log after launch.

endlocal
