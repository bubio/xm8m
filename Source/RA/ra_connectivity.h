#ifndef XM8_RA_CONNECTIVITY_H
#define XM8_RA_CONNECTIVITY_H

#include "ra_session_state.h"

#include <cstdint>
#include <memory>

namespace Xm8Ra {

enum class RaReachabilityState {
	Unknown,
	Reachable,
	Unreachable,
};

struct RaReachabilityTransition {
	bool has_signal = false;
	RaSessionSignal signal = RaSessionSignal::Disconnected;
};

class RaConnectivityTracker {
public:
	RaReachabilityTransition Observe(RaReachabilityState state)
	{
		RaReachabilityTransition result;
		if (state == RaReachabilityState::Unknown) {
			return result;
		}
		if (state_ != RaReachabilityState::Unknown && state_ != state) {
			result.has_signal = true;
			result.signal = state == RaReachabilityState::Reachable ?
				RaSessionSignal::Reconnected : RaSessionSignal::Disconnected;
		}
		state_ = state;
		return result;
	}

	RaReachabilityState State() const { return state_; }

private:
	RaReachabilityState state_ = RaReachabilityState::Unknown;
};

// Transport failures do not necessarily change interface reachability. Keep a
// separate bounded retry clock for durable unlock submissions so endpoint and
// DNS failures are retried while the interface remains online.
class RaUnlockRetryBackoff {
public:
	static constexpr uint32_t kInitialDelayMs = 1000;
	static constexpr uint32_t kMaximumDelayMs = 60000;

	void Reset()
	{
		scheduled_ = false;
		deadline_ = 0;
		delay_ = kInitialDelayMs;
	}

	void Schedule(uint32_t now)
	{
		if (!scheduled_) {
			deadline_ = now + delay_;
			scheduled_ = true;
		}
	}

	void RequestImmediate(uint32_t now)
	{
		deadline_ = now;
		scheduled_ = true;
	}

	bool IsDue(uint32_t now) const
	{
		return scheduled_ && static_cast<int32_t>(now - deadline_) >= 0;
	}

	void RecordAttempt(uint32_t now)
	{
		if (delay_ < kMaximumDelayMs / 2) {
			delay_ *= 2;
		}
		else {
			delay_ = kMaximumDelayMs;
		}
		deadline_ = now + delay_;
		scheduled_ = true;
	}

	bool IsScheduled() const { return scheduled_; }
	uint32_t DelayMs() const { return delay_; }

private:
	bool scheduled_ = false;
	uint32_t deadline_ = 0;
	uint32_t delay_ = kInitialDelayMs;
};

class RaConnectivityMonitor {
public:
	virtual ~RaConnectivityMonitor() = default;
	virtual RaReachabilityState Poll() = 0;
};

std::unique_ptr<RaConnectivityMonitor> CreatePlatformRaConnectivityMonitor();

} // namespace Xm8Ra

#endif
