#include "ra_media_change_policy.h"

#include <cstdlib>
#include <iostream>

int main()
{
	bool ok = true;
	auto check = [&ok](bool condition, const char *message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			ok = false;
		}
	};

	check(Xm8Ra::CanEjectRaMedia(0, false),
		"idle Drive 1 may be ejected");
	check(!Xm8Ra::CanEjectRaMedia(0, true),
		"Drive 1 eject is blocked while RA identifies it");
	check(Xm8Ra::CanEjectRaMedia(1, false),
		"Drive 2 may be ejected");
	check(Xm8Ra::CanEjectRaMedia(1, true),
		"Drive 2 may be ejected while Drive 1 is identified");

	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
