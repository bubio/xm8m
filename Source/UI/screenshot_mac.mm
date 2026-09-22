#import <Foundation/Foundation.h>
#include <string>
namespace Screenshot {
std::string MacPicturesDirectory() {
    @autoreleasepool {
        NSURL *url = [[[NSFileManager defaultManager] URLsForDirectory:NSPicturesDirectory
            inDomains:NSUserDomainMask] firstObject];
        const char *path = [url fileSystemRepresentation];
        return path ? path : "";
    }
}
}
