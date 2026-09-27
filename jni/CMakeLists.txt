cmake_minimum_required(VERSION 3.22)
project(rgain_jni)

include(FetchContent)
FetchContent_Declare(
  ebur128
  GIT_REPOSITORY https://github.com/jiixyj/libebur128.git
  GIT_TAG v1.2.6
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ebur128)

add_library(rgain_jni SHARED rsgain_jni.cpp)
target_compile_features(rgain_jni PRIVATE cxx_std_17)

# ebur128 headers are in the source root
target_include_directories(rgain_jni PRIVATE ${ebur128_SOURCE_DIR})

find_library(log-lib log)
target_link_libraries(rgain_jni PRIVATE ebur128 ${log-lib})
