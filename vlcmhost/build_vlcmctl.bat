@echo off
rem Build vlcmctl.exe (tool dieu khien) bang trinh bien dich cua Visual Studio.
rem Chay: nhap dup file nay, hoac chay trong cmd. Ket qua: vlcmctl.exe canh file nay.
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (echo Khong tim thay vswhere.exe - chua cai Visual Studio? & pause & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || (echo Khong goi duoc vcvars32.bat & pause & exit /b 1)
cl /nologo /EHsc /std:c++17 /utf-8 /O2 /MT "%~dp0vlcmctl.cpp" /Fe"%~dp0vlcmctl.exe" /Fo"%TEMP%\vlcmctl.obj" user32.lib
if errorlevel 1 (echo Build loi & pause & exit /b 1)
echo OK: %~dp0vlcmctl.exe
pause
