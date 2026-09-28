#include <cstdint>
#include <jni.h>
#include <string>
#include <vector>
#include <cmath>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include "ebur128.h"

#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaError.h>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}
#endif

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_himig_offline_RsGain_version(JNIEnv* env, jclass) {
#ifdef HAS_FFMPEG
    return env->NewStringUTF("rsgain-jni 2.0 ffmpeg+ebur128 (Termux pipeline)");
#else
    return env->NewStringUTF("rsgain-jni 1.3 ndk-opt");
#endif
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
    ebur128_state* st = ebur128_init((unsigned int)channels, (unsigned long)sampleRate, EBUR128_MODE_I);
    if (!st) { env->ReleasePrimitiveArrayCritical(pcmData, data, JNI_ABORT); return -99.0; }
    size_t frames = (size_t)(len / channels);
    ebur128_add_frames_short(st, data, frames);
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
    ebur128_state* st = ebur128_init((unsigned int)channels, (unsigned long)sampleRate, EBUR128_MODE_I);
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
    int peakAbs = 0;
    for (int i = 0; i < len; i++) {
        int v = data[i] < 0 ? -data[i] : data[i];
        if (v > peakAbs) peakAbs = v;
    }
    if (st) ebur128_add_frames_short(st, data, (size_t)frames);
    env->ReleasePrimitiveArrayCritical(chunk, data, JNI_ABORT);
    return (double)peakAbs / 32768.0;
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

#ifdef HAS_FFMPEG
// --- True rsgain scan.cpp pipeline via FFmpeg ---
struct FdIOContext {
    int fd;
    int64_t base_offset;
    int64_t file_size;
    int64_t pos;
};

static int fdio_read(void* opaque, uint8_t* buf, int buf_size) {
    FdIOContext* c = (FdIOContext*)opaque;
    ssize_t r = pread(c->fd, buf, buf_size, c->base_offset + c->pos);
    if (r <= 0) return AVERROR_EOF;
    c->pos += r;
    return (int)r;
}
static int64_t fdio_seek(void* opaque, int64_t offset, int whence) {
    FdIOContext* c = (FdIOContext*)opaque;
    int64_t new_pos = -1;
    if (whence == AVSEEK_SIZE) return c->file_size;
    else if (whence == SEEK_SET) new_pos = offset;
    else if (whence == SEEK_CUR) new_pos = c->pos + offset;
    else if (whence == SEEK_END) new_pos = c->file_size + offset;
    if (new_pos < 0 || new_pos > c->file_size) return -1;
    c->pos = new_pos;
    return c->pos;
}

static jdoubleArray scan_with_ffmpeg(JNIEnv* env, int fd, int64_t offset, int64_t length) {
    // dup fd so FFmpeg doesn't close the original
    int dupfd = dup(fd);
    if (dupfd < 0) return nullptr;

    FdIOContext* ioCtx = new FdIOContext{dupfd, offset, length, 0};
    const int avioSize = 4096;
    uint8_t* avioBuf = (uint8_t*)av_malloc(avioSize);
    if (!avioBuf) { close(dupfd); delete ioCtx; return nullptr; }

    AVIOContext* avio = avio_alloc_context(avioBuf, avioSize, 0, ioCtx, fdio_read, nullptr, fdio_seek);
    if (!avio) { av_free(avioBuf); close(dupfd); delete ioCtx; return nullptr; }

    AVFormatContext* fmt = avformat_alloc_context();
    fmt->pb = avio;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&fmt, "fd", nullptr, nullptr) < 0) {
        avio_context_free(&avio); close(dupfd); delete ioCtx;
        return nullptr;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt); // frees avio
        close(dupfd); delete ioCtx;
        return nullptr;
    }

    int stream_idx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_idx < 0) { avformat_close_input(&fmt); close(dupfd); delete ioCtx; return nullptr; }
    AVStream* stream = fmt->streams[stream_idx];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) { avformat_close_input(&fmt); close(dupfd); delete ioCtx; return nullptr; }

    AVCodecContext* cctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cctx, stream->codecpar);
    // rsgain: enable multithreaded decoding like Termux
    cctx->thread_count = 0;
    if (avcodec_open2(cctx, codec, nullptr) < 0) {
        avcodec_free_context(&cctx); avformat_close_input(&fmt); close(dupfd); delete ioCtx;
        return nullptr;
    }

    int sampleRate = cctx->sample_rate ? cctx->sample_rate : 44100;
    int channels = cctx->channels ? cctx->channels : 2;
    // rsgain scan.cpp: determine_output_format -> we force S16 like rsgain does for ebur128
    SwrContext* swr = swr_alloc_set_opts(nullptr,
        av_get_default_channel_layout(channels), AV_SAMPLE_FMT_S16, sampleRate,
        cctx->channel_layout ? cctx->channel_layout : av_get_default_channel_layout(cctx->channels),
        cctx->sample_fmt, cctx->sample_rate,
        0, nullptr);
    swr_init(swr);

    ebur128_state* ebur = ebur128_init((unsigned)channels, (unsigned long)sampleRate, EBUR128_MODE_I);
    if (!ebur) {
        swr_free(&swr); avcodec_free_context(&cctx); avformat_close_input(&fmt); close(dupfd); delete ioCtx;
        return nullptr;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int peakAbs = 0;

    // rsgain scan loop: av_read_frame -> send_packet -> receive_frame -> swr_convert -> ebur128
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == stream_idx) {
            if (avcodec_send_packet(cctx, pkt) == 0) {
                while (avcodec_receive_frame(cctx, frame) == 0) {
                    // resample to S16
                    int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr, cctx->sample_rate) + frame->nb_samples,
                        sampleRate, cctx->sample_rate, AV_ROUND_UP);
                    int16_t* dst = (int16_t*)av_malloc(dst_nb_samples * channels * sizeof(int16_t));
                    uint8_t* out[1] = {(uint8_t*)dst};
                    int converted = swr_convert(swr, out, dst_nb_samples,
                        (const uint8_t**)frame->data, frame->nb_samples);
                    if (converted > 0) {
                        int total = converted * channels;
                        for (int i = 0; i < total; i++) {
                            int v = dst[i] < 0 ? -dst[i] : dst[i];
                            if (v > peakAbs) peakAbs = v;
                        }
                        ebur128_add_frames_short(ebur, dst, converted);
                    }
                    av_free(dst);
                }
            }
        }
        av_packet_unref(pkt);
    }
    // flush decoder
    avcodec_send_packet(cctx, nullptr);
    while (avcodec_receive_frame(cctx, frame) == 0) {
        int dst_nb_samples = av_rescale_rnd(swr_get_delay(swr, cctx->sample_rate) + frame->nb_samples,
            sampleRate, cctx->sample_rate, AV_ROUND_UP);
        int16_t* dst = (int16_t*)av_malloc(dst_nb_samples * channels * sizeof(int16_t));
        uint8_t* out[1] = {(uint8_t*)dst};
        int converted = swr_convert(swr, out, dst_nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
        if (converted > 0) {
            int total = converted * channels;
            for (int i = 0; i < total; i++) {
                int v = dst[i] < 0 ? -dst[i] : dst[i];
                if (v > peakAbs) peakAbs = v;
            }
            ebur128_add_frames_short(ebur, dst, converted);
        }
        av_free(dst);
    }

    double lufs = -99.0;
    ebur128_loudness_global(ebur, &lufs);
    ebur128_destroy(&ebur);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&cctx);
    avformat_close_input(&fmt); // frees avio
    close(dupfd);
    delete ioCtx;

    jdoubleArray ret = env->NewDoubleArray(2);
    if (!ret) return nullptr;
    jdouble vals[2] = { lufs, (double)peakAbs / 32768.0 };
    env->SetDoubleArrayRegion(ret, 0, 2, vals);
    return ret;
}
#endif

