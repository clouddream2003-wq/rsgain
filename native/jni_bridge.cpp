#include <jni.h>
#include <cstddef>
#include <cstdlib>
#include <ebur128.h>

struct ScannerHandle {
    ebur128_state* st;
    int channels;
    int sampleRate;
};

extern "C" JNIEXPORT jlong JNICALL
Java_com_himig_offline_RgNative_createScanner(JNIEnv* env, jclass, jint channels, jint sampleRate) {
    ScannerHandle* h = new ScannerHandle();
    h->channels = channels;
    h->sampleRate = sampleRate;
    h->st = ebur128_init((unsigned int)channels, (unsigned int)sampleRate, EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK);
    if (!h->st) {
        delete h;
        return 0;
    }
    return reinterpret_cast<jlong>(h);
}

extern "C" JNIEXPORT void JNICALL
Java_com_himig_offline_RgNative_feedScanner(JNIEnv* env, jclass, jlong handle, jfloatArray pcmArray, jint frames) {
    ScannerHandle* h = reinterpret_cast<ScannerHandle*>(handle);
    if (!h || !h->st) return;
    jfloat* data = env->GetFloatArrayElements(pcmArray, nullptr);
    if (!data) return;
    ebur128_add_frames_float(h->st, data, (size_t)frames);
    env->ReleaseFloatArrayElements(pcmArray, data, JNI_ABORT);
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_himig_offline_RgNative_getResult(JNIEnv* env, jclass, jlong handle) {
    ScannerHandle* h = reinterpret_cast<ScannerHandle*>(handle);
    jfloatArray out = env->NewFloatArray(4);
    if (!h || !h->st) return out;
    double lufs = -70.0;
    ebur128_loudness_global(h->st, &lufs);
    double peak = 0.0;
    for (unsigned int c = 0; c < (unsigned int)h->channels; ++c) {
        double cp = 0.0;
        ebur128_true_peak(h->st, c, &cp);
        if (cp > peak) peak = cp;
    }
    double gain = -18.0 - lufs;
    jfloat vals[4];
    vals[0] = (jfloat)lufs;
    vals[1] = (jfloat)peak;
    vals[2] = (jfloat)gain;
    vals[3] = (jfloat)h->sampleRate;
    env->SetFloatArrayRegion(out, 0, 4, vals);
    return out;
}

extern "C" JNIEXPORT void JNICALL
Java_com_himig_offline_RgNative_destroyScanner(JNIEnv* env, jclass, jlong handle) {
    ScannerHandle* h = reinterpret_cast<ScannerHandle*>(handle);
    if (!h) return;
    if (h->st) ebur128_destroy(&h->st);
    delete h;
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_himig_offline_RgNative_scanFds(JNIEnv* env, jclass, jintArray fds, jint numThreads) {
    jfloatArray out = env->NewFloatArray(0);
    return out;
}
