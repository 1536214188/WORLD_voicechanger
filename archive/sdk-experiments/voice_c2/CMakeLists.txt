cmake_minimum_required(VERSION 3.10)
project(VoiceChangerSDK)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 包含您的 SDK 头文件和 WORLD 的头文件
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/../World/src)

# 收集所有源文件 (包括您的 cpp 和 WORLD 的 cpp)
file(GLOB_RECURSE SOURCES 
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/../World/src/*.cpp"
)

# 生成动态链接库 (Shared Library)
add_library(voicechanger SHARED ${SOURCES})