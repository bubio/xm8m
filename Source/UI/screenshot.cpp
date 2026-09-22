#include "screenshot.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <stdexcept>
#include "SDL.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#endif

namespace Screenshot {
namespace {
void BigEndian(std::vector<uint8_t>& out, uint32_t n) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(uint8_t(n >> s));
}
void Chunk(std::vector<uint8_t>& out, const char *type, const std::vector<uint8_t>& data) {
    BigEndian(out, uint32_t(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = 0xffffffff;
    for (size_t i = start; i < out.size(); ++i) {
        crc ^= out[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    BigEndian(out, ~crc);
}
#ifdef _WIN32
std::wstring Wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, nullptr, 0);
    if (!n) return {};
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1, &w[0], n);
    w.resize(n - 1);
    return w;
}
std::string Utf8(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (!n) return {};
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    s.resize(n - 1);
    return s;
}
#endif
bool MakeDirectories(const std::string& path) {
#ifdef _WIN32
    int status = SHCreateDirectoryExW(nullptr, Wide(path).c_str(), nullptr);
    if (status == ERROR_SUCCESS || status == ERROR_ALREADY_EXISTS) return true;
    errno = status == ERROR_FILE_EXISTS ? ENOTDIR : EACCES;
    return false;
#else
    if (path.empty() || path[0] != '/') { errno = EINVAL; return false; }
    for (size_t i = 1; i <= path.size(); ++i) {
        if (i < path.size() && path[i] != '/') continue;
        std::string part = path.substr(0, i);
        if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
        struct stat st;
        if (stat(part.c_str(), &st) != 0) return false;
        if (!S_ISDIR(st.st_mode)) { errno = ENOTDIR; return false; }
    }
    return true;
#endif
}
std::string Stem() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    time_t seconds = std::chrono::system_clock::to_time_t(now);
    tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char date[64], suffix[16];
    std::strftime(date, sizeof(date), "XM8_%Y%m%d_%H%M%S", &local);
    std::snprintf(suffix, sizeof(suffix), "_%03d", int(ms % 1000));
    return std::string(date) + suffix;
}
}

std::vector<uint8_t> EncodePng(const uint32_t *pixels, int width, int height,
    int stride, int row_step, bool double_lines) {
    if (!pixels || width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        stride < width || row_step < 1 || row_step > 2) throw std::invalid_argument("Invalid screenshot frame");
    std::vector<uint8_t> raw;
    raw.reserve(size_t(width * 3 + 1) * height);
    for (int y = 0; y < height; ++y) {
        raw.push_back(0); // PNG filter: None
        const uint32_t *row = pixels + (double_lines ? y / 2 : y) * row_step * stride;
        for (int x = 0; x < width; ++x) {
            raw.push_back(uint8_t(row[x] >> 16));
            raw.push_back(uint8_t(row[x] >> 8));
            raw.push_back(uint8_t(row[x]));
        }
    }
    // zlib stream with stored DEFLATE blocks: no external codec dependency.
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t offset = 0; offset < raw.size();) {
        unsigned n = unsigned(std::min(size_t(65535), raw.size() - offset));
        z.push_back(offset + n == raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n)); z.push_back(uint8_t((~n) >> 8));
        z.insert(z.end(), raw.begin() + offset, raw.begin() + offset + n);
        offset += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    BigEndian(z, (b << 16) | a);
    std::vector<uint8_t> out = {137, 80, 78, 71, 13, 10, 26, 10}, ihdr;
    BigEndian(ihdr, width); BigEndian(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    Chunk(out, "IHDR", ihdr); Chunk(out, "IDAT", z); Chunk(out, "IEND", {});
    return out;
}

std::string ReadXdgPictures(const std::string& config, const std::string& home) {
    std::ifstream file(config);
    std::string line;
    while (std::getline(file, line)) {
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos || line.compare(p, 17, "XDG_PICTURES_DIR=") != 0) continue;
        p += 17;
        if (p >= line.size() || line[p++] != '"') continue;
        std::string value;
        bool valid = true, closed = false;
        if (line.compare(p, 5, "$HOME") == 0 && p + 5 < line.size() &&
            (line[p + 5] == '/' || line[p + 5] == '"')) { value = home; p += 5; }
        for (; p < line.size(); ++p) {
            char c = line[p];
            if (c == '"') { closed = true; break; }
            if (c == '\\' && p + 1 < line.size()) { value += line[++p]; continue; }
            if (c == '$' || c == '`') { valid = false; break; }
            value += c;
        }
        if (valid && closed && !value.empty() && value[0] == '/') return value;
    }
    return home.empty() ? std::string() : home + "/Pictures";
}
#ifdef __APPLE__
std::string MacPicturesDirectory();
#endif
std::string PicturesDirectory() {
#ifdef _WIN32
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DEFAULT, nullptr, &path))) return {};
    std::string result = Utf8(path);
    CoTaskMemFree(path);
    return result;
#elif defined(__APPLE__)
    return MacPicturesDirectory();
#elif defined(__ANDROID__)
    return "Pictures";
#else
    const char *env = SDL_getenv("HOME");
    std::string home = env ? env : "";
    if (home.empty() || home[0] != '/') {
        struct passwd *pw = getpwuid(getuid());
        home = pw && pw->pw_dir ? pw->pw_dir : "";
    }
    env = SDL_getenv("XDG_CONFIG_HOME");
    std::string config = env && env[0] == '/' ? env : home + "/.config";
    return ReadXdgPictures(config + "/user-dirs.dirs", home);
#endif
}
std::string Directory() {
    std::string root = PicturesDirectory();
    return root.empty() ? root : root + "/XM8/Screenshots";
}
std::string FileUrl(const std::string& path) {
    static const char hex[] = "0123456789ABCDEF";
    std::string url = "file://";
#ifdef _WIN32
    if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
        return FileUrl("//" + path.substr(2));
    }
    if (path.compare(0, 2, "//") == 0) url = "file:";
    else url += '/';
