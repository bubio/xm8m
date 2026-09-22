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
	// RA ON + two-drive D&D: Drive 1 starts the anchor and Drive 2 becomes
	// part of that request, rather than a competing Starting-state request.
	RaDiskPolicyContext drive1;
	drive1.ra_enabled = true;
	drive1.session_state = RaSessionState::Ready;
	drive1.role = RaDiskRole::Anchor;
	Check(ClassifyRaDiskAction(drive1) == RaDiskAction::BeginAnchorLaunch,
		"RA ON drop starts Drive 1 anchor");
	Check(CanAttachDrive2ToAnchorLaunch(true, true, false),
		"Drive 2 attaches to the same drop transaction");
	Check(!CanAttachDrive2ToAnchorLaunch(true, true, true),
		"a second transaction is still rejected");

	// The two-drive D&D path defers reset until Drive 1 identification
	// completes. If that identification reports an unregistered game, it is an
	// Offline local commit, but the original D&D reset must still run once.
	RaDiskTransactionState deferred_drop;
	deferred_drop.Begin(RaDiskTransactionKind::Auxiliary,
		RaDiskTransactionPhase::VerifyingAuxiliary, true, true,
		RaDiskTransactionOperation::PairedOpen);
	const bool reset_after_offline_commit = deferred_drop.reset_requested;
	deferred_drop.Clear(); // equivalent to ending the failed RA transaction
	Check(reset_after_offline_commit,
		"unregistered D&D retains its deferred reset before transaction clear");
	Check(PlanDroppedReset(false, true, false) ==
		RaDroppedResetAction::NormalResetAndIdentifyDrive1,
		"unregistered D&D Offline commit uses normal reset/re-anchor");

	// RA ON -> mount -> RA OFF -> replacement must become an ordinary local
	// mount, independent of the previous RA session.
	RaDiskPolicyContext after_mode_off;
	after_mode_off.ra_enabled = false;
	after_mode_off.session_state = RaSessionState::Ready;
	after_mode_off.role = RaDiskRole::Anchor;
	Check(ClassifyRaDiskAction(after_mode_off) == RaDiskAction::MountLocal,
		"RA OFF replacement does not retain an RA transaction");
	return EXIT_SUCCESS;
}
