#include "ra_media_change_policy.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void Check(bool value, const std::string& message)
{
	if (!value) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

// One row of the media table in the RA media specification (45 rev. 1.2/1.3).
struct Row {
	Xm8Ra::RaSessionState state;
	bool same_hash;
	Xm8Ra::RaMediaRelation relation;
	Xm8Ra::RaDiskAction drive1;
	const char *name;
};
}

int main()
{
	using namespace Xm8Ra;
	const RaMediaRelation current = RaMediaRelation::CurrentGame;
	const RaMediaRelation other = RaMediaRelation::OtherGame;
	const RaMediaRelation unknown = RaMediaRelation::Unregistered;
	const Row rows[] = {
		// Ready: any Drive 1 medium is cold reset and identified.
		{RaSessionState::Ready, false, current, RaDiskAction::BeginAnchorLaunch, "Ready current"},
		{RaSessionState::Ready, false, other, RaDiskAction::BeginAnchorLaunch, "Ready other"},
		{RaSessionState::Ready, false, unknown, RaDiskAction::BeginAnchorLaunch, "Ready unregistered"},
		{RaSessionState::Ready, true, unknown, RaDiskAction::BeginAnchorLaunch, "Ready same hash"},
		// Offline: mount only. The next cold reset identifies it.
		{RaSessionState::Offline, false, current, RaDiskAction::MountLocal, "Offline current"},
		{RaSessionState::Offline, false, other, RaDiskAction::MountLocal, "Offline other"},
		{RaSessionState::Offline, false, unknown, RaDiskAction::MountLocal, "Offline unregistered"},
		{RaSessionState::Offline, true, unknown, RaDiskAction::MountLocal, "Offline same hash"},
		// Starting: Drive 1 is being identified.
		{RaSessionState::Starting, false, current, RaDiskAction::RejectBusy, "Starting current"},
		{RaSessionState::Starting, false, other, RaDiskAction::RejectBusy, "Starting other"},
		{RaSessionState::Starting, false, unknown, RaDiskAction::RejectBusy, "Starting unregistered"},
		{RaSessionState::Starting, true, current, RaDiskAction::RejectBusy, "Starting same hash"},
		// A running game. Connectivity does not change the answer.
		{RaSessionState::Active, true, current, RaDiskAction::MountLocal, "Active same hash"},
		{RaSessionState::Active, false, current, RaDiskAction::MountSameGameAnchor, "Active current"},
		{RaSessionState::Active, false, other, RaDiskAction::RestartAnchorLaunch, "Active other"},
		{RaSessionState::Active, false, unknown, RaDiskAction::EnterOfflineAndMount, "Active unregistered"},
		{RaSessionState::ActiveDisconnected, true, current, RaDiskAction::MountLocal, "Disconnected same hash"},
		{RaSessionState::ActiveDisconnected, false, current, RaDiskAction::MountSameGameAnchor, "Disconnected current"},
		{RaSessionState::ActiveDisconnected, false, other, RaDiskAction::RestartAnchorLaunch, "Disconnected other"},
		{RaSessionState::ActiveDisconnected, false, unknown, RaDiskAction::EnterOfflineAndMount, "Disconnected unregistered"},
	};

	for (const Row& row : rows) {
		RaDiskPolicyContext input;
		input.ra_enabled = true;
		input.session_state = row.state;
		input.same_hash = row.same_hash;
		input.relation = row.relation;

		input.role = RaDiskRole::Anchor;
		Check(ClassifyRaDiskAction(input) == row.drive1,
			std::string("Drive 1: ") + row.name);

		// Drive 2 never takes part in an RA decision.
		input.role = RaDiskRole::Auxiliary;
		Check(ClassifyRaDiskAction(input) == RaDiskAction::MountLocal,
			std::string("Drive 2 is always local: ") + row.name);

		// RA OFF is an ordinary local mount for both drives.
		input.ra_enabled = false;
		input.role = RaDiskRole::Anchor;
		Check(ClassifyRaDiskAction(input) == RaDiskAction::MountLocal,
			std::string("RA OFF Drive 1: ") + row.name);
		input.role = RaDiskRole::Auxiliary;
		Check(ClassifyRaDiskAction(input) == RaDiskAction::MountLocal,
			std::string("RA OFF Drive 2: ") + row.name);
	}

	// Local knowledge decides the relation. Anything unconfirmed is unregistered.
	Check(ClassifyRaMediaRelation(true, 1234, 1234) == current,
		"registered hash of the running game");
	Check(ClassifyRaMediaRelation(true, 5678, 1234) == other,
		"registered hash of another game");
	Check(ClassifyRaMediaRelation(true, 0, 1234) == unknown,
		"hash known to be unregistered");
	Check(ClassifyRaMediaRelation(false, 1234, 1234) == unknown,
		"hash without local knowledge is unregistered");
	Check(ClassifyRaMediaRelation(true, 1234, 0) == other,
		"no running game cannot make a medium current");

	if (failures != 0) return EXIT_FAILURE;
	std::cout << "ra_media_change_policy_test: PASS\n";
	return EXIT_SUCCESS;
}
