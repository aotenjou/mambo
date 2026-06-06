@echo off
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\auxiliary\Build\vcvarsall.bat" x64
cl BluetoothConsole.cpp /EHsc /link ws2_32.lib bthprops.lib /out:BluetoothConsole.exe
