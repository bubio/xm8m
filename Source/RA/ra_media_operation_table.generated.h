// Generated from Documents/RetroAchievements/48_media_machine.json. Do not edit.
#ifndef XM8_RA_MEDIA_OPERATION_TABLE_GENERATED_H
#define XM8_RA_MEDIA_OPERATION_TABLE_GENERATED_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace Xm8Ra { namespace MediaOperation {

enum class State { Idle, Prepare, Plan, AwaitResolve, AwaitAnchor, CheckAux, AwaitAux, Advance, AwaitChange, Commit, Restore, ChooseRollback, AwaitRollback, Finalize, AwaitReset, AwaitReanchor, CheckPostAux, AwaitPostAux, Count };
enum class Event { Submit, Prepared, PlanResult, ResolveResult, LoadResult, AuxPlan, VerifyResult, AdvanceResult, ChangeResult, CommitResult, RestoreResult, RollbackPlan, RollbackResult, FinishPlan, ResetDone, Connectivity, Stale, Count };
enum class Value { request, ok, failed, local, fallback, resolve, launch, existing, same, other, unregistered, unavailable, satisfied, query, rejected, commit, change, restored, rollback, end, ended, preserve, none, reanchor, noanchor, connected, disconnected, retired, Count };
enum class Effect { AcceptPrepare, RejectLocal, DecidePlan, EnterOffline, ApplyVm, ResolveAnchor, BeginLaunch, RememberResolved, RememberLaunch, DecideAux, VerifyAux, RememberVerified, DecideAdvance, ChangeActive, RememberChanged, DecideFinish, RememberCommitFailure, RestoreVm, RememberRestore, DecideRollback, RollbackActive, RememberRolledBack, CompleteFailure, CompleteSuccess, ResetVm, ResetProgress, ActivateLaunch, BeginReanchor, NoAnchor, DecidePostAux, UpdateConnectivity, RejectBusy, IgnoreStale, ProtocolError, Count };
enum class CellKind { Normal, Busy, Connectivity, Stale, Invalid };

struct Transition {
    std::uint64_t values;
    State next;
    std::array<Effect, 4> effects;
    std::size_t effect_count;
    unsigned id;
};
struct Cell {
    CellKind kind;
    std::size_t first;
    std::size_t count;
    Effect generic_effect;
};

