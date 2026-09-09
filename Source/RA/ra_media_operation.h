#ifndef XM8_RA_MEDIA_OPERATION_H
#define XM8_RA_MEDIA_OPERATION_H

#include "ra_media_operation_table.generated.h"

namespace Xm8Ra { namespace MediaOperation {

// This layer never invokes RA, the VM, callbacks or completion handlers.
// The owner applies next before executing effects, then queues their results.
struct Step {
    State next;
    CellKind kind;
    std::array<Effect, 4> effects;
    std::size_t effect_count;
    unsigned transition_id; // Txx in the design; zero for generic cells.
};

inline Step Reduce(State current, Event event, Value value)
{
    const auto state_index = static_cast<std::size_t>(current);
    const auto event_index = static_cast<std::size_t>(event);
    const auto value_index = static_cast<std::size_t>(value);
    const Step invalid = {current, CellKind::Invalid, {{Effect::ProtocolError}}, 1, 0};
    if (state_index >= static_cast<std::size_t>(State::Count) ||
        event_index >= static_cast<std::size_t>(Event::Count) ||
        value_index >= static_cast<std::size_t>(Value::Count)) return invalid;
    const std::uint64_t value_bit = std::uint64_t(1) << value_index;
    if (!(kValueDomains[event_index] & value_bit)) return invalid;

    const Cell& cell = kCells[state_index][event_index];
    if (cell.kind != CellKind::Normal)
        return {current, cell.kind, {{cell.generic_effect}}, 1, 0};
    for (std::size_t index = cell.first; index < cell.first + cell.count; ++index) {
        const Transition& transition = kTransitions[index];
        if (transition.values & value_bit)
            return {transition.next, CellKind::Normal, transition.effects,
                transition.effect_count, transition.id};
    }
    return invalid;
}

} } // namespace Xm8Ra::MediaOperation
#endif
