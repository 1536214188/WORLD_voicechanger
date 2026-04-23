@echo off
echo =========================================
echo       [1/3] 正在编译 VoiceChanger SDK...
echo =========================================
cd /d D:\voice_project\voicechanger_sdk\src
cl /nologo /LD /EHsc /D VC_EXPORTS voicechanger.cpp ..\lib\world.lib /I ..\include /I D:\voice_project\World\src

:: /Y 表示覆盖文件时不再询问是或否
copy /Y voicechanger.lib ..\lib\
copy /Y voicechanger.dll ..\demo\

echo.
echo =========================================
echo       [2/3] 正在编译 测试 Demo...
echo =========================================
cd /d D:\voice_project\voicechanger_sdk\demo
cl /nologo /EHsc demo.cpp ..\lib\voicechanger.lib /I ..\include

echo.
echo =========================================
echo       [3/3] 运行测试...
echo =========================================
demo.exe

echo.
pause