static constexpr Transition kTransitions[] = {
    {0x1ULL, State::Prepare, {{Effect::AcceptPrepare}}, 1, 1}, // Idle/Submit T01
    {0x4ULL, State::Idle, {{Effect::RejectLocal}}, 1, 2}, // Prepare/Prepared T02
    {0x2ULL, State::Plan, {{Effect::DecidePlan}}, 1, 3}, // Prepare/Prepared T03
    {0x8ULL, State::Commit, {{Effect::ApplyVm}}, 1, 4}, // Plan/PlanResult T04
    {0x10ULL, State::Commit, {{Effect::EnterOffline, Effect::ApplyVm}}, 2, 5}, // Plan/PlanResult T05
    {0x20ULL, State::AwaitResolve, {{Effect::ResolveAnchor}}, 1, 6}, // Plan/PlanResult T06
    {0x40ULL, State::AwaitAnchor, {{Effect::BeginLaunch}}, 1, 7}, // Plan/PlanResult T07
    {0x80ULL, State::CheckAux, {{Effect::DecideAux}}, 1, 8}, // Plan/PlanResult T08
    {0x100ULL, State::CheckAux, {{Effect::RememberResolved, Effect::DecideAux}}, 2, 9}, // AwaitResolve/ResolveResult T09
    {0x200ULL, State::AwaitAnchor, {{Effect::RememberResolved, Effect::BeginLaunch}}, 2, 10}, // AwaitResolve/ResolveResult T10
    {0xc00ULL, State::Commit, {{Effect::EnterOffline, Effect::ApplyVm}}, 2, 11}, // AwaitResolve/ResolveResult T11
    {0x2ULL, State::CheckAux, {{Effect::RememberLaunch, Effect::DecideAux}}, 2, 12}, // AwaitAnchor/LoadResult T12
    {0x800ULL, State::Commit, {{Effect::EnterOffline, Effect::ApplyVm}}, 2, 13}, // AwaitAnchor/LoadResult T13
    {0x1000ULL, State::Advance, {{Effect::DecideAdvance}}, 1, 14}, // CheckAux/AuxPlan T14
    {0x2000ULL, State::AwaitAux, {{Effect::VerifyAux}}, 1, 15}, // CheckAux/AuxPlan T15
    {0x10ULL, State::Commit, {{Effect::EnterOffline, Effect::ApplyVm}}, 2, 16}, // CheckAux/AuxPlan T16
    {0x100ULL, State::Advance, {{Effect::RememberVerified, Effect::DecideAdvance}}, 2, 17}, // AwaitAux/VerifyResult T17
    {0x4800ULL, State::Commit, {{Effect::EnterOffline, Effect::ApplyVm}}, 2, 18}, // AwaitAux/VerifyResult T18
    {0x8000ULL, State::Commit, {{Effect::ApplyVm}}, 1, 19}, // Advance/AdvanceResult T19
    {0x10000ULL, State::AwaitChange, {{Effect::ChangeActive}}, 1, 20}, // Advance/AdvanceResult T20
    {0x10ULL, State::Commit, {{Effect::EnterOffline, Effect::ApplyVm}}, 2, 21}, // Advance/AdvanceResult T21
    {0x2ULL, State::Commit, {{Effect::RememberChanged, Effect::ApplyVm}}, 2, 22}, // AwaitChange/ChangeResult T22
    {0x800ULL, State::Commit, {{Effect::EnterOffline, Effect::ApplyVm}}, 2, 23}, // AwaitChange/ChangeResult T23
    {0x2ULL, State::Finalize, {{Effect::DecideFinish}}, 1, 24}, // Commit/CommitResult T24
    {0x4ULL, State::Restore, {{Effect::RememberCommitFailure, Effect::RestoreVm}}, 2, 25}, // Commit/CommitResult T25
    {0x20004ULL, State::ChooseRollback, {{Effect::RememberRestore, Effect::DecideRollback}}, 2, 26}, // Restore/RestoreResult T26
    {0x40000ULL, State::AwaitRollback, {{Effect::RollbackActive}}, 1, 27}, // ChooseRollback/RollbackPlan T27
    {0x80000ULL, State::Idle, {{Effect::EnterOffline, Effect::CompleteFailure}}, 2, 28}, // ChooseRollback/RollbackPlan T28
    {0x300000ULL, State::Idle, {{Effect::CompleteFailure}}, 1, 29}, // ChooseRollback/RollbackPlan T29
    {0x2ULL, State::Idle, {{Effect::RememberRolledBack, Effect::CompleteFailure}}, 2, 30}, // AwaitRollback/RollbackResult T30
    {0x800ULL, State::Idle, {{Effect::EnterOffline, Effect::CompleteFailure}}, 2, 31}, // AwaitRollback/RollbackResult T31
    {0x400000ULL, State::Idle, {{Effect::CompleteSuccess}}, 1, 32}, // Finalize/FinishPlan T32
    {0xa00048ULL, State::AwaitReset, {{Effect::ResetVm}}, 1, 33}, // Finalize/FinishPlan T33
    {0x8ULL, State::Idle, {{Effect::CompleteSuccess}}, 1, 34}, // AwaitReset/ResetDone T34
    {0x200000ULL, State::Idle, {{Effect::ResetProgress, Effect::CompleteSuccess}}, 2, 35}, // AwaitReset/ResetDone T35
    {0x40ULL, State::Idle, {{Effect::ResetProgress, Effect::ActivateLaunch, Effect::CompleteSuccess}}, 3, 36}, // AwaitReset/ResetDone T36
    {0x800000ULL, State::AwaitReanchor, {{Effect::BeginReanchor}}, 1, 37}, // AwaitReset/ResetDone T37
    {0x1000000ULL, State::Idle, {{Effect::NoAnchor, Effect::CompleteSuccess}}, 2, 38}, // AwaitReset/ResetDone T38
    {0x2ULL, State::CheckPostAux, {{Effect::RememberLaunch, Effect::DecidePostAux}}, 2, 39}, // AwaitReanchor/LoadResult T39
    {0x800ULL, State::Idle, {{Effect::EnterOffline, Effect::CompleteSuccess}}, 2, 40}, // AwaitReanchor/LoadResult T40
    {0x1000ULL, State::Idle, {{Effect::ResetProgress, Effect::ActivateLaunch, Effect::CompleteSuccess}}, 3, 41}, // CheckPostAux/AuxPlan T41
    {0x2000ULL, State::AwaitPostAux, {{Effect::VerifyAux}}, 1, 42}, // CheckPostAux/AuxPlan T42
    {0x10ULL, State::Idle, {{Effect::EnterOffline, Effect::CompleteSuccess}}, 2, 43}, // CheckPostAux/AuxPlan T43
    {0x100ULL, State::Idle, {{Effect::RememberVerified, Effect::ResetProgress, Effect::ActivateLaunch, Effect::CompleteSuccess}}, 4, 44}, // AwaitPostAux/VerifyResult T44
    {0x4800ULL, State::Idle, {{Effect::EnterOffline, Effect::CompleteSuccess}}, 2, 45}, // AwaitPostAux/VerifyResult T45
};

