@echo off
cd /d "x:\appcrap\VStudio\ESP32\esp-now-test\test"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cl.exe /nologo /std:c17 /W3 /Fe:test_runner.exe ^
  /Iinclude ^
  /I"x:\appcrap\VStudio\ESP32\esp-now-test\components\keyboard\include" ^
  /I"x:\appcrap\VStudio\ESP32\esp-now-test\components\event_bus\include" ^
  /I"x:\appcrap\VStudio\ESP32\esp-now-test\components\config_module\include" ^
  /I"x:\appcrap\VStudio\ESP32\esp-now-test\components\ble_module\include" ^
  /I"x:\appcrap\VStudio\ESP32\esp-now-test\components\split" ^
  /I"x:\appcrap\VStudio\ESP32\esp-now-test\components\split\include" ^
  main.c
rem NOTE: split tests are compiled only by GCC/Clang (see main.c #ifndef _MSC_VER).
rem       split_protocol.h uses __attribute__((packed)) and C23 enum syntax.
