#include <jni.h>
#include <string>
#include <vector>
#include "ebur128.h"

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_rsgain_RsGain_version(JNIEnv* env, jclass) {
    std::string v = "rsgain-jni 1.1 ebur128";
    return env->NewStringUTF(v.c_str());
}

// One-shot for testing: pass whole PCM
JNIEXPORT jdouble JNICALL
Java_com_rsgain_RsGain_analyzePcm(JNIEnv* env, jclass,
    jshortArray pcmData, jint sampleRate, jint channels) {
    jsize len = env->GetArrayLength(pcmData);
    jshort* data = env->GetShortArrayElements(pcmData, nullptr);
    if (!data) return -99.0;

    ebur128_state* st = ebur128_init((unsigned int)channels,
                                     (unsigned long)sampleRate,
                                     EBUR128_MODE_I);
    if (!st) {
        env->ReleaseShortArrayElements(pcmData, data, JNI_ABORT);
        return -99.0;
    }

    size_t frames = (size_t)(len / channels);
    // process in chunks to avoid huge stack
    size_t offset = 0;
    const size_t CHUNK_FRAMES = 4096;
    while (offset < frames) {
        size_t todo = frames - offset;
        if (todo > CHUNK_FRAMES) todo = CHUNK_FRAMES;
        ebur128_add_frames_short(st, data + offset * channels, todo);
        offset += todo;
    }

    double loudness = -99.0;
    ebur128_loudness_global(st, &loudness);
    ebur128_destroy(&st);
    env->ReleaseShortArrayElements(pcmData, data, JNI_ABORT);
    return loudness;
}

// Streaming API for fast UI like Termux (recommended for 25 tracks)
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
    jshort* data = env->GetShortArrayElements(chunk, nullptr);
    if (!data) return;
    ebur128_add_frames_short(st, data, (size_t)frames);
    env->ReleaseShortArrayElements(chunk, data, JNI_ABORT);
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

// Keep old stub so old Sketchware blocks don't crash
JNIEXPORT jfloat JNICALL
Java_com_rsgain_RsGain_scanTrack(JNIEnv* env, jclass, jstring path) {
    return 0.0f;
}

} // extern C
2
Replace jni/CMakeLists.txt
Same: Edit → delete all → paste → Commit
cmake_minimum_required(VERSION 3.22)
project(rgain_jni)

# Find ebur128 sources - works whether submodule is at root or subfolder
file(GLOB EBUR128_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/../submodules/ebur128/*.c
    ${CMAKE_CURRE
