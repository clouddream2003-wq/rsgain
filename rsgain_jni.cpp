#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <string>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <ebur128.h>
}
#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>
#include <taglib/filestream.h>

static void proc_path_from_fd(int fd, char* out, size_t outsz) {
    snprintf(out, outsz, "/proc/self/fd/%d", fd);
}

extern "C" JNIEXPORT jfloatArray JNICALL Java_com_himig_offline_RgNative_scanFd(JNIEnv* env, jclass, jint jfd) {
    int fd = dup(jfd);
    if (fd < 0) return nullptr;
    lseek(fd, 0, SEEK_SET);
    char procPath[64];
    proc_path_from_fd(fd, procPath, sizeof(procPath));
    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, procPath, nullptr, nullptr) < 0) {
        close(fd);
        return nullptr;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        close(fd);
        return nullptr;
    }
    int streamIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIdx < 0) {
        avformat_close_input(&fmtCtx);
        close(fd);
        return nullptr;
    }
    AVStream* st = fmtCtx->streams[streamIdx];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        close(fd);
        return nullptr;
    }
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, st->codecpar);
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        close(fd);
        return nullptr;
    }
    SwrContext* swr = swr_alloc();
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    av_opt_set_chlayout(swr, "in_chlayout", &codecCtx->ch_layout, 0);
    av_opt_set_chlayout(swr, "out_chlayout", &outLayout, 0);
    av_opt_set_int(swr, "in_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate", 48000, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", codecCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
    swr_init(swr);
    ebur128_state* r128 = ebur128_init(2, 48000, EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* outFrame = av_frame_alloc();
    outFrame->format = AV_SAMPLE_FMT_FLT;
    outFrame->ch_layout = outLayout;
    outFrame->sample_rate = 48000;
    double peak = 0.0;
    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == streamIdx) {
            if (avcodec_send_packet(codecCtx, pkt) == 0) {
                while (avcodec_receive_frame(codecCtx, frame) == 0) {
                    outFrame->nb_samples = av_rescale_rnd(swr_get_delay(swr, codecCtx->sample_rate) + frame->nb_samples, 48000, codecCtx->sample_rate, AV_ROUND_UP);
                    av_frame_get_buffer(outFrame, 0);
                    int got = swr_convert(swr, outFrame->data, outFrame->nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
                    if (got > 0) {
                        float* pcm = (float*)outFrame->data[0];
                        ebur128_add_frames_float(r128, pcm, (size_t)got);
                    }
                    av_frame_unref(outFrame);
                }
            }
        }
        av_packet_unref(pkt);
    }
    avcodec_send_packet(codecCtx, nullptr);
    while (avcodec_receive_frame(codecCtx, frame) == 0) {
        outFrame->nb_samples = av_rescale_rnd(swr_get_delay(swr, codecCtx->sample_rate) + frame->nb_samples, 48000, codecCtx->sample_rate, AV_ROUND_UP);
        av_frame_get_buffer(outFrame, 0);
        int got = swr_convert(swr, outFrame->data, outFrame->nb_samples, (const uint8_t**)frame->data, frame->nb_samples);
        if (got > 0) {
            float* pcm = (float*)outFrame->data[0];
            ebur128_add_frames_float(r128, pcm, (size_t)got);
        }
        av_frame_unref(outFrame);
    }
    double loudness = -70.0;
    ebur128_loudness_global(r128, &loudness);
    double truePeak = 0.0;
    ebur128_true_peak(r128, 0, &truePeak);
    double truePeak2 = 0.0;
    ebur128_true_peak(r128, 1, &truePeak2);
    if (truePeak2 > truePeak) truePeak = truePeak2;
    if (truePeak < 0.00001) truePeak = 0.00001;
    if (loudness < -69.0) loudness = -70.0;
    double target = -18.0;
    double gain = target - loudness;
    if (gain > 20.0) gain = 20.0;
    if (gain < -20.0) gain = -20.0;
    ebur128_destroy(&r128);
    swr_free(&swr);
    av_frame_free(&frame);
    av_frame_free(&outFrame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    close(fd);
    jfloatArray out = env->NewFloatArray(3);
    jfloat vals[3];
    vals[0] = (jfloat)gain;
    vals[1] = (jfloat)truePeak;
    vals[2] = (jfloat)loudness;
    env->SetFloatArrayRegion(out, 0, 3, vals);
    return out;
}

extern "C" JNIEXPORT jint JNICALL Java_com_himig_offline_RgNative_writeTagsFd(JNIEnv* env, jclass, jint jfd, jfloat trackGain, jfloat trackPeak, jfloat albumGain, jfloat albumPeak) {
    int fd = dup(jfd);
    if (fd < 0) return -1;
    char procPath[64];
    proc_path_from_fd(fd, procPath, sizeof(procPath));
    TagLib::FileRef f(procPath);
    if (f.isNull()) {
        close(fd);
        return -2;
    }
    TagLib::PropertyMap props = f.file()->properties();
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f dB", (double)trackGain);
    props.replace("REPLAYGAIN_TRACK_GAIN", TagLib::StringList(TagLib::String(buf)));
    snprintf(buf, sizeof(buf), "%.6f", (double)trackPeak);
    props.replace("REPLAYGAIN_TRACK_PEAK", TagLib::StringList(TagLib::String(buf)));
    snprintf(buf, sizeof(buf), "%.2f dB", (double)albumGain);
    props.replace("REPLAYGAIN_ALBUM_GAIN", TagLib::StringList(TagLib::String(buf)));
    snprintf(buf, sizeof(buf), "%.6f", (double)albumPeak);
    props.replace("REPLAYGAIN_ALBUM_PEAK", TagLib::StringList(TagLib::String(buf)));
    props.replace("REPLAYGAIN_REFERENCE_LOUDNESS", TagLib::StringList(TagLib::String("-18.00 LUFS")));
    f.file()->setProperties(props);
    bool ok = f.save();
    close(fd);
    return ok? 0 : -3;
}