// NDK fallback (same as before, optimized)
static jdoubleArray scan_with_ndk(JNIEnv* env, int fd, int64_t offset, int64_t length) {
    if (fd < 0) return nullptr;
    AMediaExtractor* ex = AMediaExtractor_new();
    if (!ex) return nullptr;
    if (AMediaExtractor_setDataSourceFd(ex, fd, (off64_t)offset, (off64_t)length) != AMEDIA_OK) {
        AMediaExtractor_delete(ex); return nullptr;
    }
    size_t numTracks = AMediaExtractor_getTrackCount(ex);
    size_t audioTrack = (size_t)-1;
    AMediaFormat* trackFormat = nullptr;
    std::string mimeStr;
    for (size_t i = 0; i < numTracks; i++) {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(ex, i);
        if (!fmt) continue;
        const char* m = nullptr;
        if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &m) && m &&
            m[0]=='a' && m[1]=='u' && m[2]=='d' && m[3]=='i' && m[4]=='o' && m[5]=='/') {
            audioTrack = i; trackFormat = fmt; mimeStr = m; break;
        }
        AMediaFormat_delete(fmt);
    }
    if (audioTrack == (size_t)-1) { AMediaExtractor_delete(ex); return nullptr; }
    int32_t sampleRate = 44100, channelCount = 2;
    AMediaFormat_getInt32(trackFormat, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
    AMediaFormat_getInt32(trackFormat, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channelCount);
    if (channelCount <= 0) channelCount = 2;
    if (sampleRate <= 0) sampleRate = 44100;
    AMediaExtractor_selectTrack(ex, audioTrack);
    AMediaCodec* codec = AMediaCodec_createDecoderByType(mimeStr.c_str());
    if (!codec) { AMediaFormat_delete(trackFormat); AMediaExtractor_delete(ex); return nullptr; }
    if (AMediaCodec_configure(codec, trackFormat, nullptr, nullptr, 0) != AMEDIA_OK) {
        AMediaFormat_delete(trackFormat); AMediaCodec_delete(codec); AMediaExtractor_delete(ex); return nullptr;
    }
    AMediaFormat_delete(trackFormat);
    if (AMediaCodec_start(codec) != AMEDIA_OK) { AMediaCodec_delete(codec); AMediaExtractor_delete(ex); return nullptr; }
    ebur128_state* ebur = ebur128_init((unsigned)channelCount, (unsigned long)sampleRate, EBUR128_MODE_I);
    if (!ebur) { AMediaCodec_stop(codec); AMediaCodec_delete(codec); AMediaExtractor_delete(ex); return nullptr; }
    int peakAbs = 0;
    bool sawInputEos = false, sawOutputEos = false;
    const long TIMEOUT_US = 5000;
    while (!sawOutputEos) {
        if (!sawInputEos) {
            ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, TIMEOUT_US);
            if (inIdx >= 0) {
                size_t inSize = 0;
                uint8_t* inBuf = AMediaCodec_getInputBuffer(codec, (size_t)inIdx, &inSize);
                ssize_t sampleSize = -1;
                if (inBuf && inSize > 0) sampleSize = AMediaExtractor_readSampleData(ex, inBuf, inSize);
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
                if (outBuf) {
                    size_t numShorts = (size_t)info.size / 2;
                    int16_t* shorts = (int16_t*)(outBuf + info.offset);
                    for (size_t i = 0; i < numShorts; i++) {
                        int v = shorts[i] < 0 ? -shorts[i] : shorts[i];
                        if (v > peakAbs) peakAbs = v;
                    }
                    size_t frames = numShorts / (size_t)channelCount;
                    if (frames) ebur128_add_frames_short(ebur, shorts, frames);
                }
            }
            AMediaCodec_releaseOutputBuffer(codec, (size_t)outIdx, false);
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) sawOutputEos = true;
        }
    }
    double lufs = -99.0;
    ebur128_loudness_global(ebur, &lufs);
    ebur128_destroy(&ebur);
    AMediaCodec_stop(codec); AMediaCodec_delete(codec); AMediaExtractor_delete(ex);
    jdoubleArray ret = env->NewDoubleArray(2);
    if (!ret) return nullptr;
    jdouble vals[2] = { lufs, (double)peakAbs / 32768.0 };
    env->SetDoubleArrayRegion(ret, 0, 2, vals);
    return ret;
}

JNIEXPORT jdoubleArray JNICALL
Java_com_himig_offline_RsGain_scanFileFd(JNIEnv* env, jclass, jint fd, jlong offset, jlong length) {
#ifdef HAS_FFMPEG
    jdoubleArray r = scan_with_ffmpeg(env, fd, offset, length);
    if (r) return r;
    // fallback to NDK if FFmpeg fails
#endif
    return scan_with_ndk(env, fd, offset, length);
}

} // extern C
