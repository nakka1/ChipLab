@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "CHIPLAB_TEST_BUILD=%~dp0build"
if not "%~1"=="" for %%I in ("%~1") do set "CHIPLAB_TEST_BUILD=%%~fI"
cd /d "%~dp0"
if not exist "%CHIPLAB_TEST_BUILD%" mkdir "%CHIPLAB_TEST_BUILD%"
if not exist "%CHIPLAB_TEST_BUILD%" goto test_failed

set "CHIPLAB_GXX="
where /q g++.exe
if not errorlevel 1 set "CHIPLAB_GXX=g++.exe"
if not defined CHIPLAB_GXX if exist "C:\msys64\ucrt64\bin\g++.exe" set "CHIPLAB_GXX=C:\msys64\ucrt64\bin\g++.exe"
if not defined CHIPLAB_GXX goto cmake_test

for %%T in (test_core test_cpu_extended test_project_extended) do (
  call :compile_console %%T
  if errorlevel 1 goto test_failed
)
echo Compilando os testes de integracao Win32...
"%CHIPLAB_GXX%" -std=c++17 -O2 -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -DUNICODE -D_UNICODE -DNOMINMAX -static -static-libgcc -static-libstdc++ "tests\test_gui_win32.cpp" -o "%CHIPLAB_TEST_BUILD%\test_gui.exe" -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lshell32
if errorlevel 1 goto test_failed
echo Compilando a bateria ampliada da interface...
"%CHIPLAB_GXX%" -std=c++17 -O2 -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -DUNICODE -D_UNICODE -DNOMINMAX -static -static-libgcc -static-libstdc++ "tests\test_gui_extended.cpp" -o "%CHIPLAB_TEST_BUILD%\test_gui_extended.exe" -lcomctl32 -lcomdlg32 -lgdi32 -luser32 -lshell32
if errorlevel 1 goto test_failed
call :run_tests "%CHIPLAB_TEST_BUILD%"
if errorlevel 1 goto test_failed
echo Testes concluidos com sucesso.
exit /b 0

:compile_console
echo Compilando %~1...
"%CHIPLAB_GXX%" -std=c++17 -O2 -Wall -Wextra -Wpedantic -finput-charset=UTF-8 -static -static-libgcc -static-libstdc++ "tests\%~1.cpp" -o "%CHIPLAB_TEST_BUILD%\%~1.exe"
exit /b %errorlevel%

:run_tests
pushd "%~1"
if errorlevel 1 exit /b 1
for %%T in (test_core test_cpu_extended test_project_extended) do (
  ".\%%T.exe"
  if errorlevel 1 goto run_failed
)
".\test_gui.exe" --evidence-dir "%CHIPLAB_TEST_BUILD%\test-evidence"
if errorlevel 1 goto run_failed
".\test_gui_extended.exe" --evidence-dir "%CHIPLAB_TEST_BUILD%\test-evidence\gui-extended"
if errorlevel 1 goto run_failed
popd
exit /b 0
:run_failed
popd
exit /b 1

:cmake_test
set "CHIPLAB_CMAKE="
where /q cmake.exe
if not errorlevel 1 set "CHIPLAB_CMAKE=cmake.exe"
if not defined CHIPLAB_CMAKE if exist "C:\Program Files\CMake\bin\cmake.exe" set "CHIPLAB_CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not defined CHIPLAB_CMAKE goto missing_compiler
"%CHIPLAB_CMAKE%" -S "%~dp0." -B "%CHIPLAB_TEST_BUILD%\cmake"
if errorlevel 1 goto test_failed
"%CHIPLAB_CMAKE%" --build "%CHIPLAB_TEST_BUILD%\cmake" --config Release --target test_core test_cpu_extended test_project_extended test_gui test_gui_extended
if errorlevel 1 goto test_failed
call :run_tests "%CHIPLAB_TEST_BUILD%\cmake\bin"
if errorlevel 1 goto test_failed
echo Testes concluidos com sucesso.
exit /b 0

:missing_compiler
echo Nao foi encontrado g++ nem CMake. Os testes precisam de um compilador C++17.
exit /b 1

:test_failed
echo Os testes ou sua compilacao falharam. Consulte as mensagens acima.
exit /b 1
