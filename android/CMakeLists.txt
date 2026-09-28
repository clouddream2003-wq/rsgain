cmake_minimum_required(VERSION 3.22)
project(rgscanner LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(DEPS_PREFIX "" CACHE PATH "Dependencies install prefix")
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")

# Search both lib and lib64, keep NO_DEFAULT_PATH to avoid host libs
foreach(_libname avformat avcodec avutil swresample tag ebur128 fmt z)
  string(TOUPPER "${_libname}" _upper)
  find_library(${_upper}_LIB ${_libname}
    PATHS "${DEPS_PREFIX}/lib" "${DEPS_PREFIX}/lib64"
    NO_DEFAULT_PATH)
endforeach()

# Map to expected variable names used below (kept for compatibility)
set(AVFORMAT_LIB ${AVFORMAT_LIB})
set(AVCODEC_LIB ${AVCODEC_LIB})
set(AVUTIL_LIB ${AVUTIL_LIB})
set(SWRESAMPLE_LIB ${SWRESAMPLE_LIB})
set(TAGLIB_LIB ${TAG_LIB})
if(NOT TAGLIB_LIB)
  find_library(TAGLIB_LIB tag PATHS "${DEPS_PREFIX}/lib" "${DEPS_PREFIX}/lib64" NO_DEFAULT_PATH)
endif()
set(EBUR128_LIB ${EBUR128_LIB})
set(FMT_LIB ${FMT_LIB})
set(ZLIB_LIB ${Z_LIB})
if(NOT ZLIB_LIB)
  find_library(ZLIB_LIB z PATHS "${DEPS_PREFIX}/lib" "${DEPS_PREFIX}/lib64" NO_DEFAULT_PATH)
endif()

find_library(LOG_LIB log)
find_library(M_LIB m)
find_library(ANDROID_LIB android)

foreach(l AVFORMAT_LIB AVCODEC_LIB AVUTIL_LIB SWRESAMPLE_LIB TAGLIB_LIB EBUR128_LIB FMT_LIB ZLIB_LIB)
  if(NOT ${l})
    message(FATAL_ERROR "${l} not found under ${DEPS_PREFIX}/lib")
  else()
    message(STATUS "${l} = ${${l}}")
  endif()
endforeach()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/config.h" @ONLY)

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
  ${AVFORMAT_LIB} ${AVCODEC_LIB} ${SWRESAMPLE_LIB} ${AVUTIL_LIB}
  ${TAGLIB_LIB} ${EBUR128_LIB} ${FMT_LIB} ${ZLIB_LIB}
  ${LOG_LIB} ${M_LIB}
)

# 16KB page size required for Android 15+
target_link_options(replaygain_scanner PRIVATE "LINKER:-z,max-page-size=16384")

set_target_properties(replaygain_scanner PROPERTIES
  OUTPUT_NAME "replaygain_scanner"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
