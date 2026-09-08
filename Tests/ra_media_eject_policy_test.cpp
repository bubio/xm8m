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

	check(Xm8Ra::CanEjectRaMedia(0, false, false, false),
		"idle anchor may be ejected");
	check(!Xm8Ra::CanEjectRaMedia(0, true, false, false),
		"anchor eject is blocked during game identification");
	check(!Xm8Ra::CanEjectRaMedia(0, false, true, false),
		"anchor eject is blocked during media change");
	check(Xm8Ra::CanEjectRaMedia(1, true, true, false),
		"untouched auxiliary may be ejected during an anchor operation");

	for (int drive : {0, 1}) {
		check(!Xm8Ra::CanEjectRaMedia(drive, false, true, true),
			"paired anchor change owns both drives");
		check(!Xm8Ra::CanEjectRaMedia(drive, false, false, true),
			"auxiliary verification does not allow independent eject");
	}

	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
