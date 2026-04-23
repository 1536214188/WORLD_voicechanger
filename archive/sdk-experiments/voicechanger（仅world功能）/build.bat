@echo off
setlocal enabledelayedexpansion

:: 确认路径
set WORLD_LIB=D:\voice_project\voicechanger\lib\world.lib
set WORLD_INC=D:\voice_project\World\src

echo [1/3] 正在编译 SDK...
cd /d D:\voice_project\voicechanger\src
:: 这里的 /link 后面跟的是库文件，输出名为 voicechanger.dll
cl /nologo /LD /EHsc /D VC_EXPORTS voicechanger.cpp /I ..\include /I "%WORLD_INC%" /link "%WORLD_LIB%" /OUT:voicechanger.dll /IMPLIB:voicechanger.lib

if not exist voicechanger.lib (
    echo [错误] SDK 编译失败，未生成 voicechanger.lib
    pause
    exit /b
)

echo [2/3] 复制文件...
copy /Y voicechanger.lib ..\lib\
copy /Y voicechanger.dll ..\demo\

echo [3/3] 编译测试 Demo...
cd /d D:\voice_project\voicechanger\demo
cl /nologo /EHsc demo.cpp ..\lib\voicechanger.lib /I ..\include /Fe:demo.exe

if not exist demo.exe (
    echo [错误] Demo 编译失败
    pause
    exit /b
)

echo [4/4] 运行测试...
demo.exe
pause