#include "ra_media_change_policy.h"

#include <cstdlib>
#include <iostream>

int main()
{
	if (Xm8Ra::MustPersistRaLaunchProfileForMount(
		Xm8Ra::RaDiskRole::Auxiliary)) {
		std::cerr << "FAIL: Drive 2 metadata failure must not roll back its mount\n";
		return EXIT_FAILURE;
	}
	if (!Xm8Ra::MustPersistRaLaunchProfileForMount(
		Xm8Ra::RaDiskRole::Anchor)) {
		std::cerr << "FAIL: Drive 1 must preserve the anchor profile invariant\n";
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
