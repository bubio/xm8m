#include "ra_disk_transaction.h"
#include "ra_media_change_policy.h"
#include "ra_session_policy.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int assertions = 0;

void Check(bool value, const std::string& message)
{
	++assertions;
	if (!value) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

const char *Name(Xm8Ra::RaSessionState state)
{
	using Xm8Ra::RaSessionState;
	switch (state) {
	case RaSessionState::Ready: return "Ready";
	case RaSessionState::Starting: return "Starting";
	case RaSessionState::Active: return "Active";
	case RaSessionState::ActiveDisconnected: return "ActiveDisconnected";
	case RaSessionState::Offline: return "Offline";
	}
	return "unknown";
}

Xm8Ra::RaDiskAction ExpectedDiskAction(
	const Xm8Ra::RaDiskPolicyContext& input)
{
	using namespace Xm8Ra;
	if (!input.ra_enabled || input.session_state == RaSessionState::Offline)
		return RaDiskAction::MountLocal;
	if (input.transaction_active || input.session_state == RaSessionState::Starting)
		return RaDiskAction::RejectBusy;
	if (input.role == RaDiskRole::Auxiliary) {
		if (input.session_state == RaSessionState::Ready || input.same_hash ||
			input.hash_verified_for_current_game) return RaDiskAction::MountLocal;
		return input.network_available ? RaDiskAction::VerifyAuxiliary :
			RaDiskAction::EnterOfflineAndMount;
	}
	if (input.session_state == RaSessionState::Ready)
		return RaDiskAction::BeginAnchorLaunch;
	if (input.same_hash) return RaDiskAction::MountLocal;
	if (!input.network_available) return RaDiskAction::EnterOfflineAndMount;
	return input.same_game ? RaDiskAction::ChangeAnchorMedia :
		RaDiskAction::RestartAnchorLaunch;
}

Xm8Ra::RaSessionState ExpectedSessionTransition(
	Xm8Ra::RaSessionState state, Xm8Ra::RaSessionSignal signal)
{
	using namespace Xm8Ra;
	if (signal == RaSessionSignal::StopGame) return RaSessionState::Ready;
	switch (state) {
	case RaSessionState::Ready:
		return signal == RaSessionSignal::BeginLaunch ?
			RaSessionState::Starting : state;
	case RaSessionState::Starting:
		if (signal == RaSessionSignal::LaunchSucceeded) return RaSessionState::Active;
		if (signal == RaSessionSignal::LaunchFailed ||
			signal == RaSessionSignal::SessionInvalidated) return RaSessionState::Offline;
		return state;
	case RaSessionState::Active:
		if (signal == RaSessionSignal::Disconnected)
			return RaSessionState::ActiveDisconnected;
		if (signal == RaSessionSignal::SessionInvalidated)
			return RaSessionState::Offline;
		return state;
	case RaSessionState::ActiveDisconnected:
		if (signal == RaSessionSignal::Reconnected) return RaSessionState::Active;
		if (signal == RaSessionSignal::SessionInvalidated)
			return RaSessionState::Offline;
		return state;
	case RaSessionState::Offline:
		return state;
	}
	return state;
}

void VerifyEverySessionSignal()
{
	using namespace Xm8Ra;
	const RaSessionState states[] = {RaSessionState::Ready, RaSessionState::Starting,
		RaSessionState::Active, RaSessionState::ActiveDisconnected,
		RaSessionState::Offline};
	const RaSessionSignal signals[] = {RaSessionSignal::BeginLaunch,
		RaSessionSignal::LaunchSucceeded, RaSessionSignal::LaunchFailed,
		RaSessionSignal::Disconnected, RaSessionSignal::Reconnected,
		RaSessionSignal::SessionInvalidated, RaSessionSignal::StopGame};
	for (RaSessionState state : states) {
		for (RaSessionSignal signal : signals) {
			Check(TransitionRaSession(state, signal) ==
				ExpectedSessionTransition(state, signal),
				std::string("session signal from ") + Name(state));
		}
	}
}

void VerifyEveryMediaCombination()
{
	using namespace Xm8Ra;
	const RaSessionState states[] = {RaSessionState::Ready, RaSessionState::Starting,
		RaSessionState::Active, RaSessionState::ActiveDisconnected,
		RaSessionState::Offline};
	const RaDiskRole roles[] = {RaDiskRole::Anchor, RaDiskRole::Auxiliary};
	for (bool ra_enabled : {false, true}) {
		for (RaSessionState state : states) {
			for (RaDiskRole role : roles) {
				for (bool transaction_active : {false, true}) {
					for (bool same_hash : {false, true}) {
						for (bool same_game : {false, true}) {
							for (bool verified : {false, true}) {
								for (bool online : {false, true}) {
									RaDiskPolicyContext input;
									input.ra_enabled = ra_enabled;
									input.session_state = state;
									input.role = role;
									input.transaction_active = transaction_active;
									input.same_hash = same_hash;
									input.same_game = same_game;
									input.hash_verified_for_current_game = verified;
									input.network_available = online;
									Check(ClassifyRaDiskAction(input) ==
										ExpectedDiskAction(input),
										std::string("media matrix ") + Name(state));
								}
							}
						}
					}
				}
			}
		}
	}
}

void VerifyEntryAndTransactionTriggers()
{
	using namespace Xm8Ra;
	// Every UI input is required to submit the same normalized two-drive
	// request. Exercise the shared transaction gates for each entry trigger.
	const char *entries[] = {"menu", "open-both", "bank", "library", "m3u",
		"cli", "drop"};
	for (const char *entry : entries) {
		Check(CanApplySequentialRaMediaBatch(true, 2, false),
			std::string(entry) + " accepts a two-drive request");
		Check(CanAttachDrive2ToAnchorLaunch(true, true, false),
			std::string(entry) + " attaches Drive 2 to its anchor launch");
		Check(!CanAttachDrive2ToAnchorLaunch(true, true, true),
			std::string(entry) + " rejects a competing transaction");
	}

	const RaDiskTransactionPhase phases[] = {RaDiskTransactionPhase::Idle,
		RaDiskTransactionPhase::VerifyingDrive2,
		RaDiskTransactionPhase::ChangingActiveMedia,
		RaDiskTransactionPhase::CommittingVm, RaDiskTransactionPhase::RollingBack,
		RaDiskTransactionPhase::Committed, RaDiskTransactionPhase::Failed};
	for (RaDiskTransactionPhase phase : phases) {
		RaDiskTransactionState transaction;
		transaction.phase = phase;
		Check(transaction.Active() == (phase != RaDiskTransactionPhase::Idle),
			"transaction active-state matrix");
		transaction.Clear();
		Check(!transaction.Active() && !transaction.OwnsReset(),
			"transaction clear from every phase");
	}
	// A new registered Drive 1 after an Offline fallback must pass through the
	// normal reset path, regardless of whether it arrived by menu or D&D.
	Check(PlanDroppedReset(false, true, false) ==
		RaDroppedResetAction::NormalResetAndIdentifyDrive1,
		"Offline replacement re-identifies its new Drive 1 anchor");
}

void VerifyHardcoreAcrossEverySessionState()
{
	using namespace Xm8Ra;
	const RaSessionState states[] = {RaSessionState::Ready, RaSessionState::Starting,
		RaSessionState::Active, RaSessionState::ActiveDisconnected,
		RaSessionState::Offline};
	const RaRestrictedOperation operations[] = {RaRestrictedOperation::LoadState,
		RaRestrictedOperation::SaveState, RaRestrictedOperation::FullSpeed,
		RaRestrictedOperation::FastDisk, RaRestrictedOperation::Debugger};
	for (RaSessionState state : states) {
		RaSessionPolicyContext context;
		context.ra_enabled = true;
		context.selected_mode = RaPlayMode::Hardcore;
		context.session_state = state;
		for (RaRestrictedOperation operation : operations) {
			const bool expected = (state == RaSessionState::Starting ||
				state == RaSessionState::Active ||
				state == RaSessionState::ActiveDisconnected) ?
				operation == RaRestrictedOperation::SaveState : true;
			Check(IsRaOperationAllowed(context, operation) == expected,
				std::string("Hardcore operation matrix ") + Name(state));
		}
	}
}

} // namespace

int main()
{
	VerifyEverySessionSignal();
	VerifyEveryMediaCombination();
	VerifyEntryAndTransactionTriggers();
	VerifyHardcoreAcrossEverySessionState();
	if (failures != 0) return EXIT_FAILURE;
	std::cout << "RA media state matrix passed: " << assertions
		<< " assertions\n";
	return EXIT_SUCCESS;
}
