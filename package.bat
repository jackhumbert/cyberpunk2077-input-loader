@echo off
REM ============================================================================
REM  Input Loader - packaging script
REM ============================================================================
REM  Produces two zips in dist\:
REM
REM    input_loader_vX.X.X.zip
REM      engine\config\platform\pc\input_loader.ini
REM      r6\cache\inputContexts.xml
REM      r6\cache\inputUserMappings.xml
REM      red4ext\plugins\input_loader\
REM          inputUserMappings.xml
REM          input_loader.dll
REM          license.md
REM          readme.md
REM
REM    input_loader_vX.X.X_pdb.zip
REM      red4ext\plugins\input_loader\
REM          input_loader.pdb
REM
REM  Prereqs: run build.bat first.
REM ============================================================================

setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

REM --- Sanity checks --------------------------------------------------------
if not exist "game_dir\red4ext\plugins\input_loader\input_loader.dll" (
  echo [ERROR] game_dir\red4ext\plugins\input_loader\input_loader.dll not found.
  echo         Run build.bat first.
  exit /b 1
)

if not exist "game_dir_debug\red4ext\plugins\input_loader\input_loader.pdb" (
  echo [ERROR] game_dir_debug\red4ext\plugins\input_loader\input_loader.pdb not found.
  echo         Run build.bat first (it produces both game_dir\ and game_dir_debug\).
  exit /b 1
)

REM --- Read version from CMakeLists.txt -------------------------------------
REM  Parse project(input_loader VERSION X.Y.Z ...) line
for /f "tokens=3 delims= " %%V in ('findstr /b "project" "%ROOT%\CMakeLists.txt"') do (
  set "VERSION_RAW=%%V"
  goto :got_version
)
:got_version
REM Strip trailing ")"
set "VERSION=%VERSION_RAW:~0,-1%"

echo [INFO] Detected version: %VERSION%

REM --- Prepare dist/ ---------------------------------------------------------
if not exist "dist" mkdir "dist"

REM --- Clean previous zips --------------------------------------------------
set "MAIN_ZIP=dist\input_loader_v%VERSION%.zip"
set "PDB_ZIP=dist\input_loader_v%VERSION%_pdb.zip"

if exist "%MAIN_ZIP%" del "%MAIN_ZIP%"
if exist "%PDB_ZIP%" del "%PDB_ZIP%"

REM --- Build main zip from game_dir ------------------------------------------
REM  We cd into game_dir so paths in the zip are relative
REM  (no leading "game_dir\" prefix in the archive).
echo [INFO] Creating %MAIN_ZIP% ...
pushd "game_dir"
REM  Use PowerShell's Compress-Archive for reliable UTF-8 paths.
powershell -NoProfile -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "Compress-Archive -Path engine,r6,red4ext -DestinationPath '%ROOT%\%MAIN_ZIP%' -Force"
popd

if not exist "%MAIN_ZIP%" (
  echo [ERROR] Failed to create %MAIN_ZIP%.
  exit /b 1
)

echo [OK]  %MAIN_ZIP%

REM --- Build PDB-only zip from game_dir_debug -------------------------------
echo [INFO] Creating %PDB_ZIP% ...
pushd "game_dir_debug"
powershell -NoProfile -Command ^
  "$ErrorActionPreference = 'Stop';" ^
  "Compress-Archive -Path red4ext -DestinationPath '%ROOT%\%PDB_ZIP%' -Force"
popd

if not exist "%PDB_ZIP%" (
  echo [ERROR] Failed to create %PDB_ZIP%.
  exit /b 1
)

echo [OK]  %PDB_ZIP%

REM --- Final report ----------------------------------------------------------
echo.
echo ============================================================
echo  PACKAGING SUCCEEDED
echo ============================================================
echo  Main release:  %MAIN_ZIP%
echo  PDB symbols:    %PDB_ZIP%
echo ============================================================

endlocal
