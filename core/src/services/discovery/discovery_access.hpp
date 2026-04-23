/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_ACCESS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_ACCESS_HPP_INCLUDED__

#include <zlink.h>

#include <string>

namespace zlink
{
class ctx_t;
class discovery_t;
class discovery_observer_t;

struct discovery_access_t
{
    static void *create (ctx_t *ctx_,
                         zlink_service_type_t service_type_,
                         const char *service_name_);
    static discovery_t *from_handle (void *discovery_);
    static int connect_registry (discovery_t *discovery_,
                                 const char *registry_endpoint_);
    static int set_dealer_peer_mode (
      discovery_t *discovery_, zlink_discovery_dealer_peer_mode_t mode_);
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
    static int set_metadata (discovery_t *discovery_,
                             const void *data_,
                             size_t size_);
    static int get_metadata (discovery_t *discovery_,
                             zlink_msg_t *metadata_out_);
    static int resolve_spot (discovery_t *discovery_,
                             const zlink_routing_id_t *spot_rid_,
                             zlink_routing_id_t *owner_node_rid_out_);
    static int member_peers (discovery_t *discovery_,
                             zlink_member_peer_entry_t *entries_,
                             size_t *count_);
    static int member_peer_metadata (discovery_t *discovery_,
                                     uint16_t service_role_,
                                     const char *endpoint_,
                                     zlink_msg_t *metadata_out_);
    static int destroy (discovery_t *discovery_);
    static void delete_handle (discovery_t *discovery_);
    static void *monitor_open (discovery_t *discovery_, int events_);
    static void set_summary_enabled (discovery_t *discovery_, bool enabled_);
    static int add_observer (discovery_t *discovery_,
                             discovery_observer_t *observer_);
    static int remove_observer (discovery_t *discovery_,
                                discovery_observer_t *observer_);
    static void upsert_service_summary (
      discovery_t *discovery_, const zlink_registry_topology_entry_t &entry_);
    static void flush_topology_reports (discovery_t *discovery_);
};
}

#endif
