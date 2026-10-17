#include "platform_path.h"

#include <cstdio>
#include <cstdlib>

static void Check(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

int main()
{
    Check(PlatformPath::JoinPortable("", "ra") == "ra", "Empty parent must not introduce a root");
    Check(PlatformPath::JoinPortable("/tmp/ra", "") == "/tmp/ra", "Empty child preserves the parent");
    Check(PlatformPath::JoinPortable("/", "ra") == "/ra", "Root must retain one separator");
    Check(PlatformPath::JoinPortable("/tmp/ra/", "media/hash/working.d88") ==
        "/tmp/ra/media/hash/working.d88", "Stored relative paths must retain their forward slashes");
    Check(PlatformPath::JoinPortable("/tmp/link/..", "ra") == "/tmp/link/../ra",
        "Portable joining must not normalize components before filesystem resolution");
    Check(PlatformPath::DirectoryPrefix("").empty(), "An empty directory must not become a root");
    Check(PlatformPath::DirectoryPrefix("/tmp/ra/") == "/tmp/ra/", "Directory prefix must not double a separator");
#ifdef _WIN32
    Check(PlatformPath::JoinPortable("C:\\settings\\", "ra") == "C:\\settings\\ra",
        "Windows directory terminators must not get an extra slash");
    const std::string root = "\\\\server\\share\\ra\\";
    const std::string relative = "media/hash/working.d88";
    const std::string joined = PlatformPath::JoinPortable(root, relative);
    Check(joined == root + relative, "UNC paths must preserve the stored relative path text");
    Check(joined.substr(PlatformPath::DirectoryPrefix(root).size()) == relative,
        "Directory prefix must match joining when extracting stored relative paths");
#else
    Check(PlatformPath::JoinPortable("/tmp/name\\", "ra") == "/tmp/name\\/ra",
        "A backslash in a POSIX directory name must not be treated as a separator");
#endif
    std::puts("Platform path tests passed");
}
