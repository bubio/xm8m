#include "ra_session_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		failures++;
	}
}

} // namespace

int main()
{
	using namespace Xm8Ra;
	RaSessionPolicyContext context;

	Check(EffectiveRaMode(context) == RaEffectiveMode::Normal,
		"RA disabled is Normal");
	context.ra_enabled = true;
	context.selected_mode = RaPlayMode::Hardcore;
	Check(EffectiveRaMode(context) == RaEffectiveMode::Normal,
		"Hardcore preference without a game does not restrict Normal operation");

	context.session_state = RaSessionState::Starting;
	Check(IsRaHardcoreSession(context), "Hardcore applies while starting");
	Check(IsRaOperationAllowed(context, RaRestrictedOperation::SaveState),
		"Hardcore allows creating debugging states");
	for (RaRestrictedOperation operation : {
		RaRestrictedOperation::LoadState,
		RaRestrictedOperation::FullSpeed,
		RaRestrictedOperation::FastDisk,
		RaRestrictedOperation::Debugger}) {
		Check(!IsRaOperationAllowed(context, operation),
			"Hardcore rejects every restricted operation");
	}
	Check(MustWaitForRaSession(context),
		"Starting session stops the VM until RA is ready");

	context.selected_mode = RaPlayMode::Casual;
	Check(CanLoadHardcoreDebugState(context),
		"Casual is the only mode that can load Hardcore debug states");
	Check(IsRaOperationAllowed(context, RaRestrictedOperation::LoadState),
		"Casual allows state loading");
	Check(IsRaOperationAllowed(context, RaRestrictedOperation::FullSpeed),
		"Casual allows full speed");
	Check(!IsRaOperationAllowed(context, RaRestrictedOperation::FastDisk),
		"Casual rejects pseudo fast disk");
	Check(!IsRaOperationAllowed(context, RaRestrictedOperation::Debugger),
		"Casual rejects debugger operations");

	context.selected_mode = RaPlayMode::Hardcore;
	context.session_state = RaSessionState::ActiveDisconnected;
	Check(IsRaHardcoreSession(context),
		"disconnect does not weaken an active Hardcore session");
	Check(!IsRaOperationAllowed(context, RaRestrictedOperation::FullSpeed),
		"disconnect does not make full speed available in Hardcore");
	Check(!CanLoadHardcoreDebugState(context),
		"Hardcore cannot call the lower-level debug state loader");
	context.session_state = RaSessionState::Offline;
	Check(!MustWaitForRaSession(context),
		"Offline fallback releases the startup VM wait");
	Check(EffectiveRaMode(context) == RaEffectiveMode::Offline,
		"invalidated session is Offline");
	Check(IsRaOperationAllowed(context, RaRestrictedOperation::LoadState),
		"Offline allows its dedicated states");
	Check(IsRaOperationAllowed(context, RaRestrictedOperation::FullSpeed),
		"Offline restores normal speed policy");
	Check(!CanLoadHardcoreDebugState(context),
		"Offline cannot load a Hardcore debug state");
	context.ra_enabled = false;
	context.session_state = RaSessionState::Starting;
	Check(!MustWaitForRaSession(context),
		"disabled RA never stops the VM for startup");
	Check(!CanLoadHardcoreDebugState(context),
		"Normal mode cannot load a Hardcore debug state");

	Check(MustResetWhenEnablingRa(RaPlayMode::Hardcore),
		"enabling RA with Hardcore selected requires a cold reset");
	Check(!MustResetWhenEnablingRa(RaPlayMode::Casual),
		"enabling RA with Casual selected preserves the running VM");
	Check(MustResetWhenChangingRaPlayMode(RaPlayMode::Casual,
		RaPlayMode::Hardcore),
		"Casual to Hardcore requires a full reset");
	Check(!MustResetWhenChangingRaPlayMode(RaPlayMode::Hardcore,
		RaPlayMode::Casual),
		"Hardcore to Casual preserves the running VM");

	RaPauseRequestGate pause_gate;
	Check(pause_gate.Evaluate(false, true) == RaPauseDecision::Run,
		"no host pause request keeps Hardcore running");
	Check(pause_gate.Evaluate(true, true) ==
		RaPauseDecision::CheckHardcore,
		"new Hardcore pause request requires one rcheevos check");
	Check(pause_gate.ResolveHardcore(false, 2) == RaPauseDecision::Run,
		"denied Hardcore pause keeps the VM running");
	Check(pause_gate.Evaluate(true, true) == RaPauseDecision::Run,
		"denied request waits for the required emulated frames");
	Check(pause_gate.TakeDenialNotification() &&
		!pause_gate.TakeDenialNotification(),
		"a held denied request emits only one notification");
	pause_gate.AdvanceFrame();
	Check(pause_gate.Evaluate(true, true) == RaPauseDecision::Run,
		"partial frame countdown does not recheck early");
	pause_gate.AdvanceFrame();
	Check(pause_gate.Evaluate(true, true) ==
		RaPauseDecision::CheckHardcore,
		"denied request rechecks after enough emulated frames");
	Check(pause_gate.Evaluate(false, true) == RaPauseDecision::Run,
		"clearing a denied request rearms the pause gate");
	Check(pause_gate.Evaluate(true, true) ==
		RaPauseDecision::CheckHardcore,
		"next distinct Hardcore pause request checks again");
	Check(pause_gate.ResolveHardcore(true) == RaPauseDecision::Pause,
		"accepted Hardcore pause stops the VM");
	Check(pause_gate.Evaluate(true, true) == RaPauseDecision::Pause,
		"held accepted request remains paused without another check");
	Check(pause_gate.Evaluate(true, false) == RaPauseDecision::Pause,
		"leaving Hardcore cannot strand an active pause request");
	Check(pause_gate.Evaluate(true, true) ==
		RaPauseDecision::CheckHardcore,
		"re-entering Hardcore while paused requires a fresh check");

	RaPauseRequestGate zero_frame_pause_gate;
	Check(zero_frame_pause_gate.Evaluate(true, true) ==
		RaPauseDecision::CheckHardcore &&
		zero_frame_pause_gate.ResolveHardcore(false) == RaPauseDecision::Run,
		"denial without a frame hint still keeps the VM running");
	zero_frame_pause_gate.AdvanceFrame();
	Check(zero_frame_pause_gate.Evaluate(true, true) ==
		RaPauseDecision::CheckHardcore,
		"zero frame hint falls back to rechecking after one frame");

	if (failures != 0) return EXIT_FAILURE;
	std::cout << "RA session policy tests passed\n";
	return EXIT_SUCCESS;
}
