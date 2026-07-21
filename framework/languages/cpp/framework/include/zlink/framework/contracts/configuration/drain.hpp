/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <variant>

namespace zlink::framework
{

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
