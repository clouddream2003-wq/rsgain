#include <jni.h>
#include <thread>
#include <vector>
// include rsgain scan.hpp, easymode.hpp here, do not reimplement

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_himig_offline_RgNative_scanFds(
  JNIEnv* env, jobject, jintArray fds, jint numThreads) {
  // 1. Get FDs from Java (from MediaStore, Android 10+ safe)
  // 2. Create thread pool sized to numThreads (massive multithreading)
  // 3. Call rsgain::ScanJob::Track::scan() per FD, streaming, no full load
  // 4. Return float[] { trackGain, peak, loudness } per file
  // No Java fallback, pure native decoding for speed
}
