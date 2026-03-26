/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_COMPAT_HPP_INCLUDED
#define ZLINK_CPP_COMPAT_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{
namespace compat
{

ZLINK_CPP_DEPRECATED ("Use zlink_errno().")
inline int err_no ()
{
    return zlink_errno ();
}

ZLINK_CPP_DEPRECATED ("Use zlink_strerror().")
inline const char *strerror (int code_)
{
    return zlink_strerror (code_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_has().")
inline int has (const char *capability_)
{
    return zlink_has (capability_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_ctx_new().")
inline void *ctx_new ()
{
    return zlink_ctx_new ();
}

ZLINK_CPP_DEPRECATED ("Use zlink_ctx_set().")
inline int ctx_set (void *ctx_, int option_, int value_)
{
    return zlink_ctx_set (
      ctx_, static_cast<zlink_ctx_option_t> (option_), value_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_ctx_shutdown().")
inline int ctx_shutdown (void *ctx_)
{
    return zlink_ctx_shutdown (ctx_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_ctx_term().")
inline int ctx_term (void *ctx_)
{
    return zlink_ctx_term (ctx_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_socket(ctx, type).")
inline void *socket (void *ctx_, int type_)
{
    return zlink_socket (ctx_, static_cast<zlink_socket_type_t> (type_));
}

ZLINK_CPP_DEPRECATED ("Use zlink_close().")
inline int close (void *socket_)
{
    return zlink_close (socket_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_bind().")
inline int bind (void *socket_, const char *endpoint_)
{
    return zlink_bind (socket_, endpoint_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_connect().")
inline int connect (void *socket_, const char *endpoint_)
{
    return zlink_connect (socket_, endpoint_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_unbind().")
inline int unbind (void *socket_, const char *endpoint_)
{
    return zlink_unbind (socket_, endpoint_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_disconnect().")
inline int disconnect (void *socket_, const char *endpoint_)
{
    return zlink_disconnect (socket_, endpoint_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_send() multipart API.")
inline int send (void *socket_,
                 zlink_msg_t *parts_,
                 size_t part_count_,
                 int flags_)
{
    return zlink_send (
      socket_, parts_, part_count_, static_cast<zlink_send_flags_t> (flags_));
}

ZLINK_CPP_DEPRECATED ("Use zlink_recv() multipart API.")
inline int recv (void *socket_,
                 zlink_routing_id_t *source_rid_out_,
                 zlink_msg_t **parts_out_,
                 size_t *part_count_out_,
                 int flags_)
{
    return zlink_recv (socket_, source_rid_out_, parts_out_, part_count_out_,
                       static_cast<zlink_send_flags_t> (flags_));
}

ZLINK_CPP_DEPRECATED ("Use zlink_publish().")
inline int publish (void *subject_,
                    const char *topic_id_,
                    zlink_msg_t *parts_,
                    size_t part_count_,
                    int flags_)
{
    return zlink_publish (
      subject_, topic_id_, parts_, part_count_,
      static_cast<zlink_send_flags_t> (flags_));
}

ZLINK_CPP_DEPRECATED ("Use zlink_set_option().")
inline int set_option (void *handle_,
                       int option_,
                       const void *value_,
                       size_t size_)
{
    return zlink_set_option (
      handle_, static_cast<zlink_option_t> (option_), value_, size_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_get_option().")
inline int get_option (void *handle_,
                       int option_,
                       void *value_,
                       size_t *size_)
{
    return zlink_get_option (
      handle_, static_cast<zlink_option_t> (option_), value_, size_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_set_routing_id().")
inline int set_routing_id (void *handle_, const void *data_, size_t size_)
{
    return zlink_set_routing_id (handle_, data_, size_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_get_routing_id().")
inline int get_routing_id (void *handle_, zlink_routing_id_t *out_)
{
    return zlink_get_routing_id (handle_, out_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_set_tls_server().")
inline int set_tls_server (void *handle_,
                           const char *cert_,
                           const char *key_,
                           int require_client_cert_)
{
    return zlink_set_tls_server (
      handle_, cert_, key_, require_client_cert_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_set_tls_client().")
inline int set_tls_client (void *handle_,
                           const char *ca_cert_,
                           const char *hostname_,
                           int trust_system_)
{
    return zlink_set_tls_client (
      handle_, ca_cert_, hostname_, trust_system_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_set_subscription().")
inline int set_subscription (void *handle_, const char *filter_)
{
    return zlink_set_subscription (handle_, filter_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_unset_subscription().")
inline int unset_subscription (void *handle_, const char *filter_)
{
    return zlink_unset_subscription (handle_, filter_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_subscription_at().")
inline int subscription_at (void *handle_,
                            size_t index_,
                            char *filter_out_,
                            size_t *filter_len_inout_,
                            int *is_pattern_out_)
{
    return zlink_subscription_at (
      handle_, index_, filter_out_, filter_len_inout_, is_pattern_out_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_socket_monitor_open().")
inline void *
socket_monitor_open (void *socket_, zlink_socket_monitor_event_mask_t events_)
{
    zlink_socket_monitor_open_options_t options;
    options.events = events_;
    return zlink_socket_monitor_open (socket_, &options);
}

ZLINK_CPP_DEPRECATED ("Use zlink_socket_monitor_recv().")
inline int monitor_recv (void *monitor_, zlink_socket_monitor_event_t *event_)
{
    return zlink_socket_monitor_recv (monitor_, event_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_monitor_snapshot().")
inline int monitor_snapshot (void *monitor_, zlink_monitor_snapshot_t *snapshot_)
{
    return zlink_monitor_snapshot (monitor_, snapshot_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_monitor_close().")
inline int monitor_close (void **monitor_)
{
    return zlink_monitor_close (monitor_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_service_monitor_open().")
inline void *
service_monitor_open (void *target_, zlink_service_monitor_event_mask_t events_)
{
    zlink_service_monitor_open_options_t options;
    options.events = events_;
    return zlink_service_monitor_open (target_, &options);
}

ZLINK_CPP_DEPRECATED ("Use zlink_service_monitor_recv().")
inline int
service_monitor_recv (void *monitor_, zlink_service_monitor_event_t *event_)
{
    return zlink_service_monitor_recv (monitor_, event_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_service_monitor_handler().")
inline int service_monitor_handler (void *monitor_,
                                    zlink_service_monitor_handler_fn handler_,
                                    void *userdata_)
{
    return zlink_service_monitor_handler (monitor_, handler_, userdata_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_socket_monitor_handler().")
inline int socket_monitor_handler (void *monitor_,
                                   zlink_socket_monitor_handler_fn handler_,
                                   void *userdata_)
{
    return zlink_socket_monitor_handler (monitor_, handler_, userdata_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_registry_new().")
inline void *registry_new (void *ctx_)
{
    return zlink_registry_new (ctx_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_registry_bind().")
inline int registry_bind (void *registry_,
                          const char *pub_endpoint_,
                          const char *router_endpoint_)
{
    return zlink_registry_bind (registry_, pub_endpoint_, router_endpoint_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_registry_destroy().")
inline int registry_destroy (void **registry_)
{
    return zlink_registry_destroy (registry_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_discovery_new(ctx, type, name).")
inline void *discovery_new (void *ctx_,
                            zlink_service_type_t service_type_,
                            const char *service_name_)
{
    return zlink_discovery_new (ctx_, service_type_, service_name_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_discovery_connect_registry().")
inline int discovery_connect_registry (void *discovery_, const char *endpoint_)
{
    return zlink_discovery_connect_registry (discovery_, endpoint_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_discovery_destroy().")
inline int discovery_destroy (void **discovery_)
{
    return zlink_discovery_destroy (discovery_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_spot_new().")
inline void *spot_new (void *ctx_)
{
    return zlink_spot_new (ctx_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_spot_destroy().")
inline int spot_destroy (void **spot_)
{
    return zlink_spot_destroy (spot_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_spot_node_new().")
inline void *spot_node_new (void *ctx_)
{
    return zlink_spot_node_new (ctx_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_spot_node_attach_discovery().")
inline int spot_node_attach_discovery (void *node_, void *discovery_)
{
    return zlink_spot_node_attach_discovery (node_, discovery_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_spot_node_destroy().")
inline int spot_node_destroy (void **node_)
{
    return zlink_spot_node_destroy (node_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_poll().")
inline int poll (zlink_pollitem_t *items_, int count_, long timeout_ms_)
{
    return zlink_poll (items_, count_, timeout_ms_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_init().")
inline int msg_init (zlink_msg_t *msg_)
{
    return zlink_msg_init (msg_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_init_size().")
inline int msg_init_size (zlink_msg_t *msg_, size_t size_)
{
    return zlink_msg_init_size (msg_, size_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_init_data().")
inline int msg_init_data (zlink_msg_t *msg_,
                          void *data_,
                          size_t size_,
                          zlink_free_fn *ffn_,
                          void *hint_)
{
    return zlink_msg_init_data (msg_, data_, size_, ffn_, hint_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_close().")
inline int msg_close (zlink_msg_t *msg_)
{
    return zlink_msg_close (msg_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_move().")
inline int msg_move (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return zlink_msg_move (dest_, src_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_copy().")
inline int msg_copy (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return zlink_msg_copy (dest_, src_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_data().")
inline void *msg_data (zlink_msg_t *msg_)
{
    return zlink_msg_data (msg_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_size().")
inline size_t msg_size (const zlink_msg_t *msg_)
{
    return zlink_msg_size (const_cast<zlink_msg_t *> (msg_));
}

ZLINK_CPP_DEPRECATED ("Use zlink_msg_refcnt().")
inline int msg_refcnt (const zlink_msg_t *msg_)
{
    return zlink_msg_refcnt (const_cast<zlink_msg_t *> (msg_));
}

ZLINK_CPP_DEPRECATED ("Use zlink_multipart_close().")
inline void multipart_close (zlink_msg_t *parts_, size_t count_)
{
    zlink_multipart_close (parts_, count_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_recv_handler().")
inline int recv_handler (void *socket_,
                         zlink_socket_msg_handler_fn handler_,
                         void *userdata_)
{
    return zlink_recv_handler (socket_, handler_, userdata_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_subscribe_handler().")
inline int subscribe_handler (void *subject_,
                              zlink_subscribe_handler_fn handler_,
                              void *userdata_)
{
    return zlink_subscribe_handler (subject_, handler_, userdata_);
}

ZLINK_CPP_DEPRECATED ("Use zlink_send_ready_handler().")
inline int send_ready_handler (void *subject_,
                               zlink_send_ready_handler_fn handler_,
                               void *userdata_)
{
    return zlink_send_ready_handler (subject_, handler_, userdata_);
}

} // namespace compat
} // namespace zlink

#endif
