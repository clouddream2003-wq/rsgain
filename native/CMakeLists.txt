cmake_minimum_required(VERSION 3.22)
project(replaygain_scanner)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS OFF)
include(FetchContent)
FetchContent_Declare(
  ebur128
  GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
  GIT_TAG master
)
FetchContent_MakeAvailable(ebur128)
add_library(replaygain_scanner SHARED jni_bridge.cpp)
find_library(log-lib log)
target_include_directories(replaygain_scanner PRIVATE ${ebur128_SOURCE_DIR}/ebur128)
target_link_libraries(replaygain_scanner ebur128 ${log-lib})
set_target_properties(replaygain_scanner PROPERTIES OUTPUT_NAME replaygain_scanner)
