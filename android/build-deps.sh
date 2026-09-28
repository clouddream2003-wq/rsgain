#!/usr/bin/env bash
set -euo pipefail

# Build native dependencies for libreplaygain_scanner (Android arm64-v8a)
# Installs static libs into $PREFIX/lib and headers into $PREFIX/include

ABI="arm64-v8a"
API="29"
PREFIX="${GITHUB_WORKSPACE:-$PWD}/deps"
JOBS="$(nproc)"
WORKDIR="/tmp/rg-deps"

: "${ANDROID_NDK_HOME:?ANDROID_NDK_HOME must be set (provided by nttld/setup-ndk)}"

ZLIB_VERSION="1.3.1"
FMT_VERSION="10.2.1"
EBUR128_VERSION="1.2.6"
TAGLIB_VERSION="2.0.2"
FFMPEG_VERSION="7.0.2"

ZLIB_URL="https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/zlib-${ZLIB_VERSION}.tar.gz"
FMT_URL="https://github.com/fmtlib/fmt/archive/refs/tags/${FMT_VERSION}.tar.gz"
EBUR128_URL="https://github.com/jiixyj/libebur128/archive/refs/tags/v${EBUR128_VERSION}.tar.gz"
FFMPEG_URL="https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"

TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
HOST_TAG="linux-x86_64"
LLVM_BIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG/bin"
SYSROOT="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG/sysroot"
TRIPLE="aarch64-linux-android"

export CC="$LLVM_BIN/${TRIPLE}${API}-clang"
export CXX="$LLVM_BIN/${TRIPLE}${API}-clang++"
export AR="$LLVM_BIN/llvm-ar"
export RANLIB="$LLVM_BIN/llvm-ranlib"
export STRIP="$LLVM_BIN/llvm-strip"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

echo "PREFIX=$PREFIX"
echo "NDK=$ANDROID_NDK_HOME"
mkdir -p "$PREFIX" "$WORKDIR/src"

fetch_and_extract() {
  local url="$1"
  local dest_name="$2"
  local archive="$WORKDIR/$(basename "$url")"
  echo "==> Downloading $dest_name"
  echo "    $url"
  rm -f "$archive"
  curl -fSL --retry 3 --connect-timeout 30 -o "$archive" "$url"
  local size
  size=$(wc -c < "$archive")
  echo "    downloaded $size bytes"
  if [ "$size" -lt 10240 ]; then
    echo "ERROR: downloaded file too small ($size bytes), likely an HTML error page:"
    head -c 2000 "$archive" || true
    exit 1
  fi
  if ! tar -tf "$archive" >/dev/null 2>&1; then
    echo "ERROR: file is not a valid tar archive: $archive"
    head -c 2000 "$archive" || true
    exit 1
  fi
  echo "    extracting..."
  tar -xf "$archive" -C "$WORKDIR/src"
}

cmake_build() {
  local srcdir="$1"
  local extra_args="${2:-}"
  echo "==> CMake building $srcdir"
  cmake -S "$srcdir" -B "$srcdir/build" -G Ninja     -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"     -DANDROID_ABI="$ABI"     -DANDROID_PLATFORM="android-$API"     -DCMAKE_ANDROID_STL_TYPE=c++_static     -DCMAKE_INSTALL_PREFIX="$PREFIX"     -DCMAKE_BUILD_TYPE=Release     $extra_args
  cmake --build "$srcdir/build" -j"$JOBS"
  cmake --install "$srcdir/build"
}

fetch_and_extract "$ZLIB_URL" "zlib-$ZLIB_VERSION"
cmake_build "$WORKDIR/src/zlib-$ZLIB_VERSION" "-DBUILD_SHARED_LIBS=OFF"

fetch_and_extract "$FMT_URL" "fmt-$FMT_VERSION"
cmake_build "$WORKDIR/src/fmt-$FMT_VERSION" "-DBUILD_SHARED_LIBS=OFF -DFMT_DOC=OFF -DFMT_TEST=OFF"

fetch_and_extract "$EBUR128_URL" "libebur128-$EBUR128_VERSION"
cmake_build "$WORKDIR/src/libebur128-$EBUR128_VERSION" "-DBUILD_SHARED_LIBS=OFF"

echo "==> Cloning taglib v${TAGLIB_VERSION} with submodules"
rm -rf "$WORKDIR/src/taglib-$TAGLIB_VERSION"
git clone --depth 1 --branch v${TAGLIB_VERSION} --recurse-submodules https://github.com/taglib/taglib.git "$WORKDIR/src/taglib-$TAGLIB_VERSION"
git -C "$WORKDIR/src/taglib-$TAGLIB_VERSION" submodule update --init --recursive
cmake_build "$WORKDIR/src/taglib-$TAGLIB_VERSION" "-DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_BINDINGS=OFF"

fetch_and_extract "$FFMPEG_URL" "ffmpeg-$FFMPEG_VERSION"
FFSRC="$WORKDIR/src/ffmpeg-$FFMPEG_VERSION"
echo "==> Configuring FFmpeg $FFMPEG_VERSION"
cd "$FFSRC"
./configure   --target-os=android   --arch=aarch64   --cpu=armv8-a   --enable-cross-compile   --cross-prefix="$LLVM_BIN/$TRIPLE-"   --sysroot="$SYSROOT"   --cc="$CC"   --cxx="$CXX"   --ar="$AR"   --ranlib="$RANLIB"   --strip="$STRIP"   --prefix="$PREFIX"   --pkg-config-flags="--static"   --extra-cflags="-fPIC -O2 -I$PREFIX/include"   --extra-ldflags="-L$PREFIX/lib"   --disable-everything   --disable-doc   --disable-avdevice   --disable-swscale   --disable-postproc   --disable-avfilter   --disable-network   --disable-programs   --disable-symver   --enable-small   --enable-avformat   --enable-avcodec   --enable-avutil   --enable-swresample   --enable-decoder=aac,alac,flac,mp3,opus,vorbis,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le,pcm_u8,wavpack,ape,mpc7,mpc8,tak,tta,dsd_lsbf,dsd_msbf,wmav1,wmav2   --enable-demuxer=aac,flac,mov,mp4,m4a,3gp,mp3,ogg,opus,wav,aiff,ape,wv,asf,dsf,mpc,tta,tak   --enable-parser=aac,aac_latm,flac,mpegaudio,opus,vorbis   --enable-protocol=file
make -j"$JOBS"
make install
cd -

for _a in avformat avcodec avutil swresample; do
  if ! ls "$PREFIX/lib/lib${_a}.a" >/dev/null 2>&1 && ! ls "$PREFIX/lib64/lib${_a}.a" >/dev/null 2>&1; then
    echo "ERROR: lib${_a}.a not found under $PREFIX/lib after FFmpeg build"
    ls -lh "$PREFIX/lib" || true
    exit 1
  fi
done

echo "==> All dependencies installed to $PREFIX"
ls -lh "$PREFIX/lib" | head -n 50
echo "Done."
