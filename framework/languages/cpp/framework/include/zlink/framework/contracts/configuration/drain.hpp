/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdint>
#include <variant>

namespace zlink::framework
{

enum class drain_force_reason_t
{
    deadline_exceeded,
    relocation_failed,
    teardown_failed
};

struct drained_t
{
};

struct force_stopped_t
{
    drain_force_reason_t reason = drain_force_reason_t::deadline_exceeded;
};

using drain_result_t = std::variant<drained_t, force_stopped_t>;

enum class framework_runtime_state_t : std::uint8_t
{
    preparing = 0,
    serving = 1,
    retiring = 2,
    draining = 3,
    stopped = 4,
    error = 5
};

enum class termination_intent_t : std::uint8_t
{
    retire = 0,
    shutdown = 1
};

enum class termination_outcome_t : std::uint8_t
{
    stopped = 0,
    blocked = 1,
    force_stopped = 2
};

enum class termination_reason_t : std::uint8_t
{
    none = 0,
    target_unavailable = 1,
    store_unavailable = 2,
    relocation_disabled = 3,
    state_incompatible = 4,
    deadline_exceeded = 5,
    relocation_failed = 6,
    teardown_failed = 7,
    runtime_not_ready = 8
};

struct termination_result_t
{
    termination_intent_t effective_intent =
      termination_intent_t::shutdown;
    termination_outcome_t outcome = termination_outcome_t::stopped;
    termination_reason_t reason = termination_reason_t::none;

    friend bool operator== (const termination_result_t &,
                            const termination_result_t &) = default;
};

} // namespace zlink::framework
