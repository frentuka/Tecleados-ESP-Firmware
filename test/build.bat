@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d "x:\appcrap\VStudio\ESP32\esp-now-test\test"
cl.exe /nologo /W3 /std:c17 /D_CRT_SECURE_NO_WARNINGS /Iinclude /Fe:test_runner.exe main.c
