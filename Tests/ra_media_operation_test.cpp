#include "ra_media_operation.h"

#include <cstdlib>
#include <iostream>
#include <vector>

using namespace Xm8Ra::MediaOperation;

static void Check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

struct Trace {
    State state = State::Idle;
    std::vector<Effect> effects;
    void Send(Event event, Value value) {
        const Step step = Reduce(state, event, value);
        Check(step.kind == CellKind::Normal, "expected a normal transition");
        Check(step.transition_id != 0, "normal transition has a design ID");
        state = step.next;
        effects.insert(effects.end(), step.effects.begin(), step.effects.begin() + step.effect_count);
    }
    void Expect(std::initializer_list<Effect> wanted) {
        Check(effects == std::vector<Effect>(wanted), "effect order matches the required behavior");
        Check(state == State::Idle, "request completed");
    }
};

int main()
{
    // A local failure is not an Offline mount and must not touch the VM.
    Trace invalid;
    invalid.Send(Event::Submit, Value::request);
    invalid.Send(Event::Prepared, Value::failed);
    invalid.Expect({Effect::AcceptPrepare, Effect::RejectLocal});

    // RA OFF drop: no RA effects, exactly one reset after application.
    Trace local;
    local.Send(Event::Submit, Value::request);
    local.Send(Event::Prepared, Value::ok);
    local.Send(Event::PlanResult, Value::local);
    local.Send(Event::CommitResult, Value::ok);
    local.Send(Event::FinishPlan, Value::local);
    local.Send(Event::ResetDone, Value::local);
    local.Expect({Effect::AcceptPrepare, Effect::DecidePlan, Effect::ApplyVm,
        Effect::DecideFinish, Effect::ResetVm, Effect::CompleteSuccess});

    // Drive 2 rejection still mounts, and reanchor cannot forget that Drive 2.
    Trace fallback;
    fallback.Send(Event::Submit, Value::request);
    fallback.Send(Event::Prepared, Value::ok);
    fallback.Send(Event::PlanResult, Value::existing);
    fallback.Send(Event::AuxPlan, Value::query);
    fallback.Send(Event::VerifyResult, Value::rejected);
    fallback.Send(Event::CommitResult, Value::ok);
    fallback.Send(Event::FinishPlan, Value::reanchor);
    fallback.Send(Event::ResetDone, Value::reanchor);
    fallback.Send(Event::LoadResult, Value::ok);
    fallback.Send(Event::AuxPlan, Value::query);
    fallback.Send(Event::VerifyResult, Value::rejected);
    fallback.Expect({Effect::AcceptPrepare, Effect::DecidePlan, Effect::DecideAux,
        Effect::VerifyAux, Effect::EnterOffline, Effect::ApplyVm, Effect::DecideFinish,
        Effect::ResetVm, Effect::BeginReanchor, Effect::RememberLaunch, Effect::DecidePostAux,
        Effect::VerifyAux, Effect::EnterOffline, Effect::CompleteSuccess});

    // A changed active hash requires VM restore before RA rollback. No reset.
    Trace rollback;
    rollback.Send(Event::Submit, Value::request);
    rollback.Send(Event::Prepared, Value::ok);
    rollback.Send(Event::PlanResult, Value::existing);
    rollback.Send(Event::AuxPlan, Value::satisfied);
    rollback.Send(Event::AdvanceResult, Value::change);
    rollback.Send(Event::ChangeResult, Value::ok);
    rollback.Send(Event::CommitResult, Value::failed);
    rollback.Send(Event::RestoreResult, Value::restored);
    rollback.Send(Event::RollbackPlan, Value::rollback);
    rollback.Send(Event::RollbackResult, Value::unavailable);
    rollback.Expect({Effect::AcceptPrepare, Effect::DecidePlan, Effect::DecideAux,
        Effect::DecideAdvance, Effect::ChangeActive, Effect::RememberChanged, Effect::ApplyVm,
        Effect::RememberCommitFailure, Effect::RestoreVm, Effect::RememberRestore,
        Effect::DecideRollback, Effect::RollbackActive, Effect::EnterOffline, Effect::CompleteFailure});

    // An already ended session cannot be resurrected after VM restoration.
    Trace ended;
    ended.Send(Event::Submit, Value::request);
    ended.Send(Event::Prepared, Value::ok);
    ended.Send(Event::PlanResult, Value::fallback);
    ended.Send(Event::CommitResult, Value::failed);
    ended.Send(Event::RestoreResult, Value::restored);
    ended.Send(Event::RollbackPlan, Value::ended);
    ended.Expect({Effect::AcceptPrepare, Effect::DecidePlan, Effect::EnterOffline, Effect::ApplyVm,
        Effect::RememberCommitFailure, Effect::RestoreVm, Effect::RememberRestore,
        Effect::DecideRollback, Effect::CompleteFailure});

    for (std::size_t i = 0; i < static_cast<std::size_t>(State::Count); ++i) {
        const State state = static_cast<State>(i);
        const auto busy = Reduce(state, Event::Submit, Value::request);
        if (state != State::Idle) {
            Check(busy.next == state && busy.kind == CellKind::Busy && busy.effect_count == 1 &&
                busy.effects[0] == Effect::RejectBusy, "Busy preserves the owned operation");
        }
        for (Value value : {Value::connected, Value::disconnected}) {
            const auto connectivity = Reduce(state, Event::Connectivity, value);
            Check(connectivity.next == state && connectivity.kind == CellKind::Connectivity &&
                connectivity.effects[0] == Effect::UpdateConnectivity, "connectivity preserves operation");
        }
        const auto stale = Reduce(state, Event::Stale, Value::retired);
        Check(stale.next == state && stale.effects[0] == Effect::IgnoreStale,
            "retired results do not advance operation");
        const auto bad_value = Reduce(state, Event::Submit, Value::ok);
        Check(bad_value.kind == CellKind::Invalid && bad_value.next == state &&
            bad_value.effects[0] == Effect::ProtocolError, "event domains are checked before dispatch");
    }
    Check(Reduce(State::Idle, Event::CommitResult, Value::ok).kind == CellKind::Invalid,
        "unexpected completion is not silently treated as success");
    Check(Reduce(State::Count, Event::Submit, Value::request).kind == CellKind::Invalid,
        "invalid state does not index table");
    Check(Reduce(State::Idle, Event::Count, Value::request).kind == CellKind::Invalid,
        "invalid event does not index table");
    Check(Reduce(State::Idle, Event::Submit, static_cast<Value>(-1)).kind == CellKind::Invalid,
        "invalid value does not shift mask");
    std::cout << "ra_media_operation_test: PASS\n";
}
