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
    int set_tls_server (const char *cert_,
                        const char *key_,
                        int require_client_cert_);
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    int topology_snapshot (zlink_registry_topology_entry_t *entries_,
                           size_t *count_);
    int topology_query (const zlink_registry_topology_filter_t *filter_,
                        zlink_registry_topology_entry_t *entries_,
                        size_t *count_);
    int member_peers (zlink_service_type_t service_type_,
                      const char *service_name_,
                      zlink_member_peer_entry_t *entries_,
                      size_t *count_);
    int member_peer_metadata (zlink_service_type_t service_type_,
                              const char *service_name_,
                              uint16_t service_role_,
                              const char *endpoint_,
                              zlink_msg_t *metadata_out_);
    int status_snapshot (zlink_registry_status_t *out_);
    int service_summary_snapshot (
      const zlink_registry_service_summary_filter_t *filter_,
      std::vector<zlink_registry_service_summary_entry_t> *out_);
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
        uint16_t service_role;
        std::string endpoint;
        zlink_routing_id_t routing_id;
        zlink_admission_state_t admission_state;
        int64_t value;
        std::vector<unsigned char> metadata;
        uint64_t registered_at;
        uint64_t last_heartbeat;
        uint32_t source_registry;
    };

    struct provider_key_t
    {
        uint16_t service_role;
        std::string endpoint;

        bool operator< (const provider_key_t &other_) const
        {
            if (service_role != other_.service_role)
                return service_role < other_.service_role;
            return endpoint < other_.endpoint;
        }
    };

    typedef std::map<provider_key_t, provider_entry_t> provider_map_t;

    struct service_entry_t
    {
        provider_map_t providers;
    };

    typedef std::map<service_key_t, service_entry_t> service_map_t;

    struct topology_key_t
    {
        uint16_t service_kind;
        uint16_t service_role;
        std::string routing_id_key;
        std::string service_name;
        std::string endpoint;

        bool operator< (const topology_key_t &other_) const
        {
            if (service_kind != other_.service_kind)
                return service_kind < other_.service_kind;
            if (service_role != other_.service_role)
                return service_role < other_.service_role;
            if (routing_id_key != other_.routing_id_key)
                return routing_id_key < other_.routing_id_key;
            if (service_name != other_.service_name)
                return service_name < other_.service_name;
            return endpoint < other_.endpoint;
        }
    };

    struct topology_entry_t
    {
        zlink_registry_topology_entry_t entry;
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
    void handle_update_attributes (void *router_,
                                   const zlink_msg_t *frames_,
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
    void collect_topology_entries_locked (
      const zlink_registry_topology_filter_t *filter_,
      std::vector<zlink_registry_topology_entry_t> *out_) const;
    void collect_matching_topology_entries_locked (
      const zlink_registry_topology_filter_t *filter_,
      std::vector<zlink_registry_topology_entry_t> *out_) const;
    bool select_spot_owner_entry_locked (
      const std::vector<zlink_registry_topology_entry_t> &matched_,
      const char *service_name_,
      zlink_registry_topology_entry_t *entry_out_) const;
    void send_bootstrap_reply (void *router_,
                               const zlink_routing_id_t &sender_id_);
    void upsert_topology_entry (const zlink_registry_topology_entry_t &entry_,
                                uint64_t now_ms_);
    void send_service_list (void *pub_);
    void remove_expired (uint64_t now_ms_);

    ctx_t *_ctx;
    uint32_t _tag;
    service_runtime_base_t _lifecycle;
    service_public_api_guard_t _public_api;

    struct socket_opt_t
    {
        int option;
        std::vector<unsigned char> value;
    };
    void apply_socket_opts (socket_base_t *socket_,
                            const std::vector<socket_opt_t> &opts_);
    void promote_runtime_sockets (socket_base_t *pub_,
                                  socket_base_t *router_,
                                  uint64_t now_ms_,
                                  socket_base_t **old_pub_out_,
                                  socket_base_t **old_router_out_);
    int ensure_peer_sub_socket ();
    void connect_peer_sub_endpoints (void *peer_sub_,
                                     const std::vector<std::string> &peer_pubs_);

    mutex_t _sync;

    struct endpoint_config_t
    {
        std::string pub_endpoint;
        std::string router_endpoint;
        std::vector<std::string> peer_pubs;
    };

    struct coordination_state_t
    {
        coordination_state_t () :
            registry_id (0),
            registry_id_set (false),
            list_seq (0),
            last_summary_error (0),
            summary_last_changed_ms (0),
            heartbeat_interval_ms (5000),
            heartbeat_timeout_ms (15000),
            broadcast_interval_ms (30000)
        {
        }

        uint32_t registry_id;
        bool registry_id_set;
        uint64_t list_seq;
        int last_summary_error;
        uint64_t summary_last_changed_ms;
        uint32_t heartbeat_interval_ms;
        uint32_t heartbeat_timeout_ms;
        uint32_t broadcast_interval_ms;
    };

    struct socket_option_state_t
    {
        std::vector<socket_opt_t> pub_opts;
        std::vector<socket_opt_t> router_opts;
        std::vector<socket_opt_t> peer_sub_opts;
    };

    struct runtime_socket_state_t
    {
        runtime_socket_state_t () :
            stop (0),
            task_id (0),
            pub_socket (NULL),
            router_socket (NULL),
            peer_sub_socket (NULL),
            next_broadcast_ms (0),
            last_sent_seq (0),
            started (false),
            next_socket_retry_ms (0)
        {
        }

        atomic_counter_t stop;
        uint64_t task_id;
        void *pub_socket;
        void *router_socket;
        void *peer_sub_socket;
        std::set<std::string> peer_connected;
        uint64_t next_broadcast_ms;
        uint64_t last_sent_seq;
        bool started;
        uint64_t next_socket_retry_ms;
    };

    struct projection_state_t
    {
        projection_state_t () : metadata_max_size (4096) {}

        service_map_t services;
        std::map<topology_key_t, topology_entry_t> topology;
        std::map<uint32_t, uint64_t> peer_seq;
        std::map<uint32_t, uint64_t> peer_last_seen;
        size_t metadata_max_size;
    };

    endpoint_config_t _endpoint_config;
    coordination_state_t _coordination_state;
    socket_option_state_t _socket_option_state;
    runtime_socket_state_t _runtime_socket_state;
    projection_state_t _projection_state;

    std::string &_pub_endpoint;
    std::string &_router_endpoint;
    std::vector<std::string> &_peer_pubs;
    uint32_t &_registry_id;
    bool &_registry_id_set;
    uint64_t &_list_seq;
    int &_last_summary_error;
    uint64_t &_summary_last_changed_ms;
    uint32_t &_heartbeat_interval_ms;
    uint32_t &_heartbeat_timeout_ms;
    uint32_t &_broadcast_interval_ms;
    std::vector<socket_opt_t> &_pub_opts;
    std::vector<socket_opt_t> &_router_opts;
    std::vector<socket_opt_t> &_peer_sub_opts;
    atomic_counter_t &_stop;
    uint64_t &_task_id;
    void *&_pub_socket;
    void *&_router_socket;
    void *&_peer_sub_socket;
    std::set<std::string> &_peer_connected;
    uint64_t &_next_broadcast_ms;
    uint64_t &_last_sent_seq;
    bool &_started;
    uint64_t &_next_socket_retry_ms;
    service_map_t &_services;
    std::map<topology_key_t, topology_entry_t> &_topology;
    std::map<uint32_t, uint64_t> &_peer_seq;
    std::map<uint32_t, uint64_t> &_peer_last_seen;
    size_t &_metadata_max_size;

    ZLINK_NON_COPYABLE_NOR_MOVABLE (registry_t)
};
}

#endif
