#ifndef XM8_RA_MEDIA_CHANGE_POLICY_H
#define XM8_RA_MEDIA_CHANGE_POLICY_H

#include "ra_session_state.h"

#include <cstdint>

namespace Xm8Ra {

// Drive 1 alone identifies the RA game. Drive 2 never takes part in an RA
// decision: any local medium may be mounted there (save disks included).
enum class RaDiskRole { Anchor, Auxiliary };

// What local knowledge (the RA hash list fetched after login and the Library
// records) says about a Drive 1 medium while a game session is running.
// Anything which cannot be confirmed locally is Unregistered.
enum class RaMediaRelation { CurrentGame, OtherGame, Unregistered };

inline RaMediaRelation ClassifyRaMediaRelation(bool known_registered,
	uint32_t media_ra_game_id, uint32_t current_ra_game_id)
{
	if (!known_registered || media_ra_game_id == 0) {
		return RaMediaRelation::Unregistered;
	}
	return current_ra_game_id != 0 && media_ra_game_id == current_ra_game_id ?
		RaMediaRelation::CurrentGame : RaMediaRelation::OtherGame;
}

enum class RaDiskAction {
	// Mount only. RA is not involved.
	MountLocal,
	// No session yet: cold reset and identify the new Drive 1 medium.
	BeginAnchorLaunch,
	// Another disk of the running game: mount and keep the session.
	MountSameGameAnchor,
	// A different registered game: cold reset and start its session.
	RestartAnchorLaunch,
	// Not a medium of the running game: end the session, then mount.
	EnterOfflineAndMount,
	// Drive 1 is being identified; a second Drive 1 request must wait.
	RejectBusy,
};

enum class RaDrive2MountAction { Unchanged, Close, OpenBank1 };

// "Open 1&2" of one D88: bank 1 goes to Drive 2, or Drive 2 is closed when
// the image has a single bank.
inline RaDrive2MountAction PlanRaPairDrive2(bool open_pair, int target_banks)
{
	if (!open_pair) return RaDrive2MountAction::Unchanged;
	return target_banks > 1 ? RaDrive2MountAction::OpenBank1 :
		RaDrive2MountAction::Close;
}

// Only facts which change the media decision. Play mode, connectivity and the
// entry point are deliberately absent: they never alter the answer.
struct RaDiskPolicyContext {
	bool ra_enabled = false;
	RaSessionState session_state = RaSessionState::Ready;
	RaDiskRole role = RaDiskRole::Anchor;
	bool same_hash = false;
	RaMediaRelation relation = RaMediaRelation::Unregistered;
};

inline RaDiskAction ClassifyRaDiskAction(const RaDiskPolicyContext& input)
{
	if (!input.ra_enabled || input.role == RaDiskRole::Auxiliary) {
		return RaDiskAction::MountLocal;
	}
	switch (input.session_state) {
	case RaSessionState::Offline:
		return RaDiskAction::MountLocal;
	case RaSessionState::Starting:
		return RaDiskAction::RejectBusy;
	case RaSessionState::Ready:
		return RaDiskAction::BeginAnchorLaunch;
	case RaSessionState::Active:
	case RaSessionState::ActiveDisconnected:
		break;
	}
	if (input.same_hash) return RaDiskAction::MountLocal;
	switch (input.relation) {
	case RaMediaRelation::CurrentGame:
		return RaDiskAction::MountSameGameAnchor;
	case RaMediaRelation::OtherGame:
		return RaDiskAction::RestartAnchorLaunch;
	case RaMediaRelation::Unregistered:
		break;
	}
	return RaDiskAction::EnterOfflineAndMount;
}

inline bool MustPersistRaLaunchProfileForMount(RaDiskRole role)
{
	return role == RaDiskRole::Anchor;
}

// Drive 2 is always independent. Drive 1 must stay while it is identified.
inline bool CanEjectRaMedia(int drive, bool game_load_pending)
{
	return drive == 1 || !game_load_pending;
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
