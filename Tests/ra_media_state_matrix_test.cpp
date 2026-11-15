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

// Independent statement of the specification table (45 rev. 1.2/1.3).
// Play mode, connectivity and the entry point are not inputs.
Xm8Ra::RaDiskAction ExpectedDiskAction(
	const Xm8Ra::RaDiskPolicyContext& input)
{
	using namespace Xm8Ra;
	if (!input.ra_enabled) return RaDiskAction::MountLocal;
	if (input.role == RaDiskRole::Auxiliary) return RaDiskAction::MountLocal;
	if (input.session_state == RaSessionState::Offline)
		return RaDiskAction::MountLocal;
	if (input.session_state == RaSessionState::Starting)
		return RaDiskAction::RejectBusy;
	if (input.session_state == RaSessionState::Ready)
		return RaDiskAction::BeginAnchorLaunch;
	if (input.same_hash) return RaDiskAction::MountLocal;
	if (input.relation == RaMediaRelation::CurrentGame)
		return RaDiskAction::MountSameGameAnchor;
	if (input.relation == RaMediaRelation::OtherGame)
		return RaDiskAction::RestartAnchorLaunch;
	return RaDiskAction::EnterOfflineAndMount;
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
	const RaMediaRelation relations[] = {RaMediaRelation::CurrentGame,
		RaMediaRelation::OtherGame, RaMediaRelation::Unregistered};
	for (bool ra_enabled : {false, true}) {
		for (RaSessionState state : states) {
			for (RaDiskRole role : roles) {
				for (bool same_hash : {false, true}) {
					for (RaMediaRelation relation : relations) {
						RaDiskPolicyContext input;
						input.ra_enabled = ra_enabled;
						input.session_state = state;
						input.role = role;
						input.same_hash = same_hash;
						input.relation = relation;
						const RaDiskAction action = ClassifyRaDiskAction(input);
						Check(action == ExpectedDiskAction(input),
							std::string("media matrix ") + Name(state));
						// Drive 2 and RA OFF never reach RA.
						if (!ra_enabled || role == RaDiskRole::Auxiliary) {
							Check(action == RaDiskAction::MountLocal,
								std::string("local-only mount ") + Name(state));
						}
					}
				}
			}
		}
	}
}

void VerifyEntryAndResetTriggers()
{
	using namespace Xm8Ra;
	// Every entry point reduces to (media per drive, reset or not). The only
	// reset planning left is whether a pending launch survives a D&D reset.
	for (bool offline : {false, true}) {
		for (bool pending : {false, true}) {
			const RaDroppedResetAction expected = pending && !offline ?
				RaDroppedResetAction::ResetVmPreservingPendingLaunch :
				RaDroppedResetAction::NormalResetAndIdentifyDrive1;
			Check(PlanDroppedReset(offline, pending) == expected,
				"D&D reset planning matrix");
		}
	}
	for (bool starts : {false, true}) {
		for (bool caller_resets : {false, true}) {
			const RaSynchronousAnchorCommitPlan plan =
				PlanSynchronousAnchorCommit(starts, caller_resets);
			Check(plan.begin_session == starts,
				"a session begins exactly when a new title starts");
			Check(plan.reset_vm_now == (starts && !caller_resets),
				"a new title is cold reset exactly once");
		}
	}
	// Drive 1 must stay while it is identified; Drive 2 is always free.
	for (bool pending : {false, true}) {
		Check(CanEjectRaMedia(0, pending) == !pending, "Drive 1 eject matrix");
		Check(CanEjectRaMedia(1, pending), "Drive 2 eject matrix");
	}
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
	VerifyEntryAndResetTriggers();
	VerifyHardcoreAcrossEverySessionState();
	if (failures != 0) return EXIT_FAILURE;
	std::cout << "RA media state matrix passed: " << assertions
		<< " assertions\n";
	return EXIT_SUCCESS;
}
