@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"

set "CHIPLAB_GXX="
where /q g++.exe
if not errorlevel 1 set "CHIPLAB_GXX=g++.exe"
if not defined CHIPLAB_GXX if exist "C:\msys64\ucrt64\bin\g++.exe" set "CHIPLAB_GXX=C:\msys64\ucrt64\bin\g++.exe"
if not defined CHIPLAB_GXX goto cmake_build

echo Compilando o aplicativo nativo ChipLab com GCC...
"%CHIPLAB_GXX%" -std=c++17 -O2 -Wall -Wextra -finput-charset=UTF-8 -DUNICODE -D_UNICODE -DNOMINMAX -static -static-libgcc -static-libstdc++ -municode -mwindows "src\main.cpp" -o "ChipLab.exe" -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lshell32
if errorlevel 1 goto build_failed
echo Pronto: %~dp0ChipLab.exe
exit /b 0

:cmake_build
set "CHIPLAB_CMAKE="
where /q cmake.exe
if not errorlevel 1 set "CHIPLAB_CMAKE=cmake.exe"
if not defined CHIPLAB_CMAKE if exist "C:\Program Files\CMake\bin\cmake.exe" set "CHIPLAB_CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not defined CHIPLAB_CMAKE goto missing_compiler
echo Configurando com CMake. E necessario ter um compilador C++ instalado.
"%CHIPLAB_CMAKE%" -S "%~dp0." -B "%~dp0build\cmake"
if errorlevel 1 goto build_failed
"%CHIPLAB_CMAKE%" --build "%~dp0build\cmake" --config Release --target ChipLab
if errorlevel 1 goto build_failed
copy /y "%~dp0build\cmake\bin\ChipLab.exe" "%~dp0ChipLab.exe" >nul
if errorlevel 1 goto build_failed
echo Pronto: %~dp0ChipLab.exe
exit /b 0

:missing_compiler
echo Nao foi encontrado g++ nem CMake. Instale um compilador C++17 para recompilar.
echo O arquivo ChipLab.exe fornecido pode ser aberto diretamente, sem recompilar.
exit /b 1

:build_failed
echo A compilacao falhou. Consulte as mensagens do compilador acima.
exit /b 1
