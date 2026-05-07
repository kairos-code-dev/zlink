/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_ACCESS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_ACCESS_HPP_INCLUDED__

#include <zlink.h>

#include "services/discovery/route_limits_internal.hpp"

#include "services/discovery/discovery.hpp"

#include <vector>

namespace zlink
{
class ctx_t;
class discovery_t;
class discovery_observer_t;
class discovery_bootstrap_runtime_t;
class discovery_uplink_runtime_t;
class mutex_t;
class service_public_api_guard_t;
class socket_base_t;

struct discovery_access_t
{
    static void *create (ctx_t *ctx_,
                         zlink_auto_connect_type_t auto_connect_type_,
                         const char *channel_name_);
    static discovery_t *from_handle (void *discovery_);
    static int connect_registry (discovery_t *discovery_,
                                 const char *registry_endpoint_);
    static int set_tls_client (discovery_t *discovery_,
                               const char *ca_cert_,
                               const char *hostname_,
                               int trust_system_);
    static int set_option (discovery_t *discovery_,
                           int option_,
                           const void *optval_,
                           size_t optvallen_);
    static int get_option (discovery_t *discovery_,
                           int option_,
                           void *optval_,
                           size_t *optvallen_);
    static int set_routing_id (discovery_t *discovery_,
                               const void *data_,
                               size_t size_);
    static int routing_id (discovery_t *discovery_, zlink_routing_id_t *out_);
    static int set_value (discovery_t *discovery_, int64_t value_);
    static int get_value (discovery_t *discovery_, int64_t *value_out_);
    static int resolve_spot (discovery_t *discovery_,
                             const zlink_routing_id_t *spot_rid_,
                             zlink_routing_id_t *owner_node_rid_out_);
    static int bind_route (discovery_t *discovery_,
                           zlink_route_kind_t kind_,
                           const void *key_,
                           size_t key_size_,
                           const void *value_,
                           size_t value_size_);
    static int unbind_route (discovery_t *discovery_,
                             zlink_route_kind_t kind_,
                             const void *key_,
                             size_t key_size_);
    static int resolve_route (discovery_t *discovery_,
                              zlink_route_kind_t kind_,
                              const void *key_,
                              size_t key_size_,
                              zlink_routing_id_t *owner_rid_out_,
                              zlink_msg_t *value_out_);
    static int member_peers (discovery_t *discovery_,
                             zlink_member_peer_entry_t *entries_,
                             size_t *count_);
    static int destroy (discovery_t *discovery_);
    static void delete_handle (discovery_t *discovery_);
    static void set_summary_enabled (discovery_t *discovery_, bool enabled_);
    static int add_observer (discovery_t *discovery_,
                             discovery_observer_t *observer_);
    static int remove_observer (discovery_t *discovery_,
                                discovery_observer_t *observer_);
    static void upsert_service_summary (
      discovery_t *discovery_, const zlink_registry_topology_entry_t &entry_);
    static void flush_topology_reports (discovery_t *discovery_);
    static mutex_t &sync (discovery_t *discovery_);
    static mutex_t &uplink_sync (discovery_t *discovery_);
    static service_public_api_guard_t &public_api_guard (
      discovery_t *discovery_);
    static bool bootstrap_socket_config_locked (discovery_t *discovery_,
                                                bool routing_id_locked_);
    static void apply_socket_option_to_sub_socket (discovery_t *discovery_,
                                                   int option_,
                                                   const void *optval_,
                                                   size_t optvallen_);
    static discovery_bootstrap_runtime_t *bootstrap_runtime (
      discovery_t *discovery_);
    static discovery_uplink_runtime_t *uplink_runtime (
      discovery_t *discovery_);
    static int ensure_control_task_active (discovery_t *discovery_);
    static void emit_ready_changed (discovery_t *discovery_,
                                    uint32_t ready_count_);
    static int ensure_topology_reporters (discovery_t *discovery_);
    static socket_base_t *create_tracked_socket (discovery_t *discovery_,
                                                 int socket_type_);
    static int close_tracked_socket (discovery_t *discovery_,
                                     socket_base_t *&socket_,
                                     int timeout_ms_);
    static int close_tracked_socket_and_wait (discovery_t *discovery_,
                                              socket_base_t *&socket_,
                                              int timeout_ms_);
    static bool should_publish_summary_entry (
      discovery_t *discovery_, const zlink_registry_topology_entry_t &entry_);
    static void collect_dirty_summary_entries (
      discovery_t *discovery_,
      std::vector<zlink_registry_topology_entry_t> *out_);
    static void mark_summary_entries_sent (
      discovery_t *discovery_,
      const std::vector<zlink_registry_topology_entry_t> &entries_,
      const std::vector<bool> &sent_);
    static void collect_registered_services_for_heartbeat (
      discovery_t *discovery_,
      uint64_t now_ms_,
      uint32_t heartbeat_interval_ms_,
      std::vector<discovery_registered_service_snapshot_t> *out_);
    static void mark_registered_service_heartbeat (
      discovery_t *discovery_,
      const discovery_registered_service_snapshot_t &service_,
      uint64_t now_ms_);
};
}

#endif
