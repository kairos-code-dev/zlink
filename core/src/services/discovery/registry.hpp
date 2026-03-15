/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_REGISTRY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_REGISTRY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_runtime_base.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/clock.hpp"
#include "utils/mutex.hpp"

#include <map>
#include <set>
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

    int bind (const char *pub_endpoint_, const char *router_endpoint_);
    int set_id (uint32_t registry_id_);
    int add_peer (const char *peer_pub_endpoint_);
    int set_heartbeat (uint32_t interval_ms_, uint32_t timeout_ms_);
    int set_broadcast_interval (uint32_t interval_ms_);
    int set_socket_option (int socket_role_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_);
    int topology_snapshot (zlink_registry_topology_entry_t *entries_,
                           size_t *count_);
    int topology_query (const zlink_registry_topology_filter_t *filter_,
                        zlink_registry_topology_entry_t *entries_,
                        size_t *count_);
    int gateway_peers_snapshot (zlink_registry_gateway_peer_entry_t *entries_,
                                size_t *count_);
    int gateway_peers_query (
      const zlink_registry_gateway_peer_filter_t *filter_,
      zlink_registry_gateway_peer_entry_t *entries_,
      size_t *count_);
    int start ();
    int destroy ();
    service_public_api_guard_t &public_api_guard_for_testing ()
    {
        return _public_api;
    }

  private:
    struct service_key_t
    {
        uint16_t service_type;
        std::string service_name;

        bool operator< (const service_key_t &other_) const
        {
            if (service_type != other_.service_type)
                return service_type < other_.service_type;
            return service_name < other_.service_name;
        }
    };

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

    typedef std::map<service_key_t, service_entry_t> service_map_t;

    struct topology_key_t
    {
        uint16_t service_kind;
        std::string routing_id_key;
        std::string service_name;

        bool operator< (const topology_key_t &other_) const
        {
            if (service_kind != other_.service_kind)
                return service_kind < other_.service_kind;
            if (routing_id_key != other_.routing_id_key)
                return routing_id_key < other_.routing_id_key;
            return service_name < other_.service_name;
        }
    };

    struct topology_entry_t
    {
        zlink_registry_topology_entry_t entry;
    };

    struct gateway_peer_key_t
    {
        std::string gateway_routing_id_key;
        std::string service_name;
        std::string peer_routing_id_key;

        bool operator< (const gateway_peer_key_t &other_) const
        {
            if (gateway_routing_id_key != other_.gateway_routing_id_key)
                return gateway_routing_id_key < other_.gateway_routing_id_key;
            if (service_name != other_.service_name)
                return service_name < other_.service_name;
            return peer_routing_id_key < other_.peer_routing_id_key;
        }
    };

    struct gateway_peer_entry_t
    {
        zlink_registry_gateway_peer_entry_t entry;
    };

    static void control_task (void *arg_);
    void tick ();
    int ensure_sockets ();
    void close_sockets ();
    void handle_router (void *router_);
    void handle_peer (void *sub_);
    void handle_register (void *router_, const zlink_msg_t *frames_,
                          size_t frame_count_,
                          const zlink_routing_id_t &sender_id_);
    void handle_unregister (void *router_, const zlink_msg_t *frames_,
                            size_t frame_count_,
                            const zlink_routing_id_t &sender_id_);
    void handle_heartbeat (const zlink_msg_t *frames_, size_t frame_count_);
    void handle_bootstrap (void *router_,
                           const zlink_routing_id_t &sender_id_);
    void handle_topology_report (const zlink_msg_t *frames_,
                                 size_t frame_count_);
    void handle_topology_query (void *router_,
                                const zlink_msg_t *frames_,
                                size_t frame_count_,
                                const zlink_routing_id_t &sender_id_);
    void handle_gateway_peer_report (const zlink_msg_t *frames_,
                                     size_t frame_count_);
    void handle_gateway_peer_query (void *router_,
                                    const zlink_msg_t *frames_,
                                    size_t frame_count_,
                                    const zlink_routing_id_t &sender_id_);
    void handle_update_weight (void *router_, const zlink_msg_t *frames_,
                               size_t frame_count_,
                               const zlink_routing_id_t &sender_id_);
    void send_register_ack (void *router_,
                            const zlink_routing_id_t &sender_id_,
                            uint8_t status_,
                            const std::string &endpoint_,
                            const std::string &error_);
    void send_unregister_ack (void *router_,
                              const zlink_routing_id_t &sender_id_,
                              uint8_t status_,
                              const std::string &error_);
    void send_topology_reply (void *router_,
                              const zlink_routing_id_t &sender_id_,
                              const std::vector<zlink_registry_topology_entry_t>
                                &entries_);
    void send_gateway_peer_reply (
      void *router_,
      const zlink_routing_id_t &sender_id_,
      const std::vector<zlink_registry_gateway_peer_entry_t> &entries_);
    void send_bootstrap_reply (void *router_,
                               const zlink_routing_id_t &sender_id_);
    void upsert_topology_entry (const zlink_registry_topology_entry_t &entry_,
                                uint64_t now_ms_);
    void upsert_gateway_peer_entry (
      const zlink_registry_gateway_peer_entry_t &entry_,
      uint64_t now_ms_);
    void send_service_list (void *pub_);
    void remove_expired (uint64_t now_ms_);

    ctx_t *_ctx;
    uint32_t _tag;
    service_runtime_base_t _lifecycle;
    service_public_api_guard_t _public_api;

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
    uint64_t _task_id;
    void *_pub_socket;
    void *_router_socket;
    void *_peer_sub_socket;
    std::set<std::string> _peer_connected;
    uint64_t _next_broadcast_ms;
    uint64_t _last_sent_seq;
    bool _started;
    uint64_t _next_socket_retry_ms;

    mutex_t _sync;

    service_map_t _services;
    std::map<topology_key_t, topology_entry_t> _topology;
    std::map<gateway_peer_key_t, gateway_peer_entry_t> _gateway_peers;
    std::map<uint32_t, uint64_t> _peer_seq;
    std::map<uint32_t, uint64_t> _peer_last_seen;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (registry_t)
};
}

#endif
