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

Xm8Ra::RaDiskAction Decide(Xm8Ra::RaDiskRole role,
	Xm8Ra::RaSessionState state, bool same_hash, bool same_game,
	bool verified, bool online)
{
	Xm8Ra::RaDiskPolicyContext input;
	input.ra_enabled = true;
	input.role = role;
	input.session_state = state;
	input.same_hash = same_hash;
	input.same_game = same_game;
	input.hash_verified_for_current_game = verified;
	input.network_available = online;
	return Xm8Ra::ClassifyRaDiskAction(input);
}
}

int main()
{
	using namespace Xm8Ra;
	// AM-01/02: source and selected play mode cannot affect this request.
	const RaDiskAction open = Decide(RaDiskRole::Anchor, RaSessionState::Active,
		false, true, false, true);
	const RaDiskAction bank = Decide(RaDiskRole::Anchor, RaSessionState::Active,
		false, true, false, true);
	Check(open == RaDiskAction::ChangeAnchorMedia && open == bank,
		"Open and bank switch share the Drive 1 decision");
	// AM-03/05/07: invalid or unverified media never blocks local use.
	Check(Decide(RaDiskRole::Anchor, RaSessionState::Active, false, false,
		false, false) == RaDiskAction::EnterOfflineAndMount,
		"offline Drive 1 falls back instead of rejecting");
	Check(Decide(RaDiskRole::Auxiliary, RaSessionState::ActiveDisconnected,
		false, false, false, false) == RaDiskAction::EnterOfflineAndMount,
		"unverified disconnected Drive 2 falls back");
	// AM-06: a cache hit is the sole disconnected Drive 2 continuation.
	Check(Decide(RaDiskRole::Auxiliary, RaSessionState::ActiveDisconnected,
		false, false, true, false) == RaDiskAction::MountLocal,
		"verified disconnected Drive 2 keeps the session");
	return EXIT_SUCCESS;
}
