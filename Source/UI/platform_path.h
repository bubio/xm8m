#ifndef XM8_PLATFORM_PATH_H
#define XM8_PLATFORM_PATH_H

#include <string>

namespace PlatformPath {
// UTF-8 paths. child must be a relative path; no existence check is performed.
std::string Join(const std::string& directory, const std::string& child);
// Target for SDL_OpenURL: a native path on Windows, a file URL elsewhere.
std::string FolderOpenTarget(const std::string& path);
}

#endif
