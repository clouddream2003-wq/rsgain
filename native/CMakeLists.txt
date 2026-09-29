cmake_minimum_required(VERSION 3.22)
project(replaygain_scanner)

set(CMAKE_CXX_STANDARD 17)

# Link upstream sources directly, no copy-paste of logic
add_library(replaygain_scanner SHARED
  jni_bridge.cpp
 ../src/rsgain.cpp
 ../src/scan.cpp
 ../src/easymode.cpp
 ../src/tag.cpp
 ../src/output.cpp
)

find_library(log-lib log)

# Add FFmpeg, taglib, libebur128 via your prebuilt arm64 deps
# Keep as SHARED for max speed, no Java fallback
target_link_libraries(replaygain_scanner
  avformat avcodec avutil swresample
  taglib ebur128
  ${log-lib}
)

set_target_properties(replaygain_scanner PROPERTIES
  OUTPUT_NAME replaygain_scanner
  BUILD_RPATH_USE_ORIGIN TRUE
)
