/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_PEER_STATE_HPP_INCLUDED__
#define __ZLINK_SPOT_PEER_STATE_HPP_INCLUDED__

#include <zlink.h>

#include <map>
#include <set>
#include <string>

namespace zlink
{
struct spot_peer_observation_t
{
    spot_peer_observation_t () : last_changed_ms (0), connected_since_ms (0) {}

    uint64_t last_changed_ms;
    uint64_t connected_since_ms;
};

struct spot_peer_state_t
{
    std::set<std::string> manual_endpoints;
    std::set<std::string> active_endpoints;
    std::set<std::string> connected_endpoints;
    std::set<std::string> discovery_endpoints;
    std::set<std::string> pending_subscription_ready_filters;
    std::map<std::string, std::set<std::string> > pub_delivery_ready_sources;
    std::map<std::string, uint32_t> pending_pub_delivery_ready_counts;
    std::map<std::string, spot_peer_observation_t> observations;
    std::map<std::string, zlink_admission_state_t> peer_admission_by_endpoint;
    std::map<std::string, zlink_admission_state_t> peer_admission_by_rid;
    bool subscription_ready_refresh_pending;
    unsigned int subscription_ready_refresh_holdoff_ticks;
    bool subscription_replay_pending;
    unsigned int subscription_replay_attempts;
    unsigned int subscription_replay_holdoff_ticks;
    bool pub_delivery_ready_refresh_pending;
    unsigned int pub_delivery_ready_refresh_holdoff_ticks;

    spot_peer_state_t () :
        subscription_ready_refresh_pending (false),
        subscription_ready_refresh_holdoff_ticks (0),
        subscription_replay_pending (false),
        subscription_replay_attempts (0),
        subscription_replay_holdoff_ticks (0),
        pub_delivery_ready_refresh_pending (false),
        pub_delivery_ready_refresh_holdoff_ticks (0)
    {
    }
};
}

#endif
