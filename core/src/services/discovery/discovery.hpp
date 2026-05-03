/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_DISCOVERY_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_DISCOVERY_HPP_INCLUDED__

#include "core/ctx.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_runtime_base.hpp"
#include "services/discovery/discovery_observer.hpp"
#include "services/discovery/discovery_runtime_internal.hpp"
#include "utils/atomic_counter.hpp"
#include "utils/mutex.hpp"

#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
class discovery_t;
class spot_node_t;
class service_control_runtime_t;
struct discovery_access_t;

enum discovery_socket_role_t
{
    discovery_socket_sub = 1
};

class discovery_t
{
  public:
    discovery_t (ctx_t *ctx_, uint16_t auto_connect_type_,
                 const std::string &channel_name_);
    ~discovery_t ();

    bool check_tag () const;

    int connect_registry (const char *registry_endpoint_);
    int set_routing_id (const void *data_, size_t size_);
    int routing_id (zlink_routing_id_t *out_) const;
    int set_option (int option_, const void *optval_, size_t optvallen_);
    int get_option (int option_, void *optval_, size_t *optvallen_) const;
    int set_tls_client (const char *ca_cert_,
                        const char *hostname_,
                        int trust_system_);
    int set_value (int64_t value_);
    int get_value (int64_t *value_out_) const;
    int resolve_spot (const zlink_routing_id_t *spot_rid_,
                      zlink_routing_id_t *owner_node_rid_out_);
    int bind_route (zlink_route_kind_t kind_,
                    const void *key_,
                    size_t key_size_,
                    const void *value_,
                    size_t value_size_);
    int unbind_route (zlink_route_kind_t kind_,
                      const void *key_,
                      size_t key_size_);
    int resolve_route (zlink_route_kind_t kind_,
                       const void *key_,
                       size_t key_size_,
                       zlink_routing_id_t *owner_rid_out_,
                       zlink_msg_t *value_out_);
    void snapshot_member_peers (
      std::vector<zlink_member_peer_entry_t> *out_) const;
    int member_peers (zlink_member_peer_entry_t *entries_, size_t *count_) const;
    int destroy ();
    int register_service (const char *endpoint_,
                          uint32_t weight_,
                          int64_t value_,
                          const std::vector<unsigned char> *metadata_,
                          std::string *resolved_endpoint_out_,
                          const zlink_routing_id_t *routing_id_ = NULL,
                          uint16_t service_role_ = 0);
    int update_service_attributes (const char *endpoint_,
                                   uint32_t weight_,
                                   int64_t value_,
                                   const std::vector<unsigned char> *metadata_,
                                   uint16_t service_role_ = 0);
    int unregister_service (const char *endpoint_,
                            uint16_t service_role_ = 0);

    uint16_t auto_connect_type () const { return _auto_connect_type; }
    const std::string &channel_name () const { return _channel_name; }
    bool spot_owner_sync_enabled () const;

    void snapshot_providers (const std::string &channel_name_,
                             std::vector<provider_info_t> *out_);
    bool latest_registry_uplink (std::string *out_);
    uint64_t update_seq ();
    uint64_t service_update_seq (const std::string &channel_name_);
    service_public_api_guard_t &public_api_guard_for_testing ()
    {
        return _public_api;
    }
    void notify_observers_for_testing (const std::set<std::string> &services_)
    {
        notify_observers (services_);
    }

  private:
    friend class spot_node_t;
    friend struct discovery_access_t;
    friend class discovery_bootstrap_runtime_t;
    friend class discovery_bootstrap_socket_config_t;
    friend class discovery_uplink_runtime_t;

    static void control_task (void *arg_);
    void tick ();
    void set_discovery_summary_enabled (bool enabled_);
    int add_observer (discovery_observer_t *observer_);
    int remove_observer (discovery_observer_t *observer_);
    void upsert_service_summary (const zlink_registry_topology_entry_t &entry_);
    int ensure_sub_socket ();
    void close_sub_socket ();
    int bootstrap_registry (const char *registry_endpoint_);
    void handle_service_list (const std::vector<zlink_msg_t> &frames_);
    void notify_observers (const std::set<std::string> &services_);
    void emit_ready_changed (uint32_t ready_count_);
    int ensure_topology_reporters ();
    void flush_topology_reports ();
    void refresh_registered_service_heartbeats (uint64_t now_ms_);
    socket_base_t *create_tracked_socket (int socket_type_);
    int close_tracked_socket (socket_base_t *&socket_, int timeout_ms_);
    int close_tracked_socket_and_wait (socket_base_t *&socket_, int timeout_ms_);
    service_control_runtime_t *control_runtime () const;
    int ensure_control_task_active ();

