/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <variant>

namespace zlink::framework
{

/* Graceful drain contract (graceful-drain-handoff §5.1/§6). The terminal
 * result is shared by every caller: drain() is idempotent and the first
 * call fixes the shared deadline. */
enum class spot_drain_policy_t
{
    drain_natural,
    release_and_recreate
};

enum class drain_force_reason_t
{
    deadline_exceeded,
    draining_state_publish_failed,
    owner_cleanup_failed,
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

} // namespace zlink::framework
