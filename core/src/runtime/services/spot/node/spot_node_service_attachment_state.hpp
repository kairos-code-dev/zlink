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
class socket_base_t;

struct spot_node_attachment_monitor_handle_t
{
    spot_node_attachment_monitor_handle_t () : handle (NULL), owner_socket (NULL) {}

    void *handle;
    socket_base_t *owner_socket;
    std::string channel_name;
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

    spot_node_service_attachment_t () : next_router_index (0) {}

    bool has_manual_pubsub () const { return manual.pub != NULL && manual.sub != NULL; }

    manual_state_t manual;
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
    void remove_monitors_by_owner_locked (const std::vector<socket_base_t *> &sockets_);

  private:
    spot_node_service_attachment_state_t _state;
};
}

#endif
