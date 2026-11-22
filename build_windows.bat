@echo off
REM Builds VoidCarve.vst3 and the installer .exe on Windows.
REM Requires: Visual Studio 2022 (Desktop C++), CMake 3.22+, Git, Inno Setup 6.
cd /d "%~dp0"
cmake -B build -A x64 || goto :err
cmake --build build --config Release --parallel || goto :err
"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" installer\VoidCarve.iss || goto :err
echo.
echo DONE!  Installer: installer\Output\VoidCarve-1.0.0-Windows-Setup.exe
pause
exit /b 0
:err
echo Build failed.
pause
exit /b 1
