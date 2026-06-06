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

set "INCLUDE=%MSVC_PATH%\include;%SDK_INCLUDE%\ucrt;%SDK_INCLUDE%\um;%SDK_INCLUDE%\shared;%CPPWINRT_INCLUDE%"
set "LIB=%MSVC_PATH%\lib\x64;%SDK_LIB%\ucrt\x64;%SDK_LIB%\um\x64"

echo ========================================
echo   Building BluetoothConsole (BLE)
echo ========================================

"%MSVC_PATH%\bin\Hostx64\x64\cl.exe" BluetoothConsole.cpp /EHsc /std:c++20 /await:strict /W3 /DWINRT_LEAN_AND_MEAN /link ws2_32.lib bthprops.lib ole32.lib oleaut32.lib runtimeobject.lib /out:BluetoothConsole.exe

if errorlevel 1 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo [OK] Build successful: BluetoothConsole.exe
pause
