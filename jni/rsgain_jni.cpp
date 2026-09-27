#include <jni.h>
#include <string>

extern "C"
JNIEXPORT jstring JNICALL
Java_com_rsgain_RsGain_version(JNIEnv* env, jclass) {
    std::string v = "rsgain-jni 1.0 test build";
    return env->NewStringUTF(v.c_str());
}

extern "C"
JNIEXPORT jfloat JNICALL
Java_com_rsgain_RsGain_scanTrack(JNIEnv* env, jclass, jstring path) {
    // TODO: call real rsgain scan after FFmpeg is added
    return 0.0f;
}
