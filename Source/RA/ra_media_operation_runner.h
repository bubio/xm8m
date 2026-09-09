#ifndef XM8_RA_MEDIA_OPERATION_RUNNER_H
#define XM8_RA_MEDIA_OPERATION_RUNNER_H

#include "ra_media_operation.h"
#include <deque>
#include <functional>

namespace Xm8Ra { namespace MediaOperation {

struct Token {
    std::uint64_t request = 0;
    std::uint64_t generation = 0;
    std::uint64_t sequence = 0;
    bool operator==(const Token& other) const {
        return request == other.request && generation == other.generation && sequence == other.sequence;
    }
};

// Owned by one UI thread. Async callbacks must return to that thread before Post.
// Effects may Post synchronously; their results are dispatched after the entire
// current effect list returns, never recursively from inside an effect handler.
class Runner {
public:
    using Handler = std::function<void(Effect, Token)>;
    Runner() = default;
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    bool Start(std::uint64_t generation, Handler handler) {
        if (active_ || draining_ || !handler) return false;
        handler_ = std::move(handler);
        active_ = true;
        generation_ = generation;
        ++request_;
        draining_ = true;
        Execute(Reduce(state_, Event::Submit, Value::request));
        draining_ = false;
        Drain();
        return true;
    }
    void Post(Token token, Event event, Value value) {
        queue_.push_back({token, event, value});
        Drain();
    }
    // Used by the owner when this request starts a new session. The returned
    // token replaces the old outstanding result token; it does not cancel media.
    Token RebindSession(std::uint64_t generation) {
        if (!active_ || !waiting_) return {};
        generation_ = generation;
        pending_ = {request_, generation_, ++sequence_};
        return pending_;
    }
    bool Active() const { return active_; }
    State CurrentState() const { return state_; }

private:
    struct Message { Token token; Event event; Value value; };
    void Execute(const Step& step) {
        state_ = step.next; // Set the wait state before any possible callback.
        for (std::size_t i = 0; i < step.effect_count; ++i) {
            const Effect effect = step.effects[i];
            const Event expected = kEffectResults[static_cast<std::size_t>(effect)];
            Token token{request_, generation_, sequence_};
            if (expected != Event::Count) {
                token.sequence = ++sequence_;
                pending_ = token;
                expected_ = expected;
                waiting_ = true;
            }
            handler_(effect, token);
        }
        // Completion handlers cannot start a replacement until all effects of
        // the old request have finished. Release stack-capturing handlers here.
        if (state_ == State::Idle) {
            active_ = false;
            waiting_ = false;
            handler_ = {};
        }
    }
    void Drain() {
        if (draining_) return;
        draining_ = true;
        while (!queue_.empty()) {
            const Message message = queue_.front();
            queue_.pop_front();
            if (!active_ || !waiting_ || !(message.token == pending_)) continue;
            const Step step = Reduce(state_, message.event, message.value);
            if (message.event != expected_ || step.kind != CellKind::Normal) {
                handler_(Effect::ProtocolError, message.token);
                continue; // Keep the real outstanding result valid.
            }
            waiting_ = false;
            Execute(step);
        }
        draining_ = false;
    }
    Handler handler_;
    std::deque<Message> queue_;
    State state_ = State::Idle;
    Token pending_;
    Event expected_ = Event::Count;
    std::uint64_t request_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t sequence_ = 0;
    bool active_ = false;
    bool waiting_ = false;
    bool draining_ = false;
};

} }
#endif
