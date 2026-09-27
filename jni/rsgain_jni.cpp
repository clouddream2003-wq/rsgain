#include <cstdint>
#include <jni.h>
#include <string>
#include <vector>
#include <cmath>
#include "ebur128.h"

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_rsgain_RsGain_version(JNIEnv* env, jclass) {
    std::string v = "rsgain-jni 1.1 ebur128";
    return env->NewStringUTF(v.c_str());
}

JNIEXPORT jdouble JNICALL
Java_com_rsgain_RsGain_analyzePcm(JNIEnv* env, jclass,
    jshortArray pcmData, jint sampleRate, jint channels) {
    jsize len = env->GetArrayLength(pcmData);
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
Java_com_rsgain_RsGain_nativeInit(JNIEnv* env, jclass, jint channels, jint sampleRate) {
    ebur128_state* st = ebur128_init((unsigned int)channels,
                                     (unsigned long)sampleRate,
                                     EBUR128_MODE_I);
    return (jlong)(intptr_t)st;
}

JNIEXPORT void JNICALL
Java_com_rsgain_RsGain_nativeAddFramesShort(JNIEnv* env, jclass,
    jlong handle, jshortArray chunk, jint frames) {
    ebur128_state* st = (ebur128_state*)(intptr_t)handle;
    if (!st || !chunk) return;
    jshort* data = (jshort*)env->GetPrimitiveArrayCritical(chunk, nullptr);
    if (!data) return;
    ebur128_add_frames_short(st, data, (size_t)frames);
    env->ReleasePrimitiveArrayCritical(chunk, data, JNI_ABORT);
}

// Fast: add + peak in one native pass (avoids Java loop over every sample)
JNIEXPORT jdouble JNICALL
Java_com_rsgain_RsGain_nativeAddFramesShortWithPeak(JNIEnv* env, jclass,
    jlong handle, jshortArray chunk, jint frames) {
    ebur128_state* st = (ebur128_state*)(intptr_t)handle;
    if (!chunk) return 0.0;
    jsize len = env->GetArrayLength(chunk);
    jshort* data = (jshort*)env->GetPrimitiveArrayCritical(chunk, nullptr);
    if (!data) return 0.0;
    double peak = 0.0;
    // compute peak
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
Java_com_rsgain_RsGain_nativeGetLoudness(JNIEnv* env, jclass, jlong handle) {
    ebur128_state* st = (ebur128_state*)(intptr_t)handle;
    if (!st) return -99.0;
    double l = -99.0;
    ebur128_loudness_global(st, &l);
    return l;
}

JNIEXPORT void JNICALL
Java_com_rsgain_RsGain_nativeDestroy(JNIEnv* env, jclass, jlong handle) {
    ebur128_state* st = (ebur128_state*)(intptr_t)handle;
    if (!st) return;
    ebur128_destroy(&st);
}

JNIEXPORT jfloat JNICALL
Java_com_rsgain_RsGain_scanTrack(JNIEnv* env, jclass, jstring path) {
    return 0.0f;
}

} // extern C
