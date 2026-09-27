cmake_minimum_required(VERSION 3.22)
project(rgain_jni)

# Find ebur128 sources - works whether submodule is at root or subfolder
file(GLOB EBUR128_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/../submodules/ebur128/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../submodules/ebur128/ebur128/*.c
)

if(NOT EBUR128_SRC)
    message(FATAL_ERROR "ebur128 sources not found! Check submodules/ebur128")
endif()

add_library(ebur128 STATIC ${EBUR128_SRC})

target_include_directories(ebur128 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../submodules/ebur128
    ${CMAKE_CURRENT_SOURCE_DIR}/../submodules/ebur128/ebur128
)

add_library(rgain_jni SHARED rsgain_jni.cpp)

target_compile_features(rgain_jni PRIVATE cxx_std_17)

target_include_directories(rgain_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../submodules/ebur128
    ${CMAKE_CURRENT_SOURCE_DIR}/../submodules/ebur128/ebur128
)

find_library(log-lib log)
target_link_libraries(rgain_jni PRIVATE ebur128 ${log-lib})