    struct topology_key_t
    {
        uint16_t service_kind;
        uint16_t service_role;
        std::string routing_id_key;
        std::string channel_name;

        bool operator< (const topology_key_t &other_) const
        {
            if (service_kind != other_.service_kind)
                return service_kind < other_.service_kind;
            if (service_role != other_.service_role)
                return service_role < other_.service_role;
            if (routing_id_key != other_.routing_id_key)
                return routing_id_key < other_.routing_id_key;
            return channel_name < other_.channel_name;
        }
    };

    struct topology_summary_t
    {
        zlink_registry_topology_entry_t entry;
        bool dirty;
        bool tombstone;
        uint64_t validated_service_seq;
    };

    topology_key_t make_summary_key (uint16_t service_kind_,
                                     uint16_t service_role_,
                                     const zlink_routing_id_t &routing_id_,
                                     const std::string &channel_name_) const;
    void store_summary_entry_locked (const topology_key_t &key_,
                                     const zlink_registry_topology_entry_t &entry_,
                                     bool dirty_,
                                     bool tombstone_,
                                     uint64_t validated_service_seq_);
    bool should_publish_summary_entry_locked (
      const zlink_registry_topology_entry_t &entry_) const;
    void mark_spot_owner_summaries_dirty_locked (bool stopped_,
                                                 uint64_t now_ms_);
    topology_key_t make_spot_topology_key (
      const zlink_routing_id_t &spot_rid_) const;
    bool resolve_owner_node_from_endpoint_locked (
      const char *endpoint_, zlink_routing_id_t *owner_node_rid_out_) const;
    bool try_resolve_spot_from_cache_locked (
      const topology_key_t &key_,
      uint64_t now_ms_,
      zlink_routing_id_t *owner_node_rid_out_) const;
    int query_spot_owner_entries_from_registry (
      const zlink_routing_id_t *spot_rid_,
      std::vector<zlink_registry_topology_entry_t> *entries_out_);
    void refresh_spot_owner_cache_locked (
      const topology_key_t &key_,
      const std::vector<zlink_registry_topology_entry_t> &entries_);

    struct registered_service_key_t
    {
        uint16_t service_role;
        std::string channel_name;
        std::string endpoint;

        bool operator< (const registered_service_key_t &other_) const
        {
            if (service_role != other_.service_role)
                return service_role < other_.service_role;
            if (channel_name != other_.channel_name)
                return channel_name < other_.channel_name;
            return endpoint < other_.endpoint;
        }
    };

    struct registered_service_t
    {
        uint16_t service_role;
        std::string channel_name;
        std::string endpoint;
        std::string uplink_endpoint;
        uint32_t weight;
        int64_t value;
        std::vector<unsigned char> metadata;
        zlink_routing_id_t routing_id;
        uint32_t source_registry;
        uint64_t registration_id;
        uint64_t last_heartbeat_ms;

        registered_service_t () :
            service_role (0),
            weight (100),
            value (0),
            source_registry (0),
            registration_id (0),
            last_heartbeat_ms (0)
        {
            memset (&routing_id, 0, sizeof (routing_id));
        }
    };

    void snapshot_registered_service_updates (
      std::vector<registered_service_t> *services_out_,
      int64_t *value_out_) const;
    int propagate_registered_service_updates (
      const std::vector<registered_service_t> &services_,
      int64_t value_);
    bool select_route_owner_locked (registered_service_t *owner_out_) const;

    ctx_t *_ctx;
    uint32_t _tag;
    service_runtime_base_t _lifecycle;
    mutable service_public_api_guard_t _public_api;

    atomic_counter_t _stop;
    uint64_t _task_id;
    void *_sub_socket;
    std::set<std::string> _connected_endpoints;

    mutable mutex_t _sync;
    mutex_t _uplink_sync;
    discovery_bootstrap_runtime_t *_bootstrap_runtime;
    discovery_uplink_runtime_t *_uplink_runtime;
    discovery_service_state_t _service_state;
    uint32_t _monitor_ready_count;
    uint16_t _auto_connect_type;
    std::string _channel_name;
    bool _discovery_summary_enabled;
    std::map<registered_service_key_t, registered_service_t> _registered_services;
    discovery_local_state_t _local_state;
    std::map<topology_key_t, topology_summary_t> _summary_store;
    ZLINK_NON_COPYABLE_NOR_MOVABLE (discovery_t)
};
}

#endif
