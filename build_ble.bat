@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM Build script for BluetoothConsole with WinRT BLE support
REM Requires MSVC (Visual Studio) and Windows SDK 10.0.26100.0
REM ============================================================

set "MSVC_PATH=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK_INCLUDE=D:\Windows Kits\10\Include\10.0.26100.0"
set "SDK_LIB=D:\Windows Kits\10\Lib\10.0.26100.0"
set "CPPWINRT_INCLUDE=%SDK_INCLUDE%\cppwinrt"

echo ========================================
echo   Building BluetoothConsole (BLE Enhanced)
echo ========================================
echo.

echo [1/3] Setting up MSVC environment...
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo [ERROR] Failed to set up MSVC environment.
    pause
    exit /b 1
)

echo.
echo [2/3] Compiling BluetoothConsole.cpp...
echo.

cl BluetoothConsole.cpp /EHsc /std:c++17 /await /W3 ^
   /I"%CPPWINRT_INCLUDE%" ^
   /DWINRT_LEAN_AND_MEAN ^
   /link ws2_32.lib bthprops.lib ole32.lib oleaut32.lib runtimeobject.lib /out:BluetoothConsole.exe

if errorlevel 1 (
    echo.
    echo [ERROR] Compilation failed!
    echo.
    echo Troubleshooting:
    echo   1. Make sure Windows SDK 10.0.26100.0 is installed
    echo   2. Make sure C++/WinRT headers exist at: %CPPWINRT_INCLUDE%
    echo   3. Make sure Visual Studio has "C++ WinRT tools" component installed
    echo   4. Try running: cppwinrt.exe -help to verify tool availability
    pause
    exit /b 1
)

echo.
echo [3/3] Build successful!
echo.
echo Output: BluetoothConsole.exe
echo.
echo This version supports:
echo   - Classic Bluetooth device discovery (BluetoothFindFirstDevice API)
echo   - BLE device scanning (BluetoothLEAdvertisementWatcher)
echo   - All BT device enumeration (WinRT DeviceWatcher)
echo   - Merged/deduplicated device list from all scan methods
echo.
pause
