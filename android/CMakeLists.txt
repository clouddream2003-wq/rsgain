cmake_minimum_required(VERSION 3.22)
project(rgscanner LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(DEPS_PREFIX "" CACHE PATH "Dependencies install prefix")
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")

if(NOT DEPS_PREFIX)
  message(FATAL_ERROR "DEPS_PREFIX is empty. Pass -DDEPS_PREFIX=<workspace>/deps")
endif()

# FFmpeg and other dependencies can be installed in lib/, lib64/, or an
# ABI-specific subdirectory depending on their build configuration.
set(_DEPS_LIBRARY_DIRS
  "${DEPS_PREFIX}/lib"
  "${DEPS_PREFIX}/lib64"
  "${DEPS_PREFIX}/lib/${ANDROID_ABI}"
  "${DEPS_PREFIX}/lib/aarch64-linux-android"
  "${DEPS_PREFIX}/lib/arm64-v8a"
)

# Clear cached NOTFOUND results so a reconfigure can discover newly installed
# dependencies instead of retaining a previous failed lookup.
foreach(_libname avformat avcodec avutil swresample tag ebur128 fmt z)
  string(TOUPPER "${_libname}" _upper)
  unset(${_upper}_LIB CACHE)
  find_library(${_upper}_LIB
    NAMES ${_libname}
    PATHS ${_DEPS_LIBRARY_DIRS}
    NO_DEFAULT_PATH
  )
endforeach()

# Map to expected variable names used below (kept for compatibility).
set(TAGLIB_LIB "${TAG_LIB}")
set(ZLIB_LIB "${Z_LIB}")

foreach(_required AVFORMAT_LIB AVCODEC_LIB AVUTIL_LIB SWRESAMPLE_LIB
                 TAGLIB_LIB EBUR128_LIB FMT_LIB ZLIB_LIB)
  if(NOT ${_required})
    message(STATUS "Dependency search directories:")
    foreach(_dir IN LISTS _DEPS_LIBRARY_DIRS)
      message(STATUS "  ${_dir}")
    endforeach()
    message(FATAL_ERROR
      "${_required} not found. Check that android/build-deps.sh installed "
      "the corresponding library under ${DEPS_PREFIX}/lib (or an ABI subdirectory).")
  else()
    message(STATUS "${_required} = ${${_required}}")
  endif()
endforeach()

find_library(LOG_LIB log)
find_library(M_LIB m)
find_library(ANDROID_LIB android)

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/config.h"
  @ONLY
)

add_library(replaygain_scanner SHARED
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/scan.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/tag.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/jni/rgscanner_jni.cpp"
)

target_include_directories(replaygain_scanner PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/external"
  "${DEPS_PREFIX}/include"
)

target_link_libraries(replaygain_scanner PRIVATE
  ${AVFORMAT_LIB}
  ${AVCODEC_LIB}
  ${SWRESAMPLE_LIB}
  ${AVUTIL_LIB}
  ${TAGLIB_LIB}
  ${EBUR128_LIB}
  ${FMT_LIB}
  ${ZLIB_LIB}
  ${LOG_LIB}
  ${M_LIB}
)

# 16 KB page-size alignment required for compatibility with Android 15+.
target_link_options(replaygain_scanner PRIVATE
  "LINKER:-z,max-page-size=16384"
)

set_target_properties(replaygain_scanner PROPERTIES
  OUTPUT_NAME "replaygain_scanner"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
