#include "screenshot.h"
#include "platform_path.h"
// This headless test uses the console entry point and does not initialize SDL.
#define SDL_MAIN_HANDLED
#include "SDL.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../ThirdParty/stb/stb_image.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#endif

static void Check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static void Decode(const std::vector<uint8_t>& png, const std::vector<uint32_t>& expected) {
    int w, h, channels;
    unsigned char *rgb = stbi_load_from_memory(png.data(), int(png.size()), &w, &h, &channels, 3);
    Check(rgb != nullptr, "PNG must decode with an independent decoder");
    Check(w == 640 && h == 400 && channels == 3, "PNG dimensions and RGB channels");
    for (int i = 0; i < w * h; ++i) {
        const uint32_t pixel = (uint32_t(rgb[i * 3]) << 16) | (uint32_t(rgb[i * 3 + 1]) << 8) | rgb[i * 3 + 2];
        Check(pixel == (expected[i] & 0xffffff), "PNG pixels must match the game frame");
    }
    stbi_image_free(rgb);
}
#ifdef _WIN32
static std::filesystem::path Extended(const std::string& path) {
    std::wstring wide = std::filesystem::u8path(path).native();
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    if (wide.compare(0, 4, L"\\\\?\\") == 0) return wide;
    return wide.compare(0, 2, L"\\\\") == 0 ? L"\\\\?\\UNC\\" + wide.substr(2) : L"\\\\?\\" + wide;
}
static void CheckWindowsStorage(const std::vector<uint8_t>& png, const std::string& folder) {
    namespace fs = std::filesystem;
    const fs::path root = fs::u8path(folder) / "long-path-test";
    fs::path deep = root;
    for (int i = 0; i < 5; ++i) deep /= std::string(70, 'a' + i);
    const std::string directory = deep.u8string();
    Check(directory.size() > MAX_PATH, "Regression directory must exceed MAX_PATH");
    // Unicode components, mixed separators, and dot components all need normalizing
    // before calling APIs with an extended-length prefix.
    const std::string target = directory + "/./unused/../\xe7\x94\xbb\xe5\x83\x8f folder";
    auto saved = Screenshot::Save(png, "frame", target);
    Check(saved.success, saved.error.c_str());
    auto collision = Screenshot::Save(png, "frame", target);
    Check(collision.success && collision.path != saved.path, "Long paths must retain collision protection");
    const fs::path actual = deep / fs::u8path("\xe7\x94\xbb\xe5\x83\x8f folder") / "frame.png";
    { std::ifstream file(Extended(actual.u8string()), std::ios::binary);
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
      Check(bytes == png, "Long Unicode path must contain the complete PNG"); }
    const auto prefixed = Screenshot::Save(png, "prefixed", Extended(deep.u8string()).u8string());
    Check(prefixed.success, prefixed.error.c_str());
    auto blocked = Screenshot::Save(png, "blocked", actual.u8string() + "/child");
    Check(!blocked.success && blocked.error.find("Cannot create screenshot folder:") == 0 &&
        blocked.error.find("error ") != std::string::npos, "Directory failure must retain its OS error code");
    auto invalid = Screenshot::Save(png, "bad?name", directory);
    Check(!invalid.success && invalid.error.find("Windows error 123") != std::string::npos,
        "Invalid filenames must report ERROR_INVALID_NAME, not permission denied");
    std::error_code cleanup;
    fs::remove_all(Extended(root.u8string()), cleanup);
    Check(!cleanup, "Remove all long-path test files");
}
#endif
int main(int argc, char **argv) {
    Check(argc == 2, "Provide a temporary output directory");
    std::string folder = argv[1];
    std::vector<uint32_t> frame(640 * 400), expected(frame.size());
    for (int y = 0; y < 400; ++y) for (int x = 0; x < 640; ++x)
        frame[y * 640 + x] = 0xff000000 | (x % 256 << 16) | (y % 256 << 8) | ((x + y) % 256);
    const auto png = Screenshot::EncodePng(frame.data(), 640, 400, 640, 1, false);
    Decode(png, frame);
    for (int y = 0; y < 400; ++y) for (int x = 0; x < 640; ++x)
        expected[y * 640 + x] = frame[(y / 2) * 640 + x];
    Decode(Screenshot::EncodePng(frame.data(), 640, 400, 640, 1, true), expected);
    for (int y = 0; y < 400; ++y) for (int x = 0; x < 640; ++x)
        expected[y * 640 + x] = frame[(y / 2 * 2) * 640 + x];
    Decode(Screenshot::EncodePng(frame.data(), 640, 400, 640, 2, true), expected);

    auto first = Screenshot::Save(png, "XM8_test", folder);
    Check(first.success, first.error.c_str());
    auto second = Screenshot::Save(png, "XM8_test", folder);
    Check(second.success && first.path != second.path, "Same timestamp must never overwrite a file");
    std::ifstream file(first.path, std::ios::binary);
    std::vector<uint8_t> saved((std::istreambuf_iterator<char>(file)), {});
    Check(saved == png, "Saved PNG must be complete and unchanged");
    file.close();
    auto failed = Screenshot::Save(png, "blocked", first.path + "/child");
    Check(!failed.success && !failed.error.empty(), "Invalid output directory must report failure");
    const auto unavailable = Screenshot::Save(png, "unavailable", "");
    Check(!unavailable.success && unavailable.error.find("unavailable") != std::string::npos,
        "An unresolved folder must report a meaningful error instead of stale errno");
#ifdef _WIN32
    CheckWindowsStorage(png, folder);
#endif

    Screenshot::Writer writer(folder);
    Check(writer.Submit(frame.data(), 640, 1, false), "Queue first frame");
    Check(writer.Submit(expected.data(), 640, 1, false), "Queue second frame");
    // Changing the source after submission must not change the pending image.
    std::fill(frame.begin(), frame.end(), 0);
    writer.Finish();
    Screenshot::Result result;
    Check(writer.Poll(result) && result.success, "Shutdown must finish the first queued screenshot");
    { std::ifstream f(result.path, std::ios::binary);
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
      Check(bytes == png, "Queued screenshot owns its original pixels"); }
    std::remove(result.path.c_str());
    Check(writer.Poll(result) && result.success, "Shutdown must finish all queued screenshots");
    { std::ifstream f(result.path, std::ios::binary);
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
      Decode(bytes, expected); }
    std::remove(result.path.c_str());
    Check(!writer.Poll(result), "Each saved frame produces one result");
    Check(!writer.Submit(frame.data(), 640, 1, false), "Shutdown rejects new work");

    const std::string config = folder + "/user-dirs.dirs";
    { std::ofstream f(config); f << "# comment\nXDG_PICTURES_DIR=\"$HOME/画像\\\" # album\"\n"; }
    Check(Screenshot::ReadXdgPictures(config, "/home/test") == "/home/test/画像\" # album", "XDG quoted home path and escaping");
    { std::ofstream f(config); f << "XDG_PICTURES_DIR=\"/media/Pictures\"\n"; }
    Check(Screenshot::ReadXdgPictures(config, "/home/test") == "/media/Pictures", "XDG absolute path");
    { std::ofstream f(config); f << "XDG_PICTURES_DIR=\"$HOME\"\n"; }
    Check(Screenshot::ReadXdgPictures(config, "/home/test") == "/home/test", "XDG disabled directory mapped to HOME");
    { std::ofstream f(config); f << "XDG_PICTURES_DIR=\"$(touch /tmp/should-not-exist)\"\n"; }
    Check(Screenshot::ReadXdgPictures(config, "/home/test") == "/home/test/Pictures", "XDG must never execute shell expressions");
#ifdef _WIN32
    Check(PlatformPath::Join("C:\\Pictures\\", "XM8/Screenshots") ==
        "C:\\Pictures\\XM8\\Screenshots", "Path joining must normalize mixed and trailing separators");
    Check(PlatformPath::FolderOpenTarget("C:/画像 folder/a#b%") ==
        "C:\\画像 folder\\a#b%", "Windows shell targets must preserve Unicode, spaces and URL metacharacters");
    Check(PlatformPath::FolderOpenTarget("//server/share/画像 folder") ==
        "\\\\server\\share\\画像 folder", "Windows shell targets must preserve UNC paths");
#else
    Check(PlatformPath::Join("/tmp/", "XM8/Screenshots") ==
        "/tmp/XM8/Screenshots", "Path joining must handle a trailing separator");
    Check(PlatformPath::FolderOpenTarget("/tmp/a b#c%画像") ==
        "file:///tmp/a%20b%23c%25%E7%94%BB%E5%83%8F", "Folder URLs must percent-encode paths");
#endif
    Check(!Screenshot::PicturesDirectory().empty(), "OS picture directory must resolve");
#ifdef _WIN32
    const std::string directory = Screenshot::Directory();
    Check(std::filesystem::u8path(directory).is_absolute(), "Screenshot directory must be absolute");
    Check(directory.find('/') == std::string::npos, "Windows screenshot directory must use native separators");
#endif
    std::remove(first.path.c_str()); std::remove(second.path.c_str()); std::remove(config.c_str());
    std::puts("Screenshot PNG, line modes, storage collision/error and XDG tests passed");
    return 0;
}
