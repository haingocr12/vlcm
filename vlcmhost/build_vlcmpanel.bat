@echo off
rem Build vlcmpanel.exe (bang dieu khien train) bang trinh bien dich cua Visual Studio.
rem Ket qua: vlcmpanel.exe canh file nay. Dat exe cung thu muc voi vlcmhost.exe (dung chung thu muc data\).
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (echo Khong tim thay vswhere.exe - chua cai Visual Studio? & pause & exit /b 1)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set VS=%%i
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || (echo Khong goi duoc vcvars32.bat & pause & exit /b 1)
cl /nologo /EHsc /std:c++17 /utf-8 /O2 /MT "%~dp0vlcmpanel.cpp" /Fe"%~dp0vlcmpanel.exe" /Fo"%TEMP%\vlcmpanel.obj" /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shell32.lib
if errorlevel 1 (echo Build loi & pause & exit /b 1)
echo OK: %~dp0vlcmpanel.exe
pause
