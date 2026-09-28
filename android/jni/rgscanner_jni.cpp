#include <jni.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <android/log.h>
#include <libavutil/log.h>

#include "scan.hpp"
#include "rsgain.hpp"
#include "tag.hpp"
#include "output.hpp"

#include <taglib/fileref.h>
#include <taglib/xiphcomment.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/textidentificationframe.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mp4item.h>
#include <taglib/apefile.h>
#include <taglib/wavpackfile.h>
#include <taglib/mpcfile.h>
#include <taglib/apetag.h>
#include <taglib/apeitem.h>
#include <taglib/asffile.h>
#include <taglib/asftag.h>
#include <taglib/wavfile.h>
#include <taglib/aifffile.h>
#include <taglib/dsffile.h>

#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "RgScanner", __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RgScanner", __VA_ARGS__)

int quiet = 0;
bool multithread = false;

const Config& get_config(FileType) {
    static Config cfg{};
    return cfg;
}

static JavaVM* g_vm = nullptr;
static jobject g_listener = nullptr;
static jmethodID m_onAlbumStart = nullptr;
static jmethodID m_onFileProgress = nullptr;
static jmethodID m_onFileDone = nullptr;
static jmethodID m_onAlbumDone = nullptr;
static jmethodID m_onComplete = nullptr;
static jmethodID m_onError = nullptr;

thread_local std::string tls_label;

static JNIEnv* envForThread() {
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        g_vm->AttachCurrentThread(&env, nullptr);
    }
    return env;
}

static void clearEx(JNIEnv* e) {
    if (e->ExceptionCheck()) e->ExceptionClear();
}

static void postAlbumStartStatic(int a, int total, int files) {
    if (!g_listener) return;
    JNIEnv* e = envForThread();
    e->CallVoidMethod(g_listener, m_onAlbumStart, a, total, files);
    clearEx(e);
}

static void postFileProgressStatic(const std::string& label, int percent) {
    if (!g_listener) return;
    JNIEnv* e = envForThread();
    jstring jl = e->NewStringUTF(label.c_str());
    e->CallVoidMethod(g_listener, m_onFileProgress, jl, percent);
    e->DeleteLocalRef(jl);
    clearEx(e);
}

static void postFileDoneStatic(const std::string& path, const std::string& gain, const std::string& peak, bool written) {
    if (!g_listener) return;
    JNIEnv* e = envForThread();
    jstring jp = e->NewStringUTF(path.c_str());
    jstring jg = e->NewStringUTF(gain.c_str());
    jstring jk = e->NewStringUTF(peak.c_str());
    e->CallVoidMethod(g_listener, m_onFileDone, jp, jg, jk, (jboolean)written);
    e->DeleteLocalRef(jp);
    e->DeleteLocalRef(jg);
    e->DeleteLocalRef(jk);
    clearEx(e);
}

static void postAlbumDoneStatic(int a, int total) {
    if (!g_listener) return;
    JNIEnv* e = envForThread();
    e->CallVoidMethod(g_listener, m_onAlbumDone, a, total);
    clearEx(e);
}

static void postCompleteStatic(int ok, int fail, int skip) {
    if (!g_listener) return;
    JNIEnv* e = envForThread();
    e->CallVoidMethod(g_listener, m_onComplete, ok, fail, skip);
    clearEx(e);
}

void ProgressBar::update(int pos) {
    if (pos == pos_prev || !len) return;
    pos_prev = pos;
    int percent = (int)(((float)pos / (float)len) * 100.f);
    if (percent > 100) percent = 100;
    if (percent != c_prev) {
        c_prev = percent;
        postFileProgressStatic(tls_label, percent);
    }
}

void ProgressBar::complete() {
    postFileProgressStatic(tls_label, 100);
}

int ProgressBar::get_console_width() {
    return 0;
}

static FileType fileTypeOf(const std::string& path) {
    static const std::pair<const char*, FileType> exts[] = {
        {".mp2", FileType::MP2}, {".mp3", FileType::MP3}, {".flac", FileType::FLAC},
        {".ogg", FileType::OGG}, {".oga", FileType::OGG}, {".spx", FileType::OGG},
        {".opus", FileType::OPUS}, {".m4a", FileType::M4A}, {".mp4", FileType::M4A},
        {".wma", FileType::WMA}, {".wav", FileType::WAV}, {".aiff", FileType::AIFF},
        {".aif", FileType::AIFF}, {".snd", FileType::AIFF}, {".wv", FileType::WAVPACK},
        {".ape", FileType::APE}, {".tak", FileType::TAK}, {".mpc", FileType::MPC},
        {".dsf", FileType::DSF}
    };
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto pos = lower.rfind('.');
    if (pos == std::string::npos) return FileType::INVALID;
    std::string ext = lower.substr(pos);
    for (auto& e : exts) if (ext == e.first) return e.second;
    return FileType::INVALID;
}