#endif
    for (unsigned char c : path) {
#ifdef _WIN32
        if (c == '\\') c = '/';
#endif
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' || c == '~') url += c;
        else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
    }
    return url;
}
bool OpenDirectory(std::string& error) {
    const std::string dir = Directory();
    if (dir.empty() || !MakeDirectories(dir)) { error = "Cannot create screenshot folder: " + dir; return false; }
    if (SDL_OpenURL(FileUrl(dir).c_str()) != 0) { error = SDL_GetError(); return false; }
    return true;
}

Result Save(const std::vector<uint8_t>& png, const std::string& stem, const std::string& directory) {
    Result result{false, directory, {}};
#ifdef __ANDROID__
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) { result.error = "Android activity unavailable"; return result; }
    jclass cls = env->GetObjectClass(activity);
    jmethodID method = cls ? env->GetMethodID(cls, "saveScreenshot", "([BLjava/lang/String;)Ljava/lang/String;") : nullptr;
    jbyteArray data = method && !env->ExceptionCheck() ? env->NewByteArray(jsize(png.size())) : nullptr;
    jstring name = data && !env->ExceptionCheck() ? env->NewStringUTF(stem.c_str()) : nullptr;
    jstring reply = nullptr;
    if (method && data && name && !env->ExceptionCheck()) {
        env->SetByteArrayRegion(data, 0, jsize(png.size()), reinterpret_cast<const jbyte *>(png.data()));
        if (!env->ExceptionCheck()) reply = static_cast<jstring>(env->CallObjectMethod(activity, method, data, name));
    }
    if (env->ExceptionCheck()) { env->ExceptionClear(); result.error = "Android screenshot storage failed"; }
    else if (reply) {
        const char *text = env->GetStringUTFChars(reply, nullptr);
        if (text) {
            std::string message(text);
            env->ReleaseStringUTFChars(reply, text);
            result.success = message.compare(0, 3, "OK:") == 0;
            if (result.success) result.path = message.substr(3);
            else result.error = message;
        }
    }
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (!result.success && result.error.empty()) result.error = "Android screenshot storage failed";
    if (reply) env->DeleteLocalRef(reply);
    if (name) env->DeleteLocalRef(name);
    if (data) env->DeleteLocalRef(data);
    env->DeleteLocalRef(cls); env->DeleteLocalRef(activity);
#else
    if (directory.empty() || !MakeDirectories(directory)) {
        result.error = "Cannot create screenshot folder: " + std::string(std::strerror(errno));
        return result;
    }
    int fd = -1;
    for (unsigned i = 0; i < 10000; ++i) {
        result.path = directory + "/" + stem + (i ? "_" + std::to_string(i) : "") + ".png";
#ifdef _WIN32
        fd = _wopen(Wide(result.path).c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        fd = open(result.path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
#endif
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) { result.error = std::strerror(errno); return result; }
#ifdef _WIN32
    FILE *file = _fdopen(fd, "wb");
#else
    FILE *file = fdopen(fd, "wb");
#endif
    bool ok = false;
    int saved_errno = 0;
    if (file) {
        ok = fwrite(png.data(), 1, png.size(), file) == png.size();
        if (!ok) saved_errno = errno;
        if (fclose(file) != 0) { ok = false; saved_errno = errno; }
    } else {
        saved_errno = errno;
#ifdef _WIN32
        _close(fd);
#else
        close(fd);
#endif
    }
    if (!ok) {
        result.error = saved_errno ? std::strerror(saved_errno) : "Incomplete screenshot write";
#ifdef _WIN32
        _wremove(Wide(result.path).c_str());
#else
        unlink(result.path.c_str());
#endif
        return result;
    }
    result.success = true;
#endif
    return result;
}

struct Writer::Impl {
    struct Job { std::vector<uint32_t> pixels; std::string stem; };
    std::string directory;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::deque<Result> results;
    bool stopping = false;
    std::thread worker;
    void Run() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stopping || !jobs.empty(); });
                if (jobs.empty()) return;
                job = std::move(jobs.front()); jobs.pop_front();
            }
            Result result;
            try { result = Save(EncodePng(job.pixels.data(), 640, 400, 640, 1, false), job.stem, directory.empty() ? Directory() : directory); }
            catch (const std::exception& e) { result = {false, {}, e.what()}; }
            std::lock_guard<std::mutex> lock(mutex);
            results.push_back(std::move(result));
        }
    }
};
Writer::Writer(const std::string& directory) : impl(new Impl) { impl->directory = directory; }
Writer::~Writer() { Finish(); delete impl; }
void Writer::Finish() {
    { std::lock_guard<std::mutex> lock(impl->mutex); impl->stopping = true; }
    impl->wake.notify_all();
    if (impl->worker.joinable()) impl->worker.join();
}
bool Writer::Submit(const uint32_t *pixels, int stride, int row_step, bool double_lines) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (!pixels || impl->stopping || impl->jobs.size() >= 8) return false;
    Impl::Job job;
    job.stem = Stem();
    job.pixels.resize(640 * 400);
    for (int y = 0; y < 400; ++y)
        std::copy_n(pixels + (double_lines ? y / 2 : y) * row_step * stride, 640, job.pixels.data() + y * 640);
    if (!impl->worker.joinable()) impl->worker = std::thread([this] { impl->Run(); });
    impl->jobs.push_back(std::move(job));
    impl->wake.notify_one();
    return true;
}
bool Writer::Poll(Result& result) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->results.empty()) return false;
    result = std::move(impl->results.front()); impl->results.pop_front();
    return true;
}
}
