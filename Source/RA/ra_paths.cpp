#include "ra_paths.h"
#include "platform_path.h"

namespace Xm8Ra {

std::string RootFromSettingDir(const char *setting_dir)
{
	if (setting_dir == nullptr || setting_dir[0] == '\0') {
		return std::string();
	}

	return PlatformPath::JoinPortable(setting_dir, "ra");
}

} // namespace Xm8Ra
