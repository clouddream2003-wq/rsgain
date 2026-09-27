#include <cstdint>
#include <jni.h>
#include <string>
#include <vector>
#include <cmath>
#include <unistd.h>
#include <android/log.h>
#include "ebur128.h"

// NDK media - native decoding (Termux-like speed, no Java copies)
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaError.h>

#define LOG_TAG "rsgain_jni"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {

// --- existing ebur128 helpers (package com.himig.offline) ---

JNIEXPORT jstring JNICALL
Java_com_himig_offline_RsGain_version(JNIEnv* env, jclass) {
    std::string v = "rsgain-jni 1.2 ebur128+ndk";
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

// --- NATIVE DECODING: single JNI call per file, Termux-like speed ---
// Uses AMediaExtractor + AMediaCodec entirely in native, feeds directly to ebur128.
// No Java short[] allocation, no GetPrimitiveArrayCritical per chunk.

JNIEXPORT jdoubleArray JNICALL
Java_com_himig_offline_RsGain_scanFileFd(JNIEnv* env, jclass, jint fd, jlong offset, jlong length) {
    if (fd < 0) return nullptr;

    AMediaExtractor* ex = AMediaExtractor_new();
    if (!ex) return nullptr;

    media_status_t st = AMediaExtractor_setDataSourceFd(ex, fd, (off64_t)offset, (off64_t)length);
    if (st != AMEDIA_OK) {
        AMediaExtractor_delete(ex);
        return nullptr;
    }

    size_t numTracks = AMediaExtractor_getTrackCount(ex);
    size_t audioTrack = (size_t)-1;
    AMediaFormat* trackFormat = nullptr;
    const char* mime = nullptr;

    for (size_t i = 0; i < numTracks; i++) {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, i);
        if (!fmt) continue;
        const char* m = nullptr;
        if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m) && m) {
            // check audio/
            if (m[0]=='a' && m[1]=='u' && m[2]=='d' && m[3]=='i' && m[4]=='o' && m[5]=='/') {
                audioTrack = i;
                trackFormat = fmt;
                mime = m;
                break;
            }
        }
        AMediaFormat_delete(fmt);
    }

    if (audioTrack == (size_t)-1 || !trackFormat) {
        AMediaExtractor_delete(ex);
        return nullptr;
    }

    int32_t sampleRate = 44100;
    int32_t channelCount = 2;
    AMediaFormat_getInt32(trackFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
    AMediaFormat_getInt32(trackFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channelCount);
    if (channelCount <= 0) channelCount = 2;
    if (sampleRate <= 0) sampleRate = 44100;

    // Keep mime copy because trackFormat will be deleted after codec creation
    std::string mimeStr = mime ? mime : "audio/mpeg";

    AMediaExtractor_selectTrack(ex, audioTrack);

    AMediaCodec* codec = AMediaCodec_createDecoderByType(mimeStr.c_str());
    if (!codec) {
        AMediaFormat_delete(trackFormat);
        AMediaExtractor_delete(ex);
        return nullptr;
    }

    media_status_t cfg = AMediaCodec_configure(codec, trackFormat, nullptr, nullptr, 0);
    AMediaFormat_delete(trackFormat);
    if (cfg != AMEDIA_OK) {
        AMediaCodec_delete(codec);
        AMediaExtractor_delete(ex);
        return nullptr;
    }

    if (AMediaCodec_start(codec) != AMEDIA_OK) {
        AMediaCodec_delete(codec);
        AMediaExtractor_delete(ex);
        return nullptr;
    }

    ebur128_state* ebur = ebur128_init((unsigned int)channelCount, (unsigned long)sampleRate, EBUR128_MODE_I);
    if (!ebur) {
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        AMediaExtractor_delete(ex);
        return nullptr;
    }

    double peak = 0.0;
    bool sawInputEos = false;
    bool sawOutputEos = false;
    const long TIMEOUT_US = 10000;

    // input loop
    while (!sawOutputEos) {
        if (!sawInputEos) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, TIMEOUT_US);
            if (inIdx >= 0) {
                size_t inSize = 0;
                uint8_t* inBuf = AMediaCodec_getInputBuffer(codec, (size_t)inIdx, &inSize);
                ssize_t sampleSize = 0;
                if (inBuf && inSize > 0) {
                    sampleSize = AMediaExtractor_readSampleData(ex, inBuf, inSize);
                } else {
                    // fallback: query size first
                    sampleSize = AMediaExtractor_getSampleSize(ex);
                    // if we can't get buffer, treat as EOS to avoid infinite loop
                    if (sampleSize < 0) sampleSize = -1;
                }

                if (sampleSize < 0) {
                    AMediaCodec_queueInputBuffer(codec, (size_t)inIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    sawInputEos = true;
                } else {
                    int64_t pts = AMediaExtractor_getSampleTime(ex);
                    AMediaCodec_queueInputBuffer(codec, (size_t)inIdx, 0, (size_t)sampleSize, (uint64_t)pts, 0);
                    AMediaExtractor_advance(ex);
                }
            }
        }

        AMediaCodecBufferInfo info;
        ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, TIMEOUT_US);
        if (outIdx >= 0) {
            if (info.size > 0) {
                size_t outSize = 0;
                uint8_t* outBuf = AMediaCodec_getOutputBuffer(codec, (size_t)outIdx, &outSize);
                if (outBuf && info.size > 0) {
                    // PCM16LE assumed (MediaCodec audio decoders output PCM16)
                    size_t numShorts = (size_t)info.size / 2;
                    int16_t* shorts = (int16_t*)(outBuf + info.offset);
                    // peak
                    for (size_t i = 0; i < numShorts; i++) {
                        double a = fabs((double)shorts[i] / 32768.0);
                        if (a > peak) peak = a;
                    }
                    size_t frames = numShorts / (size_t)channelCount;
                    if (frames > 0) {
                        ebur128_add_frames_short(ebur, shorts, frames);
                    }
                }
            }
            AMediaCodec_releaseOutputBuffer(codec, (size_t)outIdx, false);
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
                sawOutputEos = true;
            }
        } else if (outIdx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            if (sawInputEos) {
                // avoid spin: small sleep via timeout already, continue
            }
            continue;
        }
        // ignore other info codes (format changed etc.)
    }

    double lufs = -99.0;
    ebur128_loudness_global(ebur, &lufs);
    ebur128_destroy(&ebur);

    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    AMediaExtractor_delete(ex);

    jdoubleArray ret = env->NewDoubleArray(2);
    if (!ret) return nullptr;
    jdouble vals[2];
    vals[0] = lufs;
    vals[1] = peak;
    env->SetDoubleArrayRegion(ret, 0, 2, vals);
    return ret;
}

} // extern C
