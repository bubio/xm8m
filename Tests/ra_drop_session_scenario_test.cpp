#include "ra_disk_transaction.h"
#include "ra_media_change_policy.h"

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
	// RA ON + two-drive D&D: Drive 1 starts identification. Drive 2 belongs to
	// the same request and is mounted without an RA decision, even though
	// Drive 1 is already Starting.
	RaDiskPolicyContext drive1;
	drive1.ra_enabled = true;
	drive1.session_state = RaSessionState::Ready;
	drive1.role = RaDiskRole::Anchor;
	Check(ClassifyRaDiskAction(drive1) == RaDiskAction::BeginAnchorLaunch,
		"RA ON drop starts Drive 1 identification");
	RaDiskPolicyContext drive2;
	drive2.ra_enabled = true;
	drive2.session_state = RaSessionState::Starting;
	drive2.role = RaDiskRole::Auxiliary;
	Check(ClassifyRaDiskAction(drive2) == RaDiskAction::MountLocal,
		"Drive 2 of the same drop is mounted while Drive 1 is identified");

	// D&D owns the single reset; the launch begun by the drop must survive it.
	const RaSynchronousAnchorCommitPlan commit =
		PlanSynchronousAnchorCommit(true, true);
	Check(commit.begin_session && !commit.reset_vm_now,
		"the drop begins the launch and leaves the reset to its caller");
	Check(PlanDroppedReset(false, true) ==
		RaDroppedResetAction::ResetVmPreservingPendingLaunch,
		"the drop reset keeps the pending identification");

	// A drop which ended the session (unregistered Drive 1) uses the normal
	// reset, which identifies the new Drive 1 once more.
	Check(PlanDroppedReset(true, false) ==
		RaDroppedResetAction::NormalResetAndIdentifyDrive1,
		"unregistered D&D uses normal reset and re-identification");

	// RA ON -> mount -> RA OFF -> replacement must become an ordinary local
	// mount, independent of the previous RA session.
	RaDiskPolicyContext after_mode_off;
	after_mode_off.ra_enabled = false;
	after_mode_off.session_state = RaSessionState::Ready;
	after_mode_off.role = RaDiskRole::Anchor;
	Check(ClassifyRaDiskAction(after_mode_off) == RaDiskAction::MountLocal,
		"RA OFF replacement does not involve RA");
	return EXIT_SUCCESS;
}
