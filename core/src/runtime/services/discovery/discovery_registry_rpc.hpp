/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_REGISTRY_RPC_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_REGISTRY_RPC_HPP_INCLUDED__

#include "zlink.h"

#include <string>
#include <vector>

namespace zlink
{
class ctx_t;
class socket_base_t;
struct discovery_bootstrap_runtime_t;

namespace discovery_registry_rpc
{
int close_dealer (ctx_t *ctx_, socket_base_t *&dealer_);
int prepare_transient_dealer (ctx_t *ctx_,
                              discovery_bootstrap_runtime_t *bootstrap_runtime_,
                              const std::string &uplink_,
                              const zlink_routing_id_t *routing_id_,
                              socket_base_t **dealer_out_);
int prepare_query_dealer (ctx_t *ctx_, const char *endpoint_, socket_base_t **dealer_out_);
int recv_status_ack (socket_base_t *socket_,
                     uint16_t expected_msg_id_,
                     int *status_out_,
                     std::string *resolved_out_,
                     uint32_t *source_registry_out_,
                     uint64_t *registration_id_out_,
                     std::string *error_out_);
int recv_topology_reply_entries (socket_base_t *socket_,
                                 std::vector<zlink_registry_topology_entry_t> *entries_out_);
int recv_route_reply (socket_base_t *socket_,
                      zlink_routing_id_t *owner_rid_out_,
                      zlink_msg_t *value_out_);
}
}

#endif
