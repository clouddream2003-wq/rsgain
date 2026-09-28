#!/bin/bash
set -e
: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME not set}"
NDK_DIR="$ANDROID_NDK_HOME"
TOOLCHAIN="$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64"
API=29
TARGET=aarch64-linux-android
SYSROOT="$TOOLCHAIN/sysroot"
export CC="$TOOLCHAIN/bin/${TARGET}${API}-clang"
export CXX="$TOOLCHAIN/bin/${TARGET}${API}-clang++"
export AR="$TOOLCHAIN/bin/llvm-ar"
export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
export STRIP="$TOOLCHAIN/bin/llvm-strip"
export CFLAGS="-O3 -fPIC"
export CXXFLAGS="-O3 -fPIC"
TC="$NDK_DIR/build/cmake/android.toolchain.cmake"
DEPS_DIR="$(pwd)/deps"
SRC_DIR="$(pwd)/deps-src"
mkdir -p "$DEPS_DIR" "$SRC_DIR"
cd "$SRC_DIR"

if [! -d zlib-1.3.1 ]; then
  curl -L -o zlib.tar.gz https://zlib.net/zlib-1.3.1.tar.gz
  tar xzf zlib.tar.gz
fi
cmake -S zlib-1.3.1 -B zlib-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS_DIR" -DBUILD_SHARED_LIBS=OFF
cmake --build zlib-build
cmake --install zlib-build

if [! -d libebur128 ]; then
  git clone --depth 1 https://github.com/jiixyj/libebur128.git
fi
cmake -S libebur128 -B ebur128-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS_DIR" -DBUILD_SHARED_LIBS=OFF
cmake --build ebur128-build
cmake --install ebur128-build

if [! -d fmt-11.1.4 ]; then
  curl -L -o fmt.tar.gz https://github.com/fmtlib/fmt/archive/refs/tags/11.1.4.tar.gz
  tar xzf fmt.tar.gz
fi
cmake -S fmt-11.1.4 -B fmt-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS_DIR" -DBUILD_SHARED_LIBS=OFF \
  -DFMT_DOC=OFF -DFMT_TEST=OFF
cmake --build fmt-build
cmake --install fmt-build

if [! -d taglib-2.0.2 ]; then
  curl -L -o taglib.tar.gz https://github.com/taglib/taglib/releases/download/v2.0.2/taglib-2.0.2.tar.gz
  tar xzf taglib.tar.gz
fi
cmake -S taglib-2.0.2 -B taglib-build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TC" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS_DIR" -DCMAKE_PREFIX_PATH="$DEPS_DIR" \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_BINDINGS=OFF
cmake --build taglib-build
cmake --install taglib-build

if [! -d ffmpeg-7.1 ]; then
  curl -L -o ffmpeg.tar.gz https://ffmpeg.org/releases/ffmpeg-7.1.tar.gz
  tar xzf ffmpeg.tar.gz
fi
cd ffmpeg-7.1
./configure \
  --prefix="$DEPS_DIR" \
  --target-os=android --arch=aarch64 --cpu=armv8-a \
  --cc="$CC" --cxx="$CXX" --ar="$AR" --ranlib="$RANLIB" --strip="$STRIP" \
  --sysroot="$SYSROOT" \
  --enable-cross-compile --enable-asm --enable-pic \
  --disable-programs --disable-doc \
  --disable-avdevice --disable-swscale --disable-postproc --disable-avfilter \
  --disable-network --disable-encoders --disable-muxers --disable-debug \
  --extra-cflags="-O3 -fPIC" \
  --extra-ldflags="-Wl,-z,max-page-size=16384"
make -j$(nproc)
make install
cd..
