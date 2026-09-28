cmake_minimum_required(VERSION 3.22)
project(rgscanner LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(DEPS_PREFIX "" CACHE PATH "Dependencies install prefix")
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")

# Resolve a dependency static library: check the exact lib/lib64 paths first
# (the deps build always produces lib<name>.a there), then fall back to
# find_library. Same result as before when the files exist, but robust
# against search-path quirks.
function(_resolve_dep var name)
  foreach(_d lib lib64)
    if(EXISTS "${DEPS_PREFIX}/${_d}/lib${name}.a")
      set(${var} "${DEPS_PREFIX}/${_d}/lib${name}.a" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  find_library(_fl ${name}
    PATHS "${DEPS_PREFIX}/lib" "${DEPS_PREFIX}/lib64"
    NO_DEFAULT_PATH)
  set(${var} "${_fl}" PARENT_SCOPE)
endfunction()

_resolve_dep(AVFORMAT_LIB avformat)
_resolve_dep(AVCODEC_LIB avcodec)
_resolve_dep(AVUTIL_LIB avutil)
_resolve_dep(SWRESAMPLE_LIB swresample)
_resolve_dep(TAGLIB_LIB tag)
_resolve_dep(EBUR128_LIB ebur128)
_resolve_dep(FMT_LIB fmt)
_resolve_dep(ZLIB_LIB z)

find_library(LOG_LIB log)
find_library(M_LIB m)
find_library(ANDROID_LIB android)

foreach(l AVFORMAT_LIB AVCODEC_LIB AVUTIL_LIB SWRESAMPLE_LIB TAGLIB_LIB EBUR128_LIB FMT_LIB ZLIB_LIB)
  if(NOT ${l})
    message(FATAL_ERROR "${l} not found under ${DEPS_PREFIX}/lib. DEPS_PREFIX=${DEPS_PREFIX}. Check the Build dependencies step log to confirm the static library was installed.")
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
