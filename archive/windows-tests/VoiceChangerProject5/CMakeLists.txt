cmake_minimum_required(VERSION 3.15)
project(VoiceChangerProject5)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

add_executable(main
    src/main.cpp
    src/voicechanger.c
)
target_compile_definitions(main PRIVATE VC_EXPORTS)

if(MSVC)
    target_compile_options(main PRIVATE /utf-8)
endif()

target_link_libraries(main
    portaudio
    world
    winmm
    ole32
    setupapi
)
