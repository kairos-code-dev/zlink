/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_DISCOVERY_GATEWAY_ACCESS_HPP_INCLUDED__
#define __ZLINK_DISCOVERY_GATEWAY_ACCESS_HPP_INCLUDED__

#include <zlink.h>

namespace zlink
{
class ctx_t;
class discovery_t;
class gateway_t;
class socket_base_t;
class service_public_api_guard_t;

struct gateway_access_t
{
    static void *create (ctx_t *ctx_);
    static gateway_t *from_handle (void *gateway_);
    static int begin_close_or_fail_busy (gateway_t *gateway_);
    static void cancel_close (gateway_t *gateway_);
    static int attach_discovery (gateway_t *gateway_, void *discovery_);
    static int bind (gateway_t *gateway_, const char *bind_endpoint_);
    static int connect (gateway_t *gateway_,
                        const char *endpoint_,
                        const zlink_routing_id_t *routing_id_);
    static int disconnect (gateway_t *gateway_, const char *endpoint_);
    static int snapshot_status (gateway_t *gateway_,
                                zlink_gateway_status_t *out_);
    static int set_lb_strategy (gateway_t *gateway_,
                                zlink_gateway_lb_strategy_t strategy_);
    static int update_peer_weight (gateway_t *gateway_,
                                   const zlink_routing_id_t *routing_id_,
                                   uint32_t weight_);
    static int destroy (gateway_t *gateway_);
    static void delete_handle (gateway_t *gateway_);
    static int set_socket_option (gateway_t *gateway_,
                                  int option_,
                                  const void *optval_,
                                  size_t optvallen_);
    static int get_socket_option (gateway_t *gateway_,
                                  int option_,
                                  void *optval_,
                                  size_t *optvallen_);
    static int last_endpoint (gateway_t *gateway_,
                              char *buffer_,
                              size_t *size_inout_);
    static int set_routing_id (gateway_t *gateway_,
                               const void *data_,
                               size_t size_);
    static int routing_id (gateway_t *gateway_, zlink_routing_id_t *out_);
    static int set_tls_server (gateway_t *gateway_,
                               const char *cert_,
                               const char *key_);
    static int set_tls_client (gateway_t *gateway_,
                               const char *ca_cert_,
                               const char *hostname_,
                               int trust_system_);
    static int send (gateway_t *gateway_,
                     zlink_msg_t *parts_,
                     size_t part_count_,
                     zlink_send_flags_t flags_);
    static int send_rid (gateway_t *gateway_,
                         const zlink_routing_id_t *routing_id_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         zlink_send_flags_t flags_);
    static service_public_api_guard_t *public_api_guard (gateway_t *gateway_);
    static int set_recv_handler (gateway_t *gateway_,
                                 zlink_socket_msg_handler_fn handler_,
                                 void *userdata_);
    static int set_send_ready_handler (gateway_t *gateway_,
                                       zlink_send_ready_handler_fn handler_,
                                       void *userdata_);
    static void *monitor_open (gateway_t *gateway_, int events_);
    static socket_base_t *router_socket (gateway_t *gateway_);
    static void dispatch_send_ready (gateway_t *gateway_);
    static void dispatch_message (gateway_t *gateway_,
                                  const zlink_routing_id_t *source_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_);
};
}

#endif
