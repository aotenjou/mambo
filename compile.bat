@echo off
setlocal enabledelayedexpansion

set "INCLUDE=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include;D:\Windows Kits\10\Include\10.0.26100.0\ucrt;D:\Windows Kits\10\Include\10.0.26100.0\shared;D:\Windows Kits\10\Include\10.0.26100.0\um"
set "LIB=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\lib\x64;D:\Windows Kits\10\Lib\10.0.26100.0\ucrt\x64;D:\Windows Kits\10\Lib\10.0.26100.0\um\x64"

"D:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe" BluetoothConsole.cpp /EHsc /MD /link ws2_32.lib bthprops.lib ole32.lib /out:BluetoothConsole.exe
