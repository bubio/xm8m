#ifndef XM8_RA_DISK_TRANSACTION_H
#define XM8_RA_DISK_TRANSACTION_H

#include <cstdint>

namespace Xm8Ra {

// A logged-in but unreachable service cannot identify a new Drive 1 medium.
// Start locally (Offline) instead of waiting for a request which cannot run.
inline bool ShouldForceLibraryOfflineForRa(bool ra_mode_enabled,
	bool service_available, bool logged_in, bool reachable)
{
	return ra_mode_enabled && service_available && logged_in && !reachable;
}

// A D&D mount always ends in one cold reset. When the same request has
// already begun identifying Drive 1, keep that pending launch instead of
// beginning it twice; otherwise use the normal reset, which re-identifies
// Drive 1 or keeps the running game's session.
enum class RaDroppedResetAction {
	NormalResetAndIdentifyDrive1,
	ResetVmPreservingPendingLaunch,
};

inline RaDroppedResetAction PlanDroppedReset(bool session_offline,
	bool anchor_launch_pending)
{
	if (anchor_launch_pending && !session_offline) {
		return RaDroppedResetAction::ResetVmPreservingPendingLaunch;
	}
	return RaDroppedResetAction::NormalResetAndIdentifyDrive1;
}

struct RaSynchronousAnchorCommitPlan {
	bool reset_vm_now = false;
	bool begin_session = false;
};

// A new RA title always starts from a cold VM. Entry points which reset by
// themselves (D&D, Library START) perform that reset after this commit.
inline RaSynchronousAnchorCommitPlan PlanSynchronousAnchorCommit(
	bool starts_new_anchor, bool caller_will_reset)
{
	return {starts_new_anchor && !caller_will_reset, starts_new_anchor};
}

// Identification, login and the retained-unlock sync share one deadline.
// After it the game runs without an RA session instead of waiting forever.
constexpr uint32_t kRaLaunchDeadlineMs = 15000;

inline bool IsRaLaunchDeadlineExpired(uint32_t started_ticks, uint32_t now_ticks)
{
	// Unsigned subtraction keeps this correct across SDL tick wrap-around.
	return static_cast<uint32_t>(now_ticks - started_ticks) >= kRaLaunchDeadlineMs;
}

} // namespace Xm8Ra

#endif
