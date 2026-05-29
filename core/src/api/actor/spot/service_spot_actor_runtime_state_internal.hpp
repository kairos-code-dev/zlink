/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

namespace zlink
{
namespace spot_actor_api_internal
{

struct actor_runtime_t
{
    actor_runtime_t () : protocol_drop_count (0), next_join_epoch (1) {}

    std::timed_mutex mutex;
    actor_node_registry_t nodes;
    actor_session_state_t sessions;
    actor_route_state_t routes;
    actor_join_state_t joins;
    actor_lifecycle_state_t lifecycle;
    uint64_t protocol_drop_count;
    uint64_t next_join_epoch;
};

}
}
