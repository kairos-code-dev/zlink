/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_STATE_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_STATE_HPP_INCLUDED__

#include "services/spot/dispatch/spot_internal_receiver.hpp"

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct actor_handle_t;
struct spot_handle_t;
struct spot_logical_state_t;

namespace zlink
{
class discovery_t;
class socket_base_t;
class spot_pub_t;
class spot_sub_t;

struct spot_node_summary_state_t
{
    spot_node_summary_state_t () : last_summary_error (0), summary_last_changed_ms (0)
    {
    }

    std::map<std::string, uint64_t> subject_last_changed_ms;
    int last_summary_error;
    uint64_t summary_last_changed_ms;
};

struct spot_node_aggregate_subscription_state_t
{
    std::unordered_map<std::string, uint32_t> local_exact_topic_refcount;
    std::unordered_map<std::string, uint32_t> local_prefix_topic_refcount;
};

struct spot_node_discovery_binding_state_t
{
    spot_node_discovery_binding_state_t () :
        discovery (NULL),
        discovery_seq (0),
        registered (false)
    {
    }

    discovery_t *discovery;
    std::string discovery_service;
    uint64_t discovery_seq;
    std::set<std::string> pending_service_updates;
    bool registered;
    std::string advertise_endpoint;
    std::string registration_uplink_endpoint;
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
    std::map<std::string, std::shared_ptr<spot_logical_state_t> > spots_by_rid;
    uint64_t next_spot_stable_id;
    bool entry_spot_rid_locked;
};

struct spot_node_actor_state_t
{
    spot_node_actor_state_t () : next_generation (0)
    {
    }

    std::set<actor_handle_t *> actor_handles;
    std::map<std::string, actor_handle_t *> actors_by_id;
    uint64_t next_generation;
};

struct spot_node_attachment_monitor_handle_t
{
    spot_node_attachment_monitor_handle_t () : handle (NULL), owner_socket (NULL)
    {
    }

    void *handle;
    socket_base_t *owner_socket;
    std::string channel_name;
};

struct spot_node_service_discovery_topology_t
{
    std::set<std::string> router_endpoints;
    std::set<std::string> pub_endpoints;
    std::set<std::string> sub_endpoints;

    void clear ()
    {
        router_endpoints.clear ();
        pub_endpoints.clear ();
        sub_endpoints.clear ();
    }

    bool pubsub_active () const
    {
        return !pub_endpoints.empty () && !sub_endpoints.empty ();
    }
};

struct spot_node_service_discovery_socket_plan_t
{
    std::vector<std::pair<std::string, socket_base_t *> > new_router_sockets;
    socket_base_t *pub_socket;
    socket_base_t *sub_socket;

    spot_node_service_discovery_socket_plan_t () : pub_socket (NULL), sub_socket (NULL)
    {
    }
};

struct spot_node_service_attachment_t
{
    struct manual_state_t
    {
        manual_state_t () : pub (NULL), sub (NULL), channel_dealer_discovery (NULL)
        {
        }

        std::vector<socket_base_t *> routers;
        socket_base_t *pub;
        socket_base_t *sub;
        discovery_t *channel_dealer_discovery;
    };

    struct discovered_state_t
    {
        enum auto_sub_replay_state_t
        {
            auto_sub_replay_none = 0,
            auto_sub_replay_initial,
            auto_sub_replay_reconnect
        };

        discovered_state_t () :
            pub (NULL),
            sub (NULL),
            auto_sub_replay_state (auto_sub_replay_none)
        {
        }

        bool has_pubsub () const
        {
            return pub != NULL && sub != NULL && !pub_endpoints.empty ()
                   && !sub_endpoints.empty ();
        }
        uint32_t router_count () const
        {
            return static_cast<uint32_t> (router_endpoints.size ());
        }
        uint32_t pub_count () const
        {
            return static_cast<uint32_t> (pub_endpoints.size ());
        }
        uint32_t sub_count () const
        {
            return static_cast<uint32_t> (sub_endpoints.size ());
        }
        bool needs_sub_replay () const
        {
            return auto_sub_replay_state != auto_sub_replay_none;
        }
        void mark_sub_replay_pending (auto_sub_replay_state_t state_)
        {
            auto_sub_replay_state = state_;
        }
        void clear_sub_replay ()
        {
            auto_sub_replay_state = auto_sub_replay_none;
        }

        std::map<std::string, socket_base_t *> routers;
        socket_base_t *pub;
        socket_base_t *sub;
        auto_sub_replay_state_t auto_sub_replay_state;
        std::set<std::string> router_endpoints;
        std::set<std::string> pub_endpoints;
        std::set<std::string> sub_endpoints;
    };

    spot_node_service_attachment_t () : next_router_index (0)
    {
    }

    bool has_manual_pubsub () const
    {
        return manual.pub != NULL && manual.sub != NULL;
    }
    bool has_auto_pubsub () const
    {
        return discovered.has_pubsub ();
    }
    uint32_t auto_router_count () const
    {
        return discovered.router_count ();
    }
    uint32_t auto_pub_count () const
    {
        return discovered.pub_count ();
    }
    uint32_t auto_sub_count () const
    {
        return discovered.sub_count ();
    }
    bool needs_auto_sub_replay () const
    {
        return discovered.needs_sub_replay ();
    }
    void mark_auto_sub_replay_pending (
      discovered_state_t::auto_sub_replay_state_t state_)
    {
        discovered.mark_sub_replay_pending (state_);
    }
    void clear_auto_sub_replay ()
    {
        discovered.clear_sub_replay ();
    }

    manual_state_t manual;
    discovered_state_t discovered;
    std::vector<socket_base_t *> router_cache;
    size_t next_router_index;
    std::set<std::string> applied_filters;
};

struct spot_node_service_attachment_state_t
{
    struct service_sub_cache_entry_t
    {
        std::string channel_name;
        socket_base_t *socket;
    };

    typedef std::vector<service_sub_cache_entry_t> service_sub_cache_t;

    struct service_sub_recv_cache_t
    {
        service_sub_cache_t entries;
    };

    std::map<std::string, spot_node_service_attachment_t> attachments;
    std::map<const socket_base_t *, std::string> socket_index;
    std::deque<spot_node_attachment_monitor_handle_t> monitors;
    std::map<std::string, discovery_t *> discoveries;
    std::map<std::string, discovery_t *> channel_dealer_discoveries;
    socket_base_t *pub_ingress;
    std::set<std::string> pending_refresh_services;
    std::shared_ptr<service_sub_recv_cache_t> sub_recv_cache;

    spot_node_service_attachment_state_t () :
        pub_ingress (NULL),
        sub_recv_cache (new service_sub_recv_cache_t ())
    {
    }
};
}

#endif
