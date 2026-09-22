#include "ra_media_change_policy.h"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool value, const char *message)
{
	if (!value) {
		std::cerr << "FAIL: " << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}
}

int main()
{
	using namespace Xm8Ra;
	RaDiskPolicyContext input;
	input.ra_enabled = true;
	input.role = RaDiskRole::Anchor;
	input.session_state = RaSessionState::Ready;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::BeginAnchorLaunch,
		"Ready Drive 1 starts identification");
	input.session_state = RaSessionState::Active;
	input.same_hash = true;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::MountLocal,
		"same hash is local");
	input.same_hash = false;
	input.same_game = true;
	input.network_available = true;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::ChangeAnchorMedia,
		"same-game Drive 1 changes active media");
	input.same_game = false;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::RestartAnchorLaunch,
		"different Drive 1 game restarts anchor");
	input.network_available = false;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::EnterOfflineAndMount,
		"offline Drive 1 falls back");
	input.role = RaDiskRole::Auxiliary;
	input.session_state = RaSessionState::ActiveDisconnected;
	input.hash_verified_for_current_game = true;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::MountLocal,
		"verified Drive 2 remains available while disconnected");
	input.hash_verified_for_current_game = false;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::EnterOfflineAndMount,
		"unverified Drive 2 falls back while disconnected");
	input.network_available = true;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::VerifyAuxiliary,
		"online Drive 2 is verified");
	input.transaction_active = true;
	Check(ClassifyRaDiskAction(input) == RaDiskAction::RejectBusy,
		"competing request is rejected");
	return EXIT_SUCCESS;
}
