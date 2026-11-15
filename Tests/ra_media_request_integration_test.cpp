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
	Xm8Ra::RaSessionState state, Xm8Ra::RaMediaRelation relation)
{
	Xm8Ra::RaDiskPolicyContext input;
	input.ra_enabled = true;
	input.role = role;
	input.session_state = state;
	input.relation = relation;
	return Xm8Ra::ClassifyRaDiskAction(input);
}
}

int main()
{
	using namespace Xm8Ra;
	// The entry point and the selected play mode are not inputs at all, so
	// Open, bank switch, Casual and Hardcore share one Drive 1 decision.
	Check(Decide(RaDiskRole::Anchor, RaSessionState::Active,
		RaMediaRelation::CurrentGame) == RaDiskAction::MountSameGameAnchor,
		"another disk of the running game keeps the session");
	// Losing the connection does not end the session for a known disk.
	Check(Decide(RaDiskRole::Anchor, RaSessionState::ActiveDisconnected,
		RaMediaRelation::CurrentGame) == RaDiskAction::MountSameGameAnchor,
		"a known disk of the running game is exchanged while disconnected");
	// Unregistered media never block local use; they end the session.
	Check(Decide(RaDiskRole::Anchor, RaSessionState::Active,
		RaMediaRelation::Unregistered) == RaDiskAction::EnterOfflineAndMount,
		"unregistered Drive 1 ends the session instead of rejecting");
	Check(Decide(RaDiskRole::Anchor, RaSessionState::Active,
		RaMediaRelation::OtherGame) == RaDiskAction::RestartAnchorLaunch,
		"another registered game starts its own session");
	// Drive 2 holds save disks and anything else without an RA reaction.
	for (RaMediaRelation relation : {RaMediaRelation::CurrentGame,
		RaMediaRelation::OtherGame, RaMediaRelation::Unregistered}) {
		Check(Decide(RaDiskRole::Auxiliary, RaSessionState::Active,
			relation) == RaDiskAction::MountLocal,
			"Drive 2 media never change the session");
		Check(Decide(RaDiskRole::Auxiliary, RaSessionState::ActiveDisconnected,
			relation) == RaDiskAction::MountLocal,
			"Drive 2 media never change a disconnected session");
		Check(Decide(RaDiskRole::Auxiliary, RaSessionState::Starting,
			relation) == RaDiskAction::MountLocal,
			"Drive 2 is free while Drive 1 is identified");
	}
	return EXIT_SUCCESS;
}
