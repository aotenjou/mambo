@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM Build script for BluetoothConsole with WinRT BLE support.
REM Auto-detects Visual Studio and Windows SDK 10.
REM ============================================================

echo ========================================
echo   Building BluetoothConsole (BLE)
echo ========================================

set "VSWHERE=!ProgramFiles(x86)!\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
    set "VSWHERE=!ProgramFiles!\Microsoft Visual Studio\Installer\vswhere.exe"
)

if exist "!VSWHERE!" (
    for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL=%%I"
    )
)

if not defined VS_INSTALL (
    if exist "!ProgramFiles!\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VS_INSTALL=!ProgramFiles!\Microsoft Visual Studio\18\Community"
    )
)

if not defined VS_INSTALL (
    echo [ERROR] Visual Studio C++ tools were not found.
    exit /b 1
)

set "VCVARS=!VS_INSTALL!\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "!VCVARS!" (
    echo [ERROR] vcvarsall.bat not found: !VCVARS!
    exit /b 1
)

set "SDK_ROOT=!ProgramFiles(x86)!\Windows Kits\10"
if not exist "!SDK_ROOT!\Include" (
    set "SDK_ROOT=!ProgramFiles!\Windows Kits\10"
)
if not exist "!SDK_ROOT!\Include" (
    echo [ERROR] Windows SDK 10 was not found.
    exit /b 1
)

set "SDK_VERSION="
for /f "delims=" %%I in ('dir /b /ad "!SDK_ROOT!\Include\10.*" 2^>nul ^| sort /r') do (
    if not defined SDK_VERSION set "SDK_VERSION=%%I"
)

if not defined SDK_VERSION (
    echo [ERROR] Windows SDK include version was not found under !SDK_ROOT!\Include.
    exit /b 1
)

set "CPPWINRT_INCLUDE=!SDK_ROOT!\Include\!SDK_VERSION!\cppwinrt"
if not exist "!CPPWINRT_INCLUDE!" (
    echo [ERROR] C++/WinRT headers were not found: !CPPWINRT_INCLUDE!
    exit /b 1
)

echo [INFO] Visual Studio: !VS_INSTALL!
echo [INFO] Windows SDK: !SDK_ROOT!\Include\!SDK_VERSION!

call "!VCVARS!" x64
if errorlevel 1 (
    echo [ERROR] Failed to initialize MSVC environment.
    exit /b 1
)

cl BluetoothConsole.cpp /EHsc /std:c++20 /await:strict /W3 ^
    /DWINRT_LEAN_AND_MEAN ^
    /I"!CPPWINRT_INCLUDE!" ^
    /link ws2_32.lib bthprops.lib ole32.lib oleaut32.lib runtimeobject.lib /out:BluetoothConsole.exe

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    exit /b 1
)

echo.
echo [OK] Build successful: BluetoothConsole.exe
exit /b 0
