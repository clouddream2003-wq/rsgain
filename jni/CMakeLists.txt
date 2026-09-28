cmake_minimum_required(VERSION 3.22)
project(rgain_jni)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
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

# --- FFmpeg for Android (true Termux speed, like rsgain scan.cpp) ---
# Option 1: Prebuilt .so in app/src/main/jniLibs/<abi>/
# Place libavformat.so, libavcodec.so, libavutil.so, libswresample.so there
# from e.g. https://github.com/arthenica/ffmpeg-kit/releases
# Option 2: Build FFmpeg yourself via https://github.com/ffmpeg/ffmpeg
#
# If FFmpeg headers/libs are found, we use them (software decode, scales to 6 threads).
# If not found, we fall back to NDK MediaCodec (hardware, 3 threads max).

set(FFMPEG_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/ffmpeg/include" CACHE PATH "FFmpeg include dir")
set(FFMPEG_LIB_DIR "${CMAKE_SOURCE_DIR}/ffmpeg/lib/${ANDROID_ABI}" CACHE PATH "FFmpeg lib dir")

find_path(AVFORMAT_INCLUDE libavformat/avformat.h
  PATHS ${FFMPEG_INCLUDE_DIR} /usr/include
  NO_DEFAULT_PATH)
find_library(AVFORMAT_LIB avformat
  PATHS ${FFMPEG_LIB_DIR}
  NO_DEFAULT_PATH)
find_library(AVCODEC_LIB avcodec PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
find_library(AVUTIL_LIB avutil PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)
find_library(SWRESAMPLE_LIB swresample PATHS ${FFMPEG_LIB_DIR} NO_DEFAULT_PATH)

if(AVFORMAT_INCLUDE AND AVFORMAT_LIB AND AVCODEC_LIB AND AVUTIL_LIB AND SWRESAMPLE_LIB)
  message(STATUS "FFmpeg found - using true rsgain scan.cpp pipeline (Termux 15s)")
  set(HAS_FFMPEG TRUE)
else()
  message(STATUS "FFmpeg not found - falling back to NDK MediaCodec (18-22s)")
  set(HAS_FFMPEG FALSE)
endif()

add_library(rgain_jni SHARED rsgain_jni.cpp)
target_compile_features(rgain_jni PRIVATE cxx_std_17)
target_compile_options(rgain_jni PRIVATE -O3 -ffast-math)
target_include_directories(rgain_jni PRIVATE ${ebur128_SOURCE_DIR} ${ebur128_SOURCE_DIR}/ebur128)

find_library(log-lib log)
find_library(mediandk-lib mediandk)

if(HAS_FFMPEG)
  target_include_directories(rgain_jni PRIVATE ${AVFORMAT_INCLUDE} ${FFMPEG_INCLUDE_DIR})
  target_compile_definitions(rgain_jni PRIVATE HAS_FFMPEG=1)
  target_link_libraries(rgain_jni PRIVATE
    ebur128
    ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWRESAMPLE_LIB}
    ${log-lib} ${mediandk-lib} z)
else()
  target_link_libraries(rgain_jni PRIVATE ebur128 ${log-lib} ${mediandk-lib})
endif()