static constexpr std::uint64_t kValueDomains[] = {
    0x1ULL, // Submit
    0x6ULL, // Prepared
    0xf8ULL, // PlanResult
    0xf00ULL, // ResolveResult
    0x802ULL, // LoadResult
    0x3010ULL, // AuxPlan
    0x4900ULL, // VerifyResult
    0x18010ULL, // AdvanceResult
    0x802ULL, // ChangeResult
    0x6ULL, // CommitResult
    0x20004ULL, // RestoreResult
    0x3c0000ULL, // RollbackPlan
    0x802ULL, // RollbackResult
    0xe00048ULL, // FinishPlan
    0x1a00048ULL, // ResetDone
    0x6000000ULL, // Connectivity
    0x8000000ULL, // Stale
};

static constexpr Event kEffectResults[] = {
    Event::Prepared, // AcceptPrepare
    Event::Count, // RejectLocal
    Event::PlanResult, // DecidePlan
    Event::Count, // EnterOffline
    Event::CommitResult, // ApplyVm
    Event::ResolveResult, // ResolveAnchor
    Event::LoadResult, // BeginLaunch
    Event::Count, // RememberResolved
    Event::Count, // RememberLaunch
    Event::AuxPlan, // DecideAux
    Event::VerifyResult, // VerifyAux
    Event::Count, // RememberVerified
    Event::AdvanceResult, // DecideAdvance
    Event::ChangeResult, // ChangeActive
    Event::Count, // RememberChanged
    Event::FinishPlan, // DecideFinish
    Event::Count, // RememberCommitFailure
    Event::RestoreResult, // RestoreVm
    Event::Count, // RememberRestore
    Event::RollbackPlan, // DecideRollback
    Event::RollbackResult, // RollbackActive
    Event::Count, // RememberRolledBack
    Event::Count, // CompleteFailure
    Event::Count, // CompleteSuccess
    Event::ResetDone, // ResetVm
    Event::Count, // ResetProgress
    Event::Count, // ActivateLaunch
    Event::LoadResult, // BeginReanchor
    Event::Count, // NoAnchor
    Event::AuxPlan, // DecidePostAux
    Event::Count, // UpdateConnectivity
    Event::Count, // RejectBusy
    Event::Count, // IgnoreStale
    Event::Count, // ProtocolError
};

static constexpr Cell kCells[18][17] = {
    { // Idle
        {CellKind::Normal, 0, 1, Effect::ProtocolError}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // Prepare
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Normal, 1, 2, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // Plan
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Normal, 3, 5, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // AwaitResolve
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Normal, 8, 3, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // AwaitAnchor
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Normal, 11, 2, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // CheckAux
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Normal, 13, 3, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // AwaitAux
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Normal, 16, 2, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // Advance
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Normal, 18, 3, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // AwaitChange
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Normal, 21, 2, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // Commit
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Normal, 23, 2, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // Restore
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Normal, 25, 1, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // ChooseRollback
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Normal, 26, 3, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // AwaitRollback
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Normal, 29, 2, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // Finalize
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Normal, 31, 2, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // AwaitReset
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Normal, 33, 5, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // AwaitReanchor
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Normal, 38, 2, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // CheckPostAux
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Normal, 40, 3, Effect::ProtocolError}, // AuxPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
    { // AwaitPostAux
        {CellKind::Busy, 0, 0, Effect::RejectBusy}, // Submit
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // Prepared
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // PlanResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResolveResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // LoadResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AuxPlan
        {CellKind::Normal, 43, 2, Effect::ProtocolError}, // VerifyResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // AdvanceResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ChangeResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // CommitResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RestoreResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // RollbackResult
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // FinishPlan
        {CellKind::Invalid, 0, 0, Effect::ProtocolError}, // ResetDone
        {CellKind::Connectivity, 0, 0, Effect::UpdateConnectivity}, // Connectivity
        {CellKind::Stale, 0, 0, Effect::IgnoreStale}, // Stale
    },
};

} } // namespace Xm8Ra::MediaOperation

#endif
