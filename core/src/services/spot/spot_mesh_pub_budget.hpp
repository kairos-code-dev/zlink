/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_MESH_PUB_BUDGET_HPP_INCLUDED__
#define __ZLINK_SPOT_MESH_PUB_BUDGET_HPP_INCLUDED__

#include <stdint.h>

#include <string>

namespace zlink
{
class socket_base_t;
struct spot_runtime_t;

namespace spot_mesh_pub_budget_t
{
int resolve_default (const std::string &endpoint_, unsigned int ready_peers_);
bool should_refresh (const std::string &endpoint_,
                     unsigned int previous_ready_peers_,
                     unsigned int next_ready_peers_);
void reset_runtime_state (spot_runtime_t *runtime_);
bool publish_ready_hint (spot_runtime_t *runtime_, uint32_t ready_count_);
int resolve_runtime_default (const spot_runtime_t *runtime_);
int resolve_initial_bind_sndhwm (const spot_runtime_t *runtime_,
                                 const std::string &endpoint_);
void refresh_live_socket (spot_runtime_t *runtime_,
                          socket_base_t *mesh_pub_,
                          int *current_hwm_,
                          uint64_t *last_budget_version_,
                          std::string *last_bound_endpoint_);
}
}

#endif
