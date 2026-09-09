#include "ra_media_operation_runner.h"
#include <cstdlib>
#include <iostream>

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
    std::cout << "ra_media_operation_runner_test: PASS\n";
}
