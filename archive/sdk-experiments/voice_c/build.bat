@echo off

echo =========================================
echo        [1/2] 编译 VoiceChanger
echo =========================================

cd /d %~dp0src

cl /nologo /LD voicechanger.c ^
..\lib\world.lib ^
/I ..\include ^
/I D:\voice_project\World\src

copy /Y voicechanger.lib ..\lib\
copy /Y voicechanger.dll ..\demo\

echo.
echo =========================================
echo        [2/2] 编译 Demo
echo =========================================

cd /d %~dp0demo

cl /nologo demo.c ^
..\lib\voicechanger.lib ^
/I ..\include ^
/link ^
..\lib\voicechanger.lib

echo.
echo =========================================
echo            运行 Demo
echo =========================================

demo.exe

pause