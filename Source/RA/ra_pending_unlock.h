#ifndef XM8_RA_PENDING_UNLOCK_H
#define XM8_RA_PENDING_UNLOCK_H

#include "ra_hmac_sha256.h"

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
	// HMAC over the fields which decide what is submitted. See
	// ComputeRaPendingUnlockMac.
	std::string mac;
};

// The outbox is a plain SQLite file, so each record carries a MAC keyed by the
// account's RA API token (kept in the OS credential store). Editing a record,
// or adding one, without that token makes the record unverifiable, and an
// unverifiable record is never submitted. Forging a record therefore needs
// the same secret as submitting the unlock directly.
inline std::string ComputeRaPendingUnlockMac(const std::string& api_token,
	const RaPendingUnlockRecord& record)
{
	if (api_token.empty()) return std::string();
	std::string message = "xm8m-ra-pending-unlock-v1\n";
	message += std::to_string(record.account.size());
	message += ':';
	message += record.account;
	message += '\n';
	message += std::to_string(record.achievement_id);
	message += '\n';
	message += record.hardcore ? '1' : '0';
	message += '\n';
	message += record.game_hash;
	message += '\n';
	message += std::to_string(record.unlocked_at);
	return RaHexDigest(RaHmacSha256(api_token, message));
}

inline bool VerifyRaPendingUnlockMac(const std::string& api_token,
	const RaPendingUnlockRecord& record)
{
	const std::string expected = ComputeRaPendingUnlockMac(api_token, record);
	return !expected.empty() && RaConstantTimeEquals(expected, record.mac);
}

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
	// Replace the MAC only (the API token of the account was reissued).
	virtual bool UpdatePendingUnlockMac(int64_t record_id,
		const std::string& mac, std::string *error) = 0;
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
	// Records still to be submitted.
	size_t remaining = 0;
	// Records kept but never submitted: the server refused them for good, or
	// their MAC does not verify. They do not block a game session.
	size_t held = 0;
	std::string message;
};

} // namespace Xm8Ra

#endif
