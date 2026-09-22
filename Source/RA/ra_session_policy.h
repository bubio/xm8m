#ifndef XM8_RA_SESSION_POLICY_H
#define XM8_RA_SESSION_POLICY_H

#include "ra_session_state.h"

#include <cstdint>

namespace Xm8Ra {

enum class RaPlayMode {
	Casual,
	Hardcore,
};

enum class RaEffectiveMode {
	Normal,
	Casual,
	Hardcore,
	Offline,
};

enum class RaRestrictedOperation {
	LoadState,
	SaveState,
	FullSpeed,
	FastDisk,
	Debugger,
};

enum class RaPauseDecision {
	Run,
	Pause,
	CheckHardcore,
};

// Converts a level-triggered host pause request into controlled Hardcore pause
// checks. An accepted pause stays latched. A denied pause runs the requested
// number of emulated frames before checking again.
class RaPauseRequestGate {
public:
	RaPauseDecision Evaluate(bool pause_requested, bool hardcore)
	{
		if (!pause_requested) {
			Reset();
			return RaPauseDecision::Run;
		}

		if (!request_active_) {
			request_active_ = true;
			hardcore_checked_ = false;
			pause_allowed_ = false;
			denial_notified_ = false;
			frames_until_recheck_ = 0;
		}

		if (!hardcore) {
			hardcore_checked_ = false;
			pause_allowed_ = true;
			denial_notified_ = false;
			frames_until_recheck_ = 0;
			return RaPauseDecision::Pause;
		}
		if (!hardcore_checked_) {
			return RaPauseDecision::CheckHardcore;
		}
		return pause_allowed_ ? RaPauseDecision::Pause : RaPauseDecision::Run;
	}

	RaPauseDecision ResolveHardcore(bool allowed, uint32_t frames_remaining = 0)
	{
		if (!request_active_) {
			return RaPauseDecision::Run;
		}
		hardcore_checked_ = true;
		pause_allowed_ = allowed;
		frames_until_recheck_ = allowed ? 0 :
			(frames_remaining != 0 ? frames_remaining : 1);
		return allowed ? RaPauseDecision::Pause : RaPauseDecision::Run;
	}

	void AdvanceFrame()
	{
		if (!request_active_ || pause_allowed_ || frames_until_recheck_ == 0) {
			return;
		}
		if (--frames_until_recheck_ == 0) {
			hardcore_checked_ = false;
		}
	}

	bool TakeDenialNotification()
	{
		if (denial_notified_) return false;
		denial_notified_ = true;
		return true;
	}

	void Reset()
	{
		request_active_ = false;
		hardcore_checked_ = false;
		pause_allowed_ = false;
		denial_notified_ = false;
		frames_until_recheck_ = 0;
	}

private:
	bool request_active_ = false;
	bool hardcore_checked_ = false;
	bool pause_allowed_ = false;
	bool denial_notified_ = false;
	uint32_t frames_until_recheck_ = 0;
};

struct RaSessionPolicyContext {
	bool ra_enabled = false;
	RaPlayMode selected_mode = RaPlayMode::Casual;
	RaSessionState session_state = RaSessionState::Ready;
};

inline RaEffectiveMode EffectiveRaMode(const RaSessionPolicyContext& context)
{
	if (!context.ra_enabled) {
		return RaEffectiveMode::Normal;
	}
	if (context.session_state == RaSessionState::Offline) {
		return RaEffectiveMode::Offline;
	}
	if (context.session_state == RaSessionState::Starting ||
		context.session_state == RaSessionState::Active ||
		context.session_state == RaSessionState::ActiveDisconnected) {
		return context.selected_mode == RaPlayMode::Hardcore ?
			RaEffectiveMode::Hardcore : RaEffectiveMode::Casual;
	}
	// RA is enabled, but no game session is currently enforcing a play mode.
	return RaEffectiveMode::Normal;
}

inline bool IsRaOperationAllowed(const RaSessionPolicyContext& context,
	RaRestrictedOperation operation)
{
	const RaEffectiveMode mode = EffectiveRaMode(context);
	if (mode == RaEffectiveMode::Hardcore) {
		// Hardcore states may be created for achievement debugging, but never
		// loaded. All other assisted operations remain unavailable.
		return operation == RaRestrictedOperation::SaveState;
	}
	if (mode == RaEffectiveMode::Casual) {
		return operation != RaRestrictedOperation::FastDisk &&
			operation != RaRestrictedOperation::Debugger;
	}
	return true;
}

inline bool IsRaOnlineSession(const RaSessionPolicyContext& context)
{
	const RaEffectiveMode mode = EffectiveRaMode(context);
	return mode == RaEffectiveMode::Casual ||
		mode == RaEffectiveMode::Hardcore;
}

inline bool IsRaHardcoreSession(const RaSessionPolicyContext& context)
{
	return EffectiveRaMode(context) == RaEffectiveMode::Hardcore;
}

inline bool CanLoadHardcoreDebugState(const RaSessionPolicyContext& context)
{
	return EffectiveRaMode(context) == RaEffectiveMode::Casual;
}

inline bool MustWaitForRaSession(const RaSessionPolicyContext& context)
{
	return context.ra_enabled &&
		context.session_state == RaSessionState::Starting;
}

inline bool MustResetWhenEnablingRa(RaPlayMode selected_mode)
{
	return selected_mode == RaPlayMode::Hardcore;
}

inline bool MustResetWhenChangingRaPlayMode(RaPlayMode current_mode,
	RaPlayMode target_mode)
{
	return current_mode != RaPlayMode::Hardcore &&
		target_mode == RaPlayMode::Hardcore;
}

} // namespace Xm8Ra

#endif
