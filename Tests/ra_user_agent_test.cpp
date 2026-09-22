#include "ra_user_agent.h"

#include <cstdlib>
#include <iostream>

int main()
{
	if (Xm8Ra::MakeXm8mUserAgent("2.0.0") != "XM8M/2.0.0" ||
		!Xm8Ra::MakeXm8mUserAgent("2.0").empty() ||
		!Xm8Ra::MakeXm8mUserAgent("2.0.0 macOS").empty() ||
		!Xm8Ra::MakeXm8mUserAgent("v2.0.0").empty()) {
		std::cerr << "RA User-Agent validation failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "RA User-Agent tests passed\n";
	return EXIT_SUCCESS;
}
