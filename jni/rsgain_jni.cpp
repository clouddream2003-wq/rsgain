#include <cstdint>
#include <jni.h>
#include <string>
#include <vector>
#include <cmath>
#include "ebur128.h"

extern "C" {

// NOTE: package is com.himig.offline to match RsGain.java
// If you move RsGain.java, update these names accordingly.

JNIEXPORT jstring JNICALL
Java_com_himig_offline_RsGain_version(JNIEnv* env, jclass) {
    std::string v = "rsgain-jni 1.1 ebur128";
    return env->NewStringUTF(v.c_str());
}

JNIEXPORT jdouble JNICALL
Java_com_himig_offline_RsGain_analyzePcm(JNIEnv* env, jclass,
    jshortArray pcmData, jint sampleRate, jint channels) {
    if (!pcmData) return -99.0;
    if (channels <= 0 || sampleRate <= 0) return -99.0;
    jsize len = env->GetArrayLength(pcmData);
    if (len <= 0) return -99.0;
    jshort* data = (jshort*)env->GetPrimitiveArrayCritical(pcmData, nullptr);
    if (!data) return -99.0;
    ebur128_state* st = ebur128_init((unsigned int)channels,
                                     (unsigned long)sampleRate,
                                     EBUR128_MODE_I);
    if (!st) {
        env->ReleasePrimitiveArrayCritical(pcmData, data, JNI_ABORT);
        return -99.0;
    }
    size_t frames = (size_t)(len / channels);
    size_t offset = 0;
    const size_t CHUNK_FRAMES = 16384;
    while (offset < frames) {
        size_t todo = frames - offset;
        if (todo > CHUNK_FRAMES) todo = CHUNK_FRAMES;
        ebur128_add_frames_short(st, data + offset * channels, todo);
        offset += todo;
    }
    double loudness = -99.0;
    ebur128_loudness_global(st, &loudness);
    ebur128_destroy(&st);
    env->ReleasePrimitiveArrayCritical(pcmData, data, JNI_ABORT);
    return loudness;
}

JNIEXPORT jlong JNICALL
Java_com_himig_offline_RsGain_nativeInit(JNIEnv* env, jclass, jint channels, jint sampleRate) {
    if (channels <= 0 || channels > 8) return 0;
    if (sampleRate <= 0 || sampleRate > 192000) return 0;
    ebur128_state* st = ebur128_init((unsigned int)channels,
                                     (unsigned long)sampleRate,
                                     EBUR128_MODE_I);
    return (jlong)(intptr_t)st;
}

JNIEXPORT void JNICALL
Java_com_himig_offline_RsGain_nativeAddFramesShort(JNIEnv* env, jclass,
    jlong handle, jshortArray chunk, jint frames) {
    ebur128_state* st = (ebur128_state*)(intptr_t)handle;
    if (!st || !chunk || frames <= 0) return;
    jshort* data = (jshort*)env->GetPrimitiveArrayCritical(chunk, nullptr);
    if (!data) return;
    ebur128_add_frames_short(st, data, (size_t)frames);
    env->ReleasePrimitiveArrayCritical(chunk, data, JNI_ABORT);
}

// Fast: add + peak in one native pass
JNIEXPORT jdouble JNICALL
Java_com_himig_offline_RsGain_nativeAddFramesShortWithPeak(JNIEnv* env, jclass,
    jlong handle, jshortArray chunk, jint frames) {
    ebur128_state* st = (ebur128_state*)(intptr_t)handle;
    if (!chunk || frames <= 0) return 0.0;
    jsize len = env->GetArrayLength(chunk);
    if (len <= 0) return 0.0;
    jshort* data = (jshort*)env->GetPrimitiveArrayCritical(chunk, nullptr);
    if (!data) return 0.0;
    double peak = 0.0;
    for (int i = 0; i < len; i++) {
        double a = fabs((double)data[i] / 32768.0);
        if (a > peak) peak = a;
    }
    if (st) {
        ebur128_add_frames_short(st, data, (size_t)frames);
    }
    env->ReleasePrimitiveArrayCritical(chunk, data, JNI_ABORT);
    return peak;
}

JNIEXPORT jdouble JNICALL
Java_com_himig_offline_RsGain_nativeGetLoudness(JNIEnv* env, jclass, jlong handle) {
    ebur128_state* st = (ebur128_state*)(intptr_t)handle;
    if (!st) return -99.0;
    double l = -99.0;
    ebur128_loudness_global(st, &l);
    return l;
}

JNIEXPORT void JNICALL
Java_com_himig_offline_RsGain_nativeDestroy(JNIEnv* env, jclass, jlong handle) {
    ebur128_state* st = (ebur128_state*)(intptr_t)handle;
    if (!st) return;
    ebur128_destroy(&st);
}

} // extern C
