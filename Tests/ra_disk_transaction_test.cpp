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
	RaDiskTransactionState transaction;
	Check(!transaction.Active(), "new transaction is idle");
	transaction.Begin(RaDiskTransactionKind::Anchor,
		RaDiskTransactionPhase::VerifyingDrive2, true, true);
	Check(transaction.Active() && transaction.OwnsReset(),
		"one transaction owns both drives and reset");
	transaction.phase = RaDiskTransactionPhase::ChangingActiveMedia;
	Check(transaction.Pending(), "media change remains pending");
	transaction.Commit();
	Check(transaction.CanCompleteLaunch(), "commit completes launch once");
	transaction.Clear();
	Check(!transaction.Active() && !transaction.OwnsReset(),
		"terminal path clears transaction ownership");
	Check(CanApplySequentialRaMediaBatch(true, 2, true),
		"two-drive request is accepted as one transaction");
	Check(!CanBeginAuxiliaryVerification(true, false),
		"new anchor is identified before Drive 2 verification");
	Check(PlanDroppedReset(true, false, false) ==
		RaDroppedResetAction::DeferredToTransaction,
		"transaction owns the D&D reset");
	Check(PlanDroppedReset(false, true, false) ==
		RaDroppedResetAction::NormalResetAndIdentifyDrive1,
		"Offline D&D uses normal reset and re-identifies Drive 1");
	Check(PlanDroppedReset(false, false, true) ==
		RaDroppedResetAction::ResetVmPreservingPendingLaunch,
		"a D&D launch preserves its own pending identification");
	return EXIT_SUCCESS;
}
