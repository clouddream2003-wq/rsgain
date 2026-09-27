cmake_minimum_required(VERSION 3.22)
project(rgain_jni)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Termux-like speed: -O3
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math -funroll-loops")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG -ffast-math -funroll-loops")
set(CMAKE_BUILD_TYPE Release)

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
target_compile_options(rgain_jni PRIVATE -O3 -ffast-math)

target_include_directories(rgain_jni PRIVATE ${ebur128_SOURCE_DIR} ${ebur128_SOURCE_DIR}/ebur128)

find_library(log-lib log)
target_link_libraries(rgain_jni PRIVATE ebur128 ${log-lib})