static bool readRgTags(const std::string& path, std::string& gain, std::string& peak) {
    gain.clear();
    peak.clear();
    TagLib::FileRef f(path.c_str(), false);
    if (f.isNull()) return false;
    if (TagLib::Ogg::XiphComment* xc = dynamic_cast<TagLib::Ogg::XiphComment*>(f.tag())) {
        const auto& m = xc->fieldListMap();
        auto it = m.find("REPLAYGAIN_TRACK_GAIN");
        if (it == m.end()) return false;
        gain = it->second.toString().to8Bit(true);
        auto itp = m.find("REPLAYGAIN_TRACK_PEAK");
        if (itp != m.end()) peak = itp->second.toString().to8Bit(true);
        return true;
    }
    TagLib::ID3v2::Tag* id3 = nullptr;
    if (auto* mf = dynamic_cast<TagLib::MPEG::File*>(f.file())) id3 = mf->ID3v2Tag();
    else if (auto* wf = dynamic_cast<TagLib::RIFF::WAV::File*>(f.file())) id3 = wf->ID3v2Tag();
    else if (auto* af = dynamic_cast<TagLib::RIFF::AIFF::File*>(f.file())) id3 = af->tag();
    else if (auto* df = dynamic_cast<TagLib::DSF::File*>(f.file())) id3 = df->tag();
    if (id3) {
        const auto& map = id3->frameListMap();
        auto it = map.find("TXXX");
        if (it == map.end()) return false;
        for (auto* fr : it->second) {
            auto* tx = dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame*>(fr);
            if (!tx || tx->fieldList().size() < 2) continue;
            if (tx->description().upper() != "REPLAYGAIN_TRACK_GAIN") continue;
            gain = tx->fieldList()[1].to8Bit(true);
            for (auto* fr2 : it->second) {
                auto* tx2 = dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame*>(fr2);
                if (!tx2 || tx2->fieldList().size() < 2) continue;
                if (tx2->description().upper() == "REPLAYGAIN_TRACK_PEAK") peak = tx2->fieldList()[1].to8Bit(true);
            }
            return true;
        }
        return false;
    }
    if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(f.file())) {
        TagLib::MP4::Tag* t = mp4->tag();
        if (!t) return false;
        const auto& m = t->itemMap();
        auto it = m.find("----:com.apple.iTunes:REPLAYGAIN_TRACK_GAIN");
        if (it == m.end()) return false;
        gain = it->second.toStringList().front().to8Bit(true);
        auto itp = m.find("----:com.apple.iTunes:REPLAYGAIN_TRACK_PEAK");
        if (itp != m.end() && !itp->second.toStringList().isEmpty()) peak = itp->second.toStringList().front().to8Bit(true);
        return true;
    }
    TagLib::APE::Tag* ape = nullptr;
    if (auto* af = dynamic_cast<TagLib::APE::File*>(f.file())) ape = af->APETag();
    else if (auto* wf = dynamic_cast<TagLib::WavPack::File*>(f.file())) ape = wf->APETag();
    else if (auto* mcf = dynamic_cast<TagLib::MPC::File*>(f.file())) ape = mcf->APETag();
    if (ape) {
        const auto& m = ape->itemListMap();
        auto it = m.find("REPLAYGAIN_TRACK_GAIN");
        if (it == m.end()) return false;
        gain = it->second.values().front().to8Bit(true);
        auto itp = m.find("REPLAYGAIN_TRACK_PEAK");
        if (itp != m.end() && !itp->second.values().isEmpty()) peak = itp->second.values().front().to8Bit(true);
        return true;
    }
    if (auto* asf = dynamic_cast<TagLib::ASF::File*>(f.file())) {
        TagLib::ASF::Tag* t = asf->tag();
        if (!t) return false;
        const auto attrs = t->attribute("REPLAYGAIN_TRACK_GAIN");
        if (attrs.isEmpty()) return false;
        gain = attrs.front().toString().to8Bit(true);
        const auto pattrs = t->attribute("REPLAYGAIN_TRACK_PEAK");
        if (!pattrs.isEmpty()) peak = pattrs.front().toString().to8Bit(true);
        return true;
    }
    return false;
}

