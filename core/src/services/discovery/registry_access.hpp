/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_REGISTRY_ACCESS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_REGISTRY_ACCESS_HPP_INCLUDED__

#include <zlink.h>

#include <vector>

namespace zlink
{
class ctx_t;
class registry_t;

struct registry_access_t
{
    static void *create (ctx_t *ctx_);
    static registry_t *from_handle (void *registry_);
    static int bind (registry_t *registry_,
                     const char *pub_endpoint_,
                     const char *router_endpoint_);
    static int set_id (registry_t *registry_, uint32_t registry_id_);
    static int add_peer (registry_t *registry_, const char *peer_pub_endpoint_);
    static int set_heartbeat (registry_t *registry_,
                              uint32_t interval_ms_,
                              uint32_t timeout_ms_);
    static int set_broadcast_interval (registry_t *registry_,
                                       uint32_t interval_ms_);
    static int destroy (registry_t *registry_);
    static void delete_handle (registry_t *registry_);
    static int set_tls_server (registry_t *registry_,
                               const char *cert_,
                               const char *key_,
                               int require_client_cert_);
    static int set_tls_client (registry_t *registry_,
                               const char *ca_cert_,
                               const char *hostname_,
                               int trust_system_);
    static int topology_snapshot (registry_t *registry_,
                                  zlink_registry_topology_entry_t *entries_,
                                  size_t *count_);
    static int status_snapshot (registry_t *registry_,
                                zlink_registry_status_t *out_);
    static int member_peers (registry_t *registry_,
                             const char *channel_name_,
                             zlink_member_peer_entry_t *entries_,
                             size_t *count_);
    static int service_summary_snapshot (
      registry_t *registry_,
      const zlink_registry_service_summary_filter_t *filter_,
      std::vector<zlink_registry_service_summary_entry_t> *out_);
    static int topology_query (
      registry_t *registry_,
      const zlink_registry_topology_filter_t *filter_,
      zlink_registry_topology_entry_t *entries_,
      size_t *count_);
};
}

#endif
