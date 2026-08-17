@echo off
REM ============================================================================
REM  Input Loader - packaging script
REM ============================================================================

setlocal

REM --- Root directory --------------------------------------------------------
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

REM --- Sanity checks ----------------------------------------------------------
if not exist "%ROOT%\game_dir\red4ext\plugins\input_loader\input_loader.dll" (
    echo [ERROR] game_dir\red4ext\plugins\input_loader\input_loader.dll not found.
    echo         Run build.bat first.
    exit /b 1
)

if not exist "%ROOT%\game_dir_debug\red4ext\plugins\input_loader\input_loader.pdb" (
    echo [ERROR] game_dir_debug\red4ext\plugins\input_loader\input_loader.pdb not found.
    echo         Run build.bat first.
    exit /b 1
)

REM --- Read version from CMakeLists.txt -------------------------------------
for /f "tokens=3" %%V in ('findstr /b "project(input_loader" "%ROOT%\CMakeLists.txt"') do (
    set "VERSION=%%V"
    goto got_version
)

echo [ERROR] Could not find project version in CMakeLists.txt.
exit /b 1

:got_version
echo [INFO] Detected version: %VERSION%

echo [INFO] Detected version: %VERSION%

REM --- Prepare dist ----------------------------------------------------------
if not exist "%ROOT%\dist" mkdir "%ROOT%\dist"

set "MAIN_ZIP=%ROOT%\dist\input_loader_v%VERSION%.zip"
set "PDB_ZIP=%ROOT%\dist\input_loader_v%VERSION%_pdb.zip"

REM --- Clean previous zips ---------------------------------------------------
if exist "%MAIN_ZIP%" del /q "%MAIN_ZIP%"
if exist "%PDB_ZIP%" del /q "%PDB_ZIP%"

REM --- Build main zip --------------------------------------------------------
echo [INFO] Creating %MAIN_ZIP% ...

pushd "%ROOT%\game_dir"

powershell -NoProfile -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "Compress-Archive -Path engine,r6,red4ext -DestinationPath '%MAIN_ZIP%' -Force"

if errorlevel 1 (
    popd
    echo [ERROR] Failed to create main release.
    exit /b 1
)

popd

if not exist "%MAIN_ZIP%" (
    echo [ERROR] Failed to create %MAIN_ZIP%.
    exit /b 1
)

echo [OK] %MAIN_ZIP%

REM --- Build PDB zip ---------------------------------------------------------
echo [INFO] Creating %PDB_ZIP% ...

pushd "%ROOT%\game_dir_debug"

powershell -NoProfile -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "Compress-Archive -Path red4ext -DestinationPath '%PDB_ZIP%' -Force"

if errorlevel 1 (
    popd
    echo [ERROR] Failed to create PDB release.
    exit /b 1
)

popd

if not exist "%PDB_ZIP%" (
    echo [ERROR] Failed to create %PDB_ZIP%.
    exit /b 1
)

echo [OK] %PDB_ZIP%

REM --- Final report ----------------------------------------------------------
echo.
echo ============================================================
echo  PACKAGING SUCCEEDED
echo ============================================================
echo  Main release: %MAIN_ZIP%
echo  PDB symbols:  %PDB_ZIP%
echo ============================================================

endlocal