extern "C" {

JNIEXPORT jint JNICALL Java_com_himig_offline_RgNative_nativeScan(
    JNIEnv* env, jclass,
    jobjectArray jfiles, jintArray jalbums,
    jint jtagMode, jdouble jtargetLoudness,
    jboolean jdoAlbum, jboolean jskipExisting, jboolean jtruePeak,
    jobject jlistener) {

    env->GetJavaVM(&g_vm);
    av_log_set_callback(nullptr);
    jclass lc = env->GetObjectClass(jlistener);
    m_onAlbumStart = env->GetMethodID(lc, "onAlbumStart", "(III)V");
    m_onFileProgress = env->GetMethodID(lc, "onFileProgress", "(Ljava/lang/String;I)V");
    m_onFileDone = env->GetMethodID(lc, "onFileDone", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V");
    m_onAlbumDone = env->GetMethodID(lc, "onAlbumDone", "(II)V");
    m_onComplete = env->GetMethodID(lc, "onComplete", "(III)V");
    m_onError = env->GetMethodID(lc, "onError", "(Ljava/lang/String;)V");
    g_listener = env->NewGlobalRef(jlistener);

    jsize n = env->GetArrayLength(jfiles);
    jint* albumArr = env->GetIntArrayElements(jalbums, nullptr);
    std::vector<std::string> paths;
    std::vector<int> albumIds;
    paths.reserve(n);
    for (jsize i = 0; i < n; i++) {
        jstring js = (jstring)env->GetObjectArrayElement(jfiles, i);
        const char* c = env->GetStringUTFChars(js, nullptr);
        paths.emplace_back(c ? c : "");
        if (c) env->ReleaseStringUTFChars(js, c);
        env->DeleteLocalRef(js);
        albumIds.push_back(albumArr[i]);
    }
    env->ReleaseIntArrayElements(jalbums, albumArr, JNI_ABORT);

    Config config;
    config.tag_mode = (jtagMode == 1) ? 'i' : 's';
    config.skip_existing = false;
    config.target_loudness = (double)jtargetLoudness;
    config.max_peak_level = 0.0;
    config.true_peak = (bool)jtruePeak;
    config.clip_mode = 'n';
    config.do_album = (bool)jdoAlbum;
    config.album_as_aes77 = false;
    config.tab_output = OutputType::NONE;
    config.sep_header = false;
    config.sort_alphanum = false;
    config.lowercase = false;
    config.id3v2version = ID3V2_KEEP;
    config.opus_mode = 'd';
    config.skip_mp4 = false;
    config.preserve_mtimes = false;
    config.dual_mono = false;

    std::atomic<int> okCount{0};
    std::atomic<int> failCount{0};
    std::atomic<int> skipCount{0};

    struct Album { int id; std::vector<std::string> files; };
    std::vector<Album> albums;
    {
        std::vector<int> idx(n);
        for (int i = 0; i < n; i++) idx[i] = i;
        std::sort(idx.begin(), idx.end(), [&](int a, int b) { return albumIds[a] < albumIds[b]; });
        for (int i : idx) {
            const std::string& p = paths[i];
            FileType ft = fileTypeOf(p);
            if (ft == FileType::INVALID) {
                failCount++;
                postFileDoneStatic(p, "", "", false);
                continue;
            }
            if (jskipExisting) {
                ScanJob::Track t(std::filesystem::path(p), ft);
                if (tag_exists(t)) {
                    std::string g, pk;
                    readRgTags(p, g, pk);
                    skipCount++;
                    postFileDoneStatic(p, g, pk, false);
                    continue;
                }
            }
            if (jdoAlbum) {
                if (albums.empty() || albums.back().id != albumIds[i]) albums.push_back({albumIds[i], {}});
                albums.back().files.push_back(p);
            } else {
                albums.push_back({albumIds[i], {p}});
            }
        }
    }

    if (!albums.empty()) {
        std::mutex ffmpeg_mutex;
        std::atomic<size_t> next{0};
        size_t nthreads = std::thread::hardware_concurrency();
        if (nthreads < 2) nthreads = 2;
        if (nthreads > 8) nthreads = 8;
        if (albums.size() < nthreads) nthreads = albums.size();

        auto worker = [&]() {
            envForThread();
            while (true) {
                size_t a = next.fetch_add(1);
                if (a >= albums.size()) break;
                Album& al = albums[a];
                postAlbumStartStatic((int)a, (int)albums.size(), (int)al.files.size());
                tls_label = al.files.size() == 1 ? al.files[0] : "album " + std::to_string(a + 1);
                std::vector<std::string> hold = al.files;
                std::vector<char*> argv;
                argv.reserve(hold.size());
                for (auto& s : hold) argv.push_back(s.data());
                std::unique_ptr<ScanJob> job(ScanJob::factory(argv.data(), argv.size(), config));
                if (!job) {
                    ALOGE("factory failed album %zu", a);
                    failCount += (int)hold.size();
                    for (auto& s : hold) postFileDoneStatic(s, "", "", false);
                    postAlbumDoneStatic((int)a, (int)albums.size());
                    continue;
                }
                bool scanOk = job->scan(&ffmpeg_mutex);
                if (!scanOk || job->error) {
                    failCount += (int)hold.size();
                    for (auto& s : hold) postFileDoneStatic(s, "", "", false);
                } else {
                    for (auto& s : hold) {
                        std::string g, pk;
                        bool has = readRgTags(s, g, pk);
                        if (has) okCount++; else failCount++;
                        postFileDoneStatic(s, g, pk, config.tag_mode == 'i' && has);
                    }
                }
                postAlbumDoneStatic((int)a, (int)albums.size());
            }
            g_vm->DetachCurrentThread();
        };

        std::vector<std::thread> pool;
        for (size_t i = 0; i < nthreads; i++) pool.emplace_back(worker);
        for (auto& t : pool) t.join();
    }

    postCompleteStatic(okCount.load(), failCount.load(), skipCount.load());
    env->DeleteGlobalRef(g_listener);
    g_listener = nullptr;
    return 0;
}

} // extern "C"
