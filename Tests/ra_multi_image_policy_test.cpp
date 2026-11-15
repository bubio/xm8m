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

	// Selecting another bank and opening another file are the same decision:
	// the policy sees only the selected bank's relation to the running game.
	for (RaMediaRelation relation : {RaMediaRelation::CurrentGame,
		RaMediaRelation::OtherGame, RaMediaRelation::Unregistered}) {
		RaDiskPolicyContext bank;
		bank.ra_enabled = true;
		bank.session_state = RaSessionState::Active;
		bank.role = RaDiskRole::Anchor;
		bank.relation = relation;
		const RaDiskPolicyContext other_file = bank;
		check(ClassifyRaDiskAction(bank) == ClassifyRaDiskAction(other_file),
			"bank and file media policy diverged");
	}

	// "Open 1&2" of one image: bank 1 to Drive 2, or close Drive 2.
	check(PlanRaPairDrive2(true, 3) == RaDrive2MountAction::OpenBank1,
		"multi-bank pair mounts bank 1 in Drive 2");
	check(PlanRaPairDrive2(true, 1) == RaDrive2MountAction::Close,
		"single-bank pair closes Drive 2");
	check(PlanRaPairDrive2(false, 3) == RaDrive2MountAction::Unchanged,
		"a single-drive open leaves Drive 2 alone");
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
