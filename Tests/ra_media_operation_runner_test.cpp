#include "ra_media_operation_runner.h"
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace Xm8Ra::MediaOperation;
static void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

int main()
{
    Runner synchronous;
    int depth = 0, applies = 0, completions = 0;
    bool released = false;
    auto handler = [&](Effect effect, Token token) {
        Check(++depth == 1, "synchronous replies must not recursively invoke effects");
        switch (effect) {
        case Effect::AcceptPrepare:
            Check(synchronous.CurrentState() == State::Prepare, "wait state is visible before callback");
            synchronous.Post(token, Event::Prepared, Value::ok);
            synchronous.Post(token, Event::Prepared, Value::ok); // Same completion twice.
            break;
        case Effect::DecidePlan:
            synchronous.Post(token, Event::PlanResult, Value::local);
            break;
        case Effect::ApplyVm:
            ++applies;
            synchronous.Post(token, Event::CommitResult, Value::ok);
            synchronous.Post(token, Event::CommitResult, Value::failed); // Late contradictory result.
            break;
        case Effect::DecideFinish:
            synchronous.Post(token, Event::FinishPlan, Value::none);
            break;
        case Effect::CompleteSuccess:
            ++completions;
            Check(synchronous.Active(), "request is owned throughout completion effects");
            Check(!synchronous.Start(7, [](Effect, Token) {}), "completion cannot reenter a new request");
            released = true;
            break;
        default: Check(false, "unexpected effect in local request");
        }
        --depth;
    };
    Check(synchronous.Start(7, handler), "accept synchronous request");
    Check(released && !synchronous.Active() && applies == 1 && completions == 1,
        "duplicate replies do not repeat application or completion");
    Check(synchronous.Start(7, handler), "next request can start after completion returns");
    Check(applies == 2 && completions == 2, "new request has its own completion");

    Runner delayed;
    Token outstanding;
    int rejected = 0, protocol_errors = 0, prepares = 0;
    auto delayed_handler = [&](Effect effect, Token token) {
        if (effect == Effect::AcceptPrepare) { outstanding = token; ++prepares; }
        else if (effect == Effect::RejectLocal) ++rejected;
        else if (effect == Effect::ProtocolError) ++protocol_errors;
        else Check(false, "unexpected delayed effect");
    };
    Check(delayed.Start(10, delayed_handler), "accept delayed request");
    const Token old = outstanding;
    Check(!delayed.Start(10, delayed_handler) && prepares == 1, "Busy leaves original handler and request intact");
    for (int field = 0; field < 3; ++field) {
        Token wrong = old;
        if (field == 0) ++wrong.request;
        if (field == 1) ++wrong.generation;
        if (field == 2) ++wrong.sequence;
        delayed.Post(wrong, Event::Prepared, Value::failed);
    }
    Check(delayed.Active() && rejected == 0 && protocol_errors == 0, "foreign results are ignored");
    const Token current = delayed.RebindSession(11);
    delayed.Post(old, Event::Prepared, Value::failed);
    Check(delayed.Active() && rejected == 0, "old session result cannot advance rebound request");
    delayed.Post(current, Event::CommitResult, Value::ok);
    Check(protocol_errors == 1 && delayed.Active(), "wrong event detects protocol error without consuming real result");
    delayed.Post(current, Event::Prepared, Value::failed);
    Check(!delayed.Active() && rejected == 1, "rebound token completes same media request");
    delayed.Post(current, Event::Prepared, Value::failed);
    Check(rejected == 1, "completion after retirement is ignored");
    Check(delayed.Start(11, delayed_handler), "start following request");
    delayed.Post(current, Event::Prepared, Value::failed);
    Check(delayed.Active() && rejected == 1, "retired request cannot complete a newer request");
    delayed.Post(outstanding, Event::Prepared, Value::failed);
    Check(!delayed.Active() && rejected == 2, "current request completes exactly once");
    Check(delayed.Start(12, delayed_handler), "start request to cancel");
    const auto cancelled = outstanding;
    Check(delayed.Cancel() && !delayed.Cancel(), "cancellation retires one active request");
    delayed.Post(cancelled, Event::Prepared, Value::failed);
    Check(rejected == 2 && !delayed.Active(), "cancelled request produces no late completion");
    Check(delayed.Start(12, delayed_handler), "next request starts after cancellation");
    delayed.Post(cancelled, Event::Prepared, Value::failed);
    Check(rejected == 2 && delayed.Active(), "cancelled token cannot complete replacement");
    delayed.Post(outstanding, Event::Prepared, Value::failed);
    Check(rejected == 3 && !delayed.Active(), "replacement owns its completion");

    Runner during_effect;
    int unexpected = 0;
    Check(during_effect.Start(1, [&](Effect effect, Token token) {
        if (effect == Effect::AcceptPrepare)
            during_effect.Post(token, Event::Prepared, Value::ok);
        else if (effect == Effect::DecidePlan) {
            during_effect.Post(token, Event::PlanResult, Value::local);
            Check(during_effect.Cancel(), "cancel from an executing handler");
            // Even a reply queued after cancellation is retired.
            during_effect.Post(token, Event::PlanResult, Value::local);
        }
        else ++unexpected;
    }), "accept request before synchronous cancellation");
    Check(!during_effect.Active() && unexpected == 0, "cancel prevents queued VM application");

    Runner effect_list;
    Check(effect_list.Start(1, [&](Effect effect, Token token) {
        if (effect == Effect::AcceptPrepare) effect_list.Post(token, Event::Prepared, Value::ok);
        else if (effect == Effect::DecidePlan) effect_list.Post(token, Event::PlanResult, Value::fallback);
        else if (effect == Effect::EnterOffline) {
            // Simulate explicit lifecycle cancellation between two effects.
            // Ordinary Offline fallback does not call Cancel.
            effect_list.Cancel();
        }
        else ++unexpected;
    }), "start request with multiple effects in one transition");
    Check(!effect_list.Active() && unexpected == 0, "cancellation skips remaining effects in current transition");

    Runner lifetime;
    auto owner = std::make_shared<int>(42);
    std::weak_ptr<int> weak = owner;
    Check(lifetime.Start(1, [owner](Effect, Token) {}), "retain async owner while waiting");
    owner.reset();
    Check(!weak.expired(), "pending handler owns its data");
    lifetime.Cancel();
    Check(weak.expired(), "cancellation releases handler and request data");
    std::cout << "ra_media_operation_runner_test: PASS\n";
}
