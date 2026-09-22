#ifndef XM8_SCREENSHOT_H
#define XM8_SCREENSHOT_H

#include <cstdint>
#include <string>
#include <vector>

namespace Screenshot {
// Input pixels are 0x00RRGGBB. Row step is 2 for scanlined 200-line frames.
std::vector<uint8_t> EncodePng(const uint32_t *pixels, int width, int height,
    int stride, int row_step, bool double_lines);
std::string PicturesDirectory();
std::string Directory();
bool OpenDirectory(std::string& error);
std::string FileUrl(const std::string& path);
std::string ReadXdgPictures(const std::string& config, const std::string& home);
struct Result { bool success; std::string path; std::string error; };
Result Save(const std::vector<uint8_t>& png, const std::string& stem,
    const std::string& directory);
class Writer {
public:
    explicit Writer(const std::string& directory = {});
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    bool Submit(const uint32_t *pixels, int stride, int row_step, bool double_lines);
    bool Poll(Result& result);
    void Finish();
private:
    struct Impl;
    Impl *impl;
};
}
#endif
