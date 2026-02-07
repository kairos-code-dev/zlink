/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_REGISTRY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_REGISTRY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "core/thread.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/clock.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <string>
#include <vector>

namespace zlink
{
class registry_t
{
  public:
    explicit registry_t (ctx_t *ctx_);
    ~registry_t ();

    bool check_tag () const;

    int set_endpoints (const char *pub_endpoint_, const char *router_endpoint_);
    int set_id (uint32_t registry_id_);
    int add_peer (const char *peer_pub_endpoint_);
    int set_heartbeat (uint32_t interval_ms_, uint32_t timeout_ms_);
    int set_broadcast_interval (uint32_t interval_ms_);
    int set_socket_option (int socket_role_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_);
    int start ();
    int destroy ();

  private:
    struct provider_entry_t
    {
        std::string endpoint;
        zlink_routing_id_t routing_id;
        uint32_t weight;
        uint64_t registered_at;
        uint64_t last_heartbeat;
        uint32_t source_registry;
    };

    typedef std::map<std::string, provider_entry_t> provider_map_t;

    struct service_entry_t
    {
        provider_map_t providers;
    };

    typedef std::map<std::string, service_entry_t> service_map_t;

    static void run (void *arg_);
    void loop ();
    void handle_router (void *router_);
    void handle_peer (void *sub_);
    void handle_register (void *router_, const zlink_msg_t *frames_,
                          size_t frame_count_,
                          const zlink_routing_id_t &sender_id_);
    void handle_unregister (const zlink_msg_t *frames_, size_t frame_count_);
    void handle_heartbeat (const zlink_msg_t *frames_, size_t frame_count_);
    void handle_update_weight (void *router_, const zlink_msg_t *frames_,
                               size_t frame_count_,
                               const zlink_routing_id_t &sender_id_);
    void send_register_ack (void *router_,
                            const zlink_routing_id_t &sender_id_,
                            uint8_t status_,
                            const std::string &endpoint_,
                            const std::string &error_);
    void send_service_list (void *pub_);
    void remove_expired (uint64_t now_ms_);

    void stop_worker ();

    ctx_t *_ctx;
    uint32_t _tag;

    std::string _pub_endpoint;
    std::string _router_endpoint;
    std::vector<std::string> _peer_pubs;

    uint32_t _registry_id;
    bool _registry_id_set;
    uint64_t _list_seq;

    uint32_t _heartbeat_interval_ms;
    uint32_t _heartbeat_timeout_ms;
    uint32_t _broadcast_interval_ms;

    struct socket_opt_t
    {
        int option;
        std::vector<unsigned char> value;
    };
    std::vector<socket_opt_t> _pub_opts;
    std::vector<socket_opt_t> _router_opts;
    std::vector<socket_opt_t> _peer_sub_opts;

    atomic_counter_t _stop;
    thread_t _worker;

    mutex_t _sync;

    service_map_t _services;
    std::map<uint32_t, uint64_t> _peer_seq;
    std::map<uint32_t, uint64_t> _peer_last_seen;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (registry_t)
};
}

#endif
