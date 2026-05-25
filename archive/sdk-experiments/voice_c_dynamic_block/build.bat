@echo off
setlocal

echo =========================================
echo        [1/2] Build Dynamic VoiceChanger
echo =========================================

cd /d %~dp0src

cl /nologo /utf-8 /LD voicechanger.c ^
/DVC_EXPORTS ^
/I ..\include ^
..\lib\world.lib

if errorlevel 1 (
echo Dynamic VoiceChanger build failed
pause
exit /b 1
)

copy /Y voicechanger.lib ..\lib\
copy /Y voicechanger.dll ..\demo\

echo.
echo =========================================
echo        [2/2] Build Dynamic Demo
echo =========================================

cd /d %~dp0demo

cl /nologo /utf-8 demo.c ^
/I ..\include ^
..\lib\voicechanger.lib

if errorlevel 1 (
echo Dynamic Demo build failed
pause
exit /b 1
)

echo.
echo =========================================
echo            Run Dynamic Demo
echo =========================================

demo.exe

pause
