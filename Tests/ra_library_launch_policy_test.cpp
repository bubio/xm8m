#include "ra_disk_transaction.h"
#include "ra_media_change_policy.h"
#include <cstdlib>
#include <iostream>

int main()
{
	using namespace Xm8Ra;
	bool ok = true;
	auto check = [&ok](bool condition, const char *message) {
		if (!condition) {
			std::cerr << "FAIL: " << message << '\n';
			ok = false;
		}
	};

	// A Library profile's Drive 2 is mounted with Drive 1. RA never verifies it.
	const RaSessionState states[] = {RaSessionState::Ready,
		RaSessionState::Starting, RaSessionState::Active,
		RaSessionState::ActiveDisconnected, RaSessionState::Offline};
	for (RaSessionState state : states) {
		RaDiskPolicyContext input;
		input.ra_enabled = true;
		input.session_state = state;
		input.role = RaDiskRole::Auxiliary;
		check(ClassifyRaDiskAction(input) == RaDiskAction::MountLocal,
			"Library Drive 2 is a local mount in every session state");
	}

	// Only a logged-in service which cannot reach RA starts locally.
	check(ShouldForceLibraryOfflineForRa(true, true, true, false),
		"unreachable logged-in launch starts Offline");
	check(!ShouldForceLibraryOfflineForRa(true, true, true, true),
		"reachable launch identifies the game");
	check(!ShouldForceLibraryOfflineForRa(true, true, false, false),
		"logged-out launch follows the normal login path");
	check(!ShouldForceLibraryOfflineForRa(false, true, true, false),
		"RA OFF never forces an RA Offline session");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
