cmake_minimum_required(VERSION 3.15)
project(VoiceChangerApp)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

set(SOURCES 
    src/main.c
    src/voicechanger.c
)

add_executable(main ${SOURCES})

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

if(MSVC)
    add_custom_command(TARGET main POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/release_package/portaudio.dll"
        $<TARGET_FILE_DIR:main>
    )
endif()