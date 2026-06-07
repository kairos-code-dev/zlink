/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_CONTROL_POLICY_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_CONTROL_POLICY_HPP_INCLUDED__

#include <set>
#include <string>
#include <vector>

namespace zlink
{
class spot_sub_t;

namespace spot_node_control_policy
{
uint32_t resolve_effective_ready_count (uint32_t ready_count_,
                                        uint32_t active_peer_count_,
                                        uint32_t connected_ready_count_);
unsigned int subscription_ready_holdoff_ticks (const std::set<std::string> &connected_endpoints_);
unsigned int subscription_replay_attempt_count (const std::set<std::string> &connected_endpoints_);
unsigned int subscription_replay_holdoff_ticks (const std::set<std::string> &connected_endpoints_);
unsigned int pub_delivery_ready_holdoff_ticks (const std::set<std::string> &connected_endpoints_);
std::string make_ready_ack_arg (const std::string &target_endpoint_,
                                const std::string &raw_filter_,
                                const std::string &ack_source_id_);
void collect_replay_raw_filters (const std::vector<spot_sub_t *> &subs_,
                                 std::set<std::string> *out_);
}
}

#endif
