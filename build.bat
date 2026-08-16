@echo off
REM ============================================================================
REM  Input Loader - one-shot build script
REM ============================================================================
REM  Usage:  build.bat [config]
REM    config = Release | Debug | RelWithDebInfo  (default: RelWithDebInfo)
REM
REM  What this script does:
REM    1. Verifies you're running from a VS developer prompt (cl.exe on PATH).
REM    2. Initializes git submodules if needed.
REM    3. Configures CMake (Ninja generator, MSVC compiler).
REM    4. Builds the input_loader.dll.
REM    5. Installs the ready-to-use game_dir/ tree.
REM
REM  After it succeeds, run package.bat to produce the distribution zips.
REM ============================================================================

setlocal enabledelayedexpansion

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=RelWithDebInfo"

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

REM --- Step 1: Verify MSVC is on PATH ---------------------------------------
where cl.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] cl.exe not found on PATH.
  echo.
  echo Open the "x64 Native Tools Command Prompt for VS 2022" from your Start
  echo Menu, cd into the repo, and run this script again.
  echo.
  echo Alternatively, run vcvars64.bat first:
  echo   "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  exit /b 1
)

where ninja.exe >nul 2>nul
if errorlevel 1 (
  echo [ERROR] ninja.exe not found on PATH.
  echo.
  echo Install Ninja from https://github.com/ninja-build/ninja/releases
  echo and put ninja.exe somewhere on your PATH.
  exit /b 1
)

echo [OK] cl.exe and ninja.exe are available.

REM --- Step 2: Initialize submodules ----------------------------------------
REM  Check each required submodule independently - one might be initialized
REM  while others aren't (e.g., if git submodule update was interrupted).
set "NEEDS_INIT=0"
if not exist "deps\spdlog\CMakeLists.txt"            set "NEEDS_INIT=1"
if not exist "deps\pugixml\CMakeLists.txt"           set "NEEDS_INIT=1"
if not exist "deps\cyberpunk_cmake\CMakeLists.txt"    set "NEEDS_INIT=1"
if not exist "deps\red4ext.sdk\include\RED4ext\RED4ext.hpp" set "NEEDS_INIT=1"

if "%NEEDS_INIT%"=="1" (
  echo [INFO] Initializing git submodules...
  git submodule update --init --recursive
  if errorlevel 1 (
    echo [ERROR] git submodule update failed.
    echo.
    echo If the error mentions permission denied or SSH:
    echo   - The .gitmodules file has been switched to HTTPS URLs, but if you
    echo     checked out the repo before that change, you may need to run:
    echo       git submodule sync
    echo     to refresh the local submodule URL configuration.
    exit /b 1
  )
) else (
  echo [OK] All submodules present.
)

REM --- Step 3: Configure CMake -----------------------------------------------
echo [INFO] Configuring CMake (Ninja + MSVC, %CONFIG%)...

if exist "build" (
  echo [INFO] Removing previous build directory...
  rmdir /s /q "build"
)

REM  CMAKE_POLICY_VERSION_MINIMUM=3.5 is needed because some submodules
REM  (pugixml, spdlog) declare cmake_minimum_required(VERSION 3.4) or older,
REM  which CMake 4.x rejects by default. Setting it as a -D flag propagates
REM  into all add_subdirectory() scopes.
cmake -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=%CONFIG% ^
  -DCMAKE_CI_BUILD=ON ^
  -DCMAKE_C_COMPILER=cl ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5

if errorlevel 1 (
  echo [ERROR] CMake configure failed.
  exit /b 1
)

REM --- Step 4: Build ---------------------------------------------------------
echo [INFO] Building...
cmake --build build --config %CONFIG%
if errorlevel 1 (
  echo [ERROR] Build failed.
  exit /b 1
)

REM --- Step 5: Install (produces game_dir\ and game_dir_debug\) ---------------
echo [INFO] Installing to game_dir\...
cmake --install build --config %CONFIG%
if errorlevel 1 (
  echo [ERROR] Install failed.
  exit /b 1
)

echo.
echo ============================================================
echo  BUILD SUCCEEDED
echo ============================================================
echo  Build output:    build\input_loader.dll
echo  Install layout:  game_dir\
echo  Debug symbols:   game_dir_debug\
echo.
echo  Next: run package.bat to produce the distribution zips.
echo ============================================================

endlocal
