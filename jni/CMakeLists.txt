cmake_minimum_required(VERSION 3.22)
project(rgain_jni)

add_library(rgain_jni SHARED rsgain_jni.cpp)

target_compile_features(rgain_jni PRIVATE cxx_std_17)

find_library(log-lib log)
target_link_libraries(rgain_jni PRIVATE ${log-lib})
