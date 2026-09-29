cmake_minimum_required(VERSION 3.22)
project(replaygain_scanner)

add_library(replaygain_scanner SHARED
  src/replaygain_jni.cpp
  src/ebur128.c
)

target_include_directories(replaygain_scanner PRIVATE
  src
  include
)

target_link_libraries(replaygain_scanner
  log
  mediandk
  android
)

target_compile_options(replaygain_scanner PRIVATE
  -O3 -ffast-math -funroll-loops
  -march=armv8-a+simd -ftree-vectorize
)

# 16KB page size required for Play Store Android 15+
target_link_options(replaygain_scanner PRIVATE
  "-Wl,-z,max-page-size=16384"
)

set_target_properties(replaygain_scanner PROPERTIES
  CXX_STANDARD 17
  OUTPUT_NAME "replaygain_scanner"
)
