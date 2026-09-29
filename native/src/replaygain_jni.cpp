#include <jni.h>
#include <thread>
#include "ebur128.h"
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>

// rsgain default: -18 LUFS reference, keep same
#define RG_REFERENCE_LUFS -18.0

extern "C"
JNIEXPORT jdoubleArray JNICALL
Java_com_himig_player_rg_RgNative_nativeScanFd(
    JNIEnv* env, jobject, jint fd, jlong offset, jlong length,
    jint sampleRate, jint channels) {

    ebur128_state* st = ebur128_init(
        (unsigned int)channels,
        (unsigned int)sampleRate,
        EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK
    );

    //... decode loop via AMediaExtractor_setDataSourceFd
    //... for each PCM float block:
    // ebur128_add_frames_float(st, pcm_float, frames_per_ch)

    double loudness = -70.0;
    ebur128_loudness_global(st, &loudness);
    double peak = 0.0;
    // get true peak per channel, take max (same as rsgain scan.cpp)
    for (unsigned int c = 0; c < (unsigned int)channels; ++c) {
        double cpeak = 0.0;
        ebur128_true_peak(st, c, &cpeak);
        if (cpeak > peak) peak = cpeak;
    }
    double gain = RG_REFERENCE_LUFS - loudness;

    ebur128_destroy(&st);

    jdoubleArray ret = env->NewDoubleArray(3);
    double vals[3] = {loudness, peak, gain};
    env->SetDoubleArrayRegion(ret, 0, 3, vals);
    return ret;
}
