#include "screenshot.h"
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
    Check(Screenshot::FileUrl("/tmp/a b#c%画像").find("a%20b%23c%25%E7%94%BB%E5%83%8F") != std::string::npos, "Folder URLs must percent-encode paths");
    Check(!Screenshot::PicturesDirectory().empty(), "OS picture directory must resolve");
    std::remove(first.path.c_str()); std::remove(second.path.c_str()); std::remove(config.c_str());
    std::puts("Screenshot PNG, line modes, storage collision/error and XDG tests passed");
    return 0;
}
