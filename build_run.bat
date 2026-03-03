@echo off
set IDF_PATH=x:\appcrap\Espressif\frameworks\esp-idf-v5.5.3
set IDF_TOOLS_PATH=x:\appcrap\Espressif
set PATH=x:\appcrap\Espressif\tools\cmake\3.30.2\bin;x:\appcrap\Espressif\tools\ninja\1.12.1;x:\appcrap\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin;x:\appcrap\Espressif\tools\idf-git\2.44.0\cmd;x:\appcrap\Espressif\python_env\idf5.5_py3.11_env\Scripts;%PATH%

echo IDF_PATH=%IDF_PATH%
echo Running build...

x:\appcrap\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe x:\appcrap\Espressif\frameworks\esp-idf-v5.5.3\tools\idf.py build > build_output.txt 2>&1
echo Build exit code: %ERRORLEVEL% >> build_output.txt
