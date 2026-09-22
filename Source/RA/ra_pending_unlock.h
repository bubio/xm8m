#ifndef XM8_RA_PENDING_UNLOCK_H
#define XM8_RA_PENDING_UNLOCK_H

#include <cstdint>
#include <string>
#include <vector>

namespace Xm8Ra {

enum class RaPendingUnlockStatus {
	Pending = 0,
	Held = 1,
};

struct RaPendingUnlockRecord {
	int64_t id = 0;
	std::string account;
	uint32_t achievement_id = 0;
	bool hardcore = false;
	std::string game_hash;
	int64_t unlocked_at = 0;
	RaPendingUnlockStatus status = RaPendingUnlockStatus::Pending;
	uint32_t attempt_count = 0;
	std::string last_error;
};

class RaPendingUnlockStore {
public:
	virtual ~RaPendingUnlockStore() = default;
	virtual bool EnqueuePendingUnlock(const RaPendingUnlockRecord& record,
		int64_t *record_id, std::string *error) = 0;
	virtual bool ListPendingUnlocks(const std::string& account,
		std::vector<RaPendingUnlockRecord> *records, std::string *error) = 0;
	virtual bool MarkPendingUnlockAttempt(int64_t record_id,
		RaPendingUnlockStatus status, const std::string& last_error,
		std::string *error) = 0;
	virtual bool RemovePendingUnlock(int64_t record_id,
		std::string *error) = 0;
	virtual bool RemovePendingUnlocksForAccount(const std::string& account,
		std::string *error) = 0;
	virtual bool CountPendingUnlocks(const std::string& account,
		size_t *count, std::string *error) = 0;
	virtual bool RecoveryRequired(std::string *reason) const
	{
		if (reason != nullptr) reason->clear();
		return false;
	}
	virtual bool ConfirmDiscardRecovery(std::string *error)
	{
		if (error != nullptr) error->clear();
		return true;
	}
};

enum class RaUnlockSyncState {
	None,
	Pending,
	Succeeded,
	Failed,
};

struct RaUnlockSyncSnapshot {
	RaUnlockSyncState state = RaUnlockSyncState::None;
	size_t remaining = 0;
	std::string message;
};

} // namespace Xm8Ra

#endif
