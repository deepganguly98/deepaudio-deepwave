@echo off
rem Builds DeepWave (VST3 + Standalone .exe) on Windows 10/11 x64.
rem Needs: Visual Studio 2022 (workload "Desktop development with C++") and CMake 3.22+  (both on PATH / Developer prompt).
rem Run from the project root:   windows\build_windows.bat
setlocal
cd /d "%~dp0\.."

cmake -B build-win -G "Visual Studio 17 2022" -A x64 || goto :fail
cmake --build build-win --config Release --target WaveGrid_Standalone WaveGrid_VST3 || goto :fail

rem collect the results into dist\
if exist dist rmdir /s /q dist
mkdir dist
for /d /r "build-win\WaveGrid_artefacts\Release" %%d in (*.vst3) do xcopy /e /i /y "%%d" "dist\%%~nxd" >nul
for /r "build-win\WaveGrid_artefacts\Release\Standalone" %%f in (*.exe) do copy /y "%%f" "dist\" >nul

echo.
echo Done. Results are in dist\
echo   DeepWave.vst3   -> copy the whole folder to  C:\Program Files\Common Files\VST3\
echo   DeepWave.exe    -> standalone app, run it directly
exit /b 0
:fail
echo.
echo BUILD FAILED - read the first error above.
exit /b 1
