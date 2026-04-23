cmake_minimum_required(VERSION 3.15)
project(VoiceChangerApp)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ==========================================
# 关键修复 1：强制使用 /MT (静态运行库) 进行编译
# 这样就能和 world.lib 的 MT_StaticRelease 匹配了
# ==========================================
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# 告诉编译器去哪里找头文件
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 告诉编译器去哪里找 .lib 文件
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

# 添加你的源代码
set(SOURCES 
    src/main.cpp 
    src/voicechanger.cpp
)

# 生成可执行文件
add_executable(main ${SOURCES})

# ==========================================
# 关键修复 2：添加 /utf-8 编译选项，消除 C4819 中文乱码警告
# ==========================================
if(MSVC)
    target_compile_options(main PRIVATE /utf-8)
endif()

# 链接第三方库
target_link_libraries(main 
    portaudio 
    world 
    winmm 
    ole32 
    setupapi
)