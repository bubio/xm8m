#ifndef XM8_RA_USER_AGENT_H
#define XM8_RA_USER_AGENT_H

#include <string>

namespace Xm8Ra {

inline bool IsNumericSemanticVersion(const std::string& version)
{
	int components = 0;
	bool has_digit = false;
	for (char ch : version) {
		if (ch >= '0' && ch <= '9') {
			has_digit = true;
		}
		else if (ch == '.' && has_digit && components < 2) {
			++components;
			has_digit = false;
		}
		else {
			return false;
		}
	}
	return components == 2 && has_digit;
}

inline std::string MakeXm8mUserAgent(const std::string& version)
{
	return IsNumericSemanticVersion(version) ? "XM8M/" + version : std::string();
}

} // namespace Xm8Ra

#endif
