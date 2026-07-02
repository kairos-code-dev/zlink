/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_NODE_SERVICE_ATTACHMENT_STATE_HPP_INCLUDED__
#define __ZLINK_SPOT_NODE_SERVICE_ATTACHMENT_STATE_HPP_INCLUDED__

#include "zlink.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace zlink
{
class discovery_t;
class socket_base_t;

struct spot_node_attachment_monitor_handle_t
{
    spot_node_attachment_monitor_handle_t () : handle (NULL), owner_socket (NULL) {}

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

    bool pubsub_active () const { return !pub_endpoints.empty () && !sub_endpoints.empty (); }
};

struct spot_node_service_discovery_socket_plan_t
{
    std::vector<std::pair<std::string, socket_base_t *>> new_router_sockets;
    socket_base_t *pub_socket;
    socket_base_t *sub_socket;

    spot_node_service_discovery_socket_plan_t () : pub_socket (NULL), sub_socket (NULL) {}
};

struct spot_node_service_attachment_t
{
    struct manual_state_t
    {
        manual_state_t () : pub (NULL), sub (NULL) {}

        std::vector<socket_base_t *> routers;
        socket_base_t *pub;
        socket_base_t *sub;
    };

    struct discovered_state_t
    {
        enum auto_sub_replay_state_t
        {
            auto_sub_replay_none = 0,
            auto_sub_replay_initial,
            auto_sub_replay_reconnect
        };

        discovered_state_t () : pub (NULL), sub (NULL), auto_sub_replay_state (auto_sub_replay_none)
        {
        }

        bool has_pubsub () const
        {
            return pub != NULL && sub != NULL && !pub_endpoints.empty () && !sub_endpoints.empty ();
        }
        uint32_t router_count () const { return static_cast<uint32_t> (router_endpoints.size ()); }
        uint32_t pub_count () const { return static_cast<uint32_t> (pub_endpoints.size ()); }
        uint32_t sub_count () const { return static_cast<uint32_t> (sub_endpoints.size ()); }
        bool needs_sub_replay () const { return auto_sub_replay_state != auto_sub_replay_none; }
        void mark_sub_replay_pending (auto_sub_replay_state_t state_)
        {
            auto_sub_replay_state = state_;
        }
        void clear_sub_replay () { auto_sub_replay_state = auto_sub_replay_none; }

        std::map<std::string, socket_base_t *> routers;
        socket_base_t *pub;
        socket_base_t *sub;
        auto_sub_replay_state_t auto_sub_replay_state;
        std::set<std::string> router_endpoints;
        std::set<std::string> pub_endpoints;
        std::set<std::string> sub_endpoints;
    };

    spot_node_service_attachment_t () : next_router_index (0) {}

    bool has_manual_pubsub () const { return manual.pub != NULL && manual.sub != NULL; }
    bool has_auto_pubsub () const { return discovered.has_pubsub (); }
    uint32_t auto_router_count () const { return discovered.router_count (); }
    uint32_t auto_pub_count () const { return discovered.pub_count (); }
    uint32_t auto_sub_count () const { return discovered.sub_count (); }
    bool needs_auto_sub_replay () const { return discovered.needs_sub_replay (); }
    void mark_auto_sub_replay_pending (discovered_state_t::auto_sub_replay_state_t state_)
    {
        discovered.mark_sub_replay_pending (state_);
    }
    void clear_auto_sub_replay () { discovered.clear_sub_replay (); }

    manual_state_t manual;
    discovered_state_t discovered;
    std::vector<socket_base_t *> router_cache;
    size_t next_router_index;
    std::set<std::string> applied_filters;
};

struct spot_node_service_attachment_state_t
{
    struct service_sub_recv_cache_t
    {
        service_sub_recv_cache_t ();
        service_sub_recv_cache_t (const service_sub_recv_cache_t &) = delete;
        service_sub_recv_cache_t &operator= (const service_sub_recv_cache_t &) = delete;
        ~service_sub_recv_cache_t ();

        void reserve (size_t capacity_);
        void add (const std::string &channel_name_, socket_base_t *socket_);
        bool wait_ready_socket (zlink_recv_flags_t flags_, socket_base_t **ready_socket_out_) const;

      private:
        struct impl_t;
        std::unique_ptr<impl_t> impl;
    };

    std::map<std::string, spot_node_service_attachment_t> attachments;
    std::map<const socket_base_t *, std::string> socket_index;
    std::deque<spot_node_attachment_monitor_handle_t> monitors;
    std::map<std::string, discovery_t *> discoveries;
    std::set<std::string> pending_refresh_services;
    std::shared_ptr<service_sub_recv_cache_t> sub_recv_cache;

    spot_node_service_attachment_state_t () : sub_recv_cache (new service_sub_recv_cache_t ()) {}
};

class spot_node_service_attachments_t
{
  public:
    spot_node_service_attachment_state_t &state () { return _state; }
    const spot_node_service_attachment_state_t &state () const { return _state; }

    void register_monitor_locked (socket_base_t *owner_socket_,
                                  void *monitor_handle_,
                                  const std::string &channel_name_);
    void rebuild_caches_locked ();
    void queue_discovery_refresh_locked (const std::string &channel_name_);
    void remove_monitors_by_owner_locked (const std::vector<socket_base_t *> &sockets_);
    bool detach_discovered_service_locked (discovery_t *discovery_,
                                           std::vector<socket_base_t *> *sockets_to_close_out_);
    void collect_pending_service_discoveries_locked (
      std::vector<std::pair<std::string, discovery_t *>> *out_);

  private:
    spot_node_service_attachment_state_t _state;
};
}

#endif
