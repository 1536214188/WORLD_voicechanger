cmake_minimum_required(VERSION 3.10)

project(voicechanger)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# ======================
# WORLD源码
# ======================

set(WORLD_SRC
    world/src/cheaptrick.cpp
    world/src/d4c.cpp
    world/src/dio.cpp
    world/src/fft.cpp
    world/src/harvest.cpp
    world/src/matlabfunctions.cpp
    world/src/stonemask.cpp
    world/src/synthesis.cpp
    world/src/common.cpp
    world/src/codec.cpp
)

# 编译WORLD静态库
add_library(
        world
        STATIC
        ${WORLD_SRC}
)

target_include_directories(
        world
        PUBLIC
        world/include
)

# ======================
# voicechanger
# ======================

add_library(
        voicechanger
        SHARED
        voicechanger/voicechanger.c
)

target_include_directories(
        voicechanger
        PUBLIC
        voicechanger
        world/include
)

target_link_libraries(
        voicechanger
        world
        log
        m
)

# 支持16KB page size (推荐)
target_link_options(
        voicechanger PRIVATE
        "-Wl,-z,max-page-size=16384"
)