#ifndef XM8_RA_MEDIA_CHANGE_POLICY_H
#define XM8_RA_MEDIA_CHANGE_POLICY_H

#include "ra_session_state.h"

#include <cstdint>
#include <string>

namespace Xm8Ra {

// Drive 1 is the rcheevos active-media anchor. Drive 2 is validated against
// the anchor game, but never changes rc_client's active media.
enum class RaDiskRole { Anchor, Auxiliary };

enum class RaDiskAction {
	MountLocal,
	// Transitional spellings for call sites which do not need to know the
	// drive role. Both mean exactly the same local VM commit.
	MountNormal = MountLocal,
	MountAuxiliary = MountLocal,
	BeginAnchorLaunch,
	ChangeAnchorMedia,
	RestartAnchorLaunch,
	VerifyAuxiliary,
	EnterOfflineAndMount,
	RejectBusy,
};

enum class RaDrive2MountAction { Unchanged, Close, OpenBank1 };

struct RaMediaMountPlan {
	bool wait_for_ra_approval = false;
	RaDrive2MountAction drive2_action_after_approval =
		RaDrive2MountAction::Unchanged;
};

// Kept at the UI boundary only while Open Both is normalized to an explicit
// two-drive request. It contains no mode-specific policy.
inline RaMediaMountPlan PlanRaMediaMount(bool wait_for_ra_approval,
	bool open_pair, int target_banks)
{
	RaMediaMountPlan plan;
	plan.wait_for_ra_approval = wait_for_ra_approval;
	if (open_pair) plan.drive2_action_after_approval = target_banks > 1 ?
		RaDrive2MountAction::OpenBank1 : RaDrive2MountAction::Close;
	return plan;
}

inline bool RaMediaRollbackRestoredAllDrives(bool open_pair,
	bool drive1_restored, bool drive2_restored)
{
	return drive1_restored && (!open_pair || drive2_restored);
}

// This is deliberately not a UI or Hardcore policy. It contains only facts
// which change the media decision, so every input path receives the same
// answer for the same two hashes and connection state.
struct RaDiskPolicyContext {
	bool ra_enabled = false;
	RaSessionState session_state = RaSessionState::Ready;
	RaDiskRole role = RaDiskRole::Anchor;
	bool transaction_active = false;
	bool same_hash = false;
	bool same_game = false;
	bool hash_verified_for_current_game = false;
	bool network_available = false;
	std::string active_hash;
	std::string target_hash;
	int64_t active_game_id = 0;
	int64_t target_game_id = 0;
};

inline RaDiskAction ClassifyRaDiskAction(const RaDiskPolicyContext& input)
{
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

inline bool MustPersistRaLaunchProfileForMount(RaDiskRole role)
{
	return role == RaDiskRole::Anchor;
}

inline bool CanEjectRaMedia(int drive, bool game_load_pending,
	bool media_change_pending, bool auxiliary_change_pending)
{
	// Only an untouched auxiliary drive is independent of the anchor request.
	return !auxiliary_change_pending &&
		(drive == 1 || (!game_load_pending && !media_change_pending));
}

// A raw D88 drop remains an explicit two-drive request, including the
// single-bank form which closes Drive 2. All other callers supply their
// desired Drive 1/2 assignment explicitly.
inline bool ShouldOpenDroppedD88AsPair(bool is_playlist)
{
	return !is_playlist;
}

} // namespace Xm8Ra

#endif
