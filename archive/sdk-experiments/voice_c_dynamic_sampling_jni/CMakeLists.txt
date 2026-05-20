cmake_minimum_required(VERSION 3.10)
project(VoiceChangerDynamicSamplingSDK C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(voicechanger SHARED
    ${CMAKE_SOURCE_DIR}/src/voicechanger.c
)

target_compile_definitions(voicechanger PRIVATE VC_EXPORTS)

target_link_libraries(voicechanger
    ${CMAKE_SOURCE_DIR}/lib/world.lib
)

add_executable(dynamic_sampling_demo
    ${CMAKE_SOURCE_DIR}/demo/demo.c
)

target_link_libraries(dynamic_sampling_demo voicechanger)
