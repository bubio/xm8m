#include "ra_disk_transaction.h"
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool value, const char *message)
{
	if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
}

int main()
{
	using namespace Xm8Ra;
	// A D&D mount ends in exactly one cold reset.
	Check(PlanDroppedReset(true, false) ==
		RaDroppedResetAction::NormalResetAndIdentifyDrive1,
		"Offline D&D uses normal reset and re-identifies Drive 1");
	Check(PlanDroppedReset(false, false) ==
		RaDroppedResetAction::NormalResetAndIdentifyDrive1,
		"D&D of the running game's disk uses the normal reset");
	Check(PlanDroppedReset(false, true) ==
		RaDroppedResetAction::ResetVmPreservingPendingLaunch,
		"a D&D launch preserves its own pending identification");
	Check(PlanDroppedReset(true, true) ==
		RaDroppedResetAction::NormalResetAndIdentifyDrive1,
		"an Offline session has no pending launch to preserve");

	// A new title begins from a cold VM exactly once.
	RaSynchronousAnchorCommitPlan plan = PlanSynchronousAnchorCommit(true, false);
	Check(plan.reset_vm_now && plan.begin_session,
		"menu launch resets now and begins the session");
	plan = PlanSynchronousAnchorCommit(true, true);
	Check(!plan.reset_vm_now && plan.begin_session,
		"a resetting entry point owns the single reset");
	plan = PlanSynchronousAnchorCommit(false, false);
	Check(!plan.reset_vm_now && !plan.begin_session,
		"an ordinary exchange neither resets nor starts a session");

	// Login, unlock sync and identification share one bounded wait.
	Check(!IsRaLaunchDeadlineExpired(1000, 1000), "deadline starts unexpired");
	Check(!IsRaLaunchDeadlineExpired(1000, 1000 + kRaLaunchDeadlineMs - 1),
		"deadline holds until its last millisecond");
	Check(IsRaLaunchDeadlineExpired(1000, 1000 + kRaLaunchDeadlineMs),
		"deadline expires after the bounded wait");
	Check(!IsRaLaunchDeadlineExpired(0xfffffff0u, 0x00000010u),
		"tick wrap-around does not expire the deadline early");
	Check(IsRaLaunchDeadlineExpired(0xfffffff0u,
		0xfffffff0u + kRaLaunchDeadlineMs),
		"deadline expires across tick wrap-around");
	return EXIT_SUCCESS;
}
