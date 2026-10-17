#include "platform_path.h"

#ifdef _WIN32
#include <filesystem>
#endif

namespace PlatformPath {
std::string Join(const std::string& directory, const std::string& child)
{
#ifdef _WIN32
    return (std::filesystem::u8path(directory) / std::filesystem::u8path(child))
        .lexically_normal().make_preferred().u8string();
#else
    if (directory.empty()) return child;
    if (child.empty()) return directory;
    return directory + (directory.back() == '/' ? "" : "/") + child;
#endif
}

std::string FolderOpenTarget(const std::string& path)
{
#ifdef _WIN32
    // SDL calls ShellExecuteW, which accepts a UTF-8 native path via SDL's
    // conversion to UTF-16. Avoid file URL escaping and shell URL parsing.
    return std::filesystem::u8path(path).lexically_normal().make_preferred().u8string();
#else
    static const char hex[] = "0123456789ABCDEF";
    std::string url = "file://";
    for (unsigned char c : path) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '/' || c == ':' || c == '-' ||
            c == '_' || c == '.' || c == '~') url += c;
        else { url += '%'; url += hex[c >> 4]; url += hex[c & 15]; }
    }
    return url;
#endif
}
}
