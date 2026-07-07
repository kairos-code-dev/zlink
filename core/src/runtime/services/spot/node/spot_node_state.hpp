/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_STATE_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_STATE_HPP_INCLUDED__

#include "services/spot/dispatch/spot_internal_receiver.hpp"
#include "services/spot/node/spot_node_service_attachment_state.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

struct spot_handle_t;
struct spot_logical_state_t;

namespace zlink
{
namespace spot_actor_api_internal
{
struct actor_handle_t;
}

using spot_actor_api_internal::actor_handle_t;

class socket_base_t;
class spot_pub_t;
class spot_sub_t;

struct spot_node_summary_state_t
{
    struct subject_snapshot_entry_t
    {
        std::string subject;
        uint32_t subject_kind;
        bool ready;
        uint64_t last_changed_ms;
    };

    spot_node_summary_state_t () :
        last_summary_error (0),
        summary_last_changed_ms (0),
        subject_snapshot_generation (1),
        cached_subject_snapshot_generation (0)
    {
    }

    void mark_subject_snapshot_changed () { ++subject_snapshot_generation; }

    std::map<std::string, uint64_t> subject_last_changed_ms;
    int last_summary_error;
    uint64_t summary_last_changed_ms;
    uint64_t subject_snapshot_generation;
    mutable uint64_t cached_subject_snapshot_generation;
    mutable std::vector<subject_snapshot_entry_t> cached_subject_entries;
};

struct spot_node_aggregate_subscription_state_t
{
    std::unordered_map<std::string, uint32_t> local_exact_topic_refcount;
    std::unordered_map<std::string, uint32_t> local_prefix_topic_refcount;
};

struct spot_node_tls_state_t
{
    spot_node_tls_state_t () :
        tls_trust_system (0),
        server_tls_locked (false),
        mesh_client_tls_locked (false),
        registration_tls_locked (false)
    {
    }

    std::string tls_cert;
    std::string tls_key;
    std::string tls_ca;
    std::string tls_hostname;
    int tls_trust_system;
    bool server_tls_locked;
    bool mesh_client_tls_locked;
    bool registration_tls_locked;
};

struct spot_node_endpoint_state_t
{
    spot_node_endpoint_state_t () :
        local_fanout_sndhwm_cfg (0),
        local_fanout_sndhwm_default (0),
        local_filtered_sub_count (0),
        active_peer_count (0)
    {
    }

    std::string bound_endpoint;
    std::string router_bind_endpoint;
    int local_fanout_sndhwm_cfg;
    int local_fanout_sndhwm_default;
    std::atomic<uint32_t> local_filtered_sub_count;
    std::atomic<uint32_t> active_peer_count;
};

struct spot_node_handle_state_t
{
    spot_node_handle_state_t () : next_spot_stable_id (1), entry_spot_rid_locked (false) {}

    spot_node_default_handles_t handle_defaults;
    std::set<spot_pub_t *> pubs;
    std::set<spot_sub_t *> subs;
    std::set<spot_handle_t *> facades;
    std::shared_ptr<spot_logical_state_t> entry_spot;
    std::map<std::string, std::shared_ptr<spot_logical_state_t>> spots_by_rid;
    std::set<std::string> pending_spot_creations;
    uint64_t next_spot_stable_id;
    bool entry_spot_rid_locked;
};

struct spot_node_actor_state_t
{
    spot_node_actor_state_t () : next_generation (0) {}

    std::set<actor_handle_t *> actor_handles;
    std::map<std::string, actor_handle_t *> actors_by_id;
    uint64_t next_generation;
};

}

#endif
