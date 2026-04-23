@echo off

echo =========================================
echo        [1/2] 编译 VoiceChanger
echo =========================================

cd /d %~dp0src

cl /nologo /LD voicechanger.c ^
/I ..\include ^
/I D:\voice_project\World\src ^
..\lib\world.lib

if errorlevel 1 (
echo VoiceChanger 编译失败
pause
exit
)

copy /Y voicechanger.lib ..\lib\
copy /Y voicechanger.dll ..\demo\

echo.
echo =========================================
echo        [2/2] 编译 Demo
echo =========================================

cd /d %~dp0demo

cl /nologo demo.c ^
/I ..\include ^
..\lib\voicechanger.lib

if errorlevel 1 (
echo Demo 编译失败
pause
exit
)

echo.
echo =========================================
echo            运行 Demo
echo =========================================

demo.exe

pause