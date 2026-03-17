/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_COMPAT_HPP_INCLUDED
#define ZLINK_CPP_COMPAT_HPP_INCLUDED

#include "common.hpp"
#include <cerrno>
#include <cstdlib>

namespace zlink
{
namespace compat
{

namespace detail
{

inline int gateway_option_from_socket_option (int option_)
{
    switch (option_) {
    case ZLINK_SNDHWM:
        return ZLINK_GATEWAY_OPT_SNDHWM;
    case ZLINK_RCVHWM:
        return ZLINK_GATEWAY_OPT_RCVHWM;
    case ZLINK_SNDTIMEO:
        return ZLINK_GATEWAY_OPT_SNDTIMEO;
    case ZLINK_RCVTIMEO:
        return ZLINK_GATEWAY_OPT_RCVTIMEO;
    case ZLINK_LINGER:
        return ZLINK_GATEWAY_OPT_LINGER;
    case ZLINK_SNDBUF:
        return ZLINK_GATEWAY_OPT_SNDBUF;
    case ZLINK_RCVBUF:
        return ZLINK_GATEWAY_OPT_RCVBUF;
    default:
        errno = EINVAL;
        return -1;
    }
}

inline int receiver_option_from_socket_option (int option_)
{
    switch (option_) {
    case ZLINK_SNDHWM:
        return ZLINK_RECEIVER_OPT_SNDHWM;
    case ZLINK_RCVHWM:
        return ZLINK_RECEIVER_OPT_RCVHWM;
    case ZLINK_SNDTIMEO:
        return ZLINK_RECEIVER_OPT_SNDTIMEO;
    case ZLINK_RCVTIMEO:
        return ZLINK_RECEIVER_OPT_RCVTIMEO;
    case ZLINK_LINGER:
        return ZLINK_RECEIVER_OPT_LINGER;
    case ZLINK_SNDBUF:
        return ZLINK_RECEIVER_OPT_SNDBUF;
    case ZLINK_RCVBUF:
        return ZLINK_RECEIVER_OPT_RCVBUF;
    default:
        errno = EINVAL;
        return -1;
    }
}

inline int spot_pub_option_from_socket_option (int option_)
{
    switch (option_) {
    case ZLINK_SNDHWM:
        return ZLINK_SPOT_PUB_OPT_SNDHWM;
    case ZLINK_SNDTIMEO:
        return ZLINK_SPOT_PUB_OPT_SNDTIMEO;
    case ZLINK_LINGER:
        return ZLINK_SPOT_PUB_OPT_LINGER;
    case ZLINK_XPUB_NODROP:
        return ZLINK_SPOT_PUB_OPT_NODROP;
    case ZLINK_SNDBUF:
        return ZLINK_SPOT_PUB_OPT_SNDBUF;
    case ZLINK_RCVBUF:
        return ZLINK_SPOT_PUB_OPT_RCVBUF;
    default:
        errno = EINVAL;
        return -1;
    }
}

inline int spot_sub_option_from_socket_option (int option_)
{
    switch (option_) {
    case ZLINK_RCVHWM:
        return ZLINK_SPOT_SUB_OPT_RCVHWM;
    case ZLINK_RCVTIMEO:
        return ZLINK_SPOT_SUB_OPT_RCVTIMEO;
    case ZLINK_LINGER:
        return ZLINK_SPOT_SUB_OPT_LINGER;
    case ZLINK_SNDBUF:
        return ZLINK_SPOT_SUB_OPT_SNDBUF;
    case ZLINK_RCVBUF:
        return ZLINK_SPOT_SUB_OPT_RCVBUF;
    default:
        errno = EINVAL;
        return -1;
    }
}

} // namespace detail

inline int err_no () { return zlink_errno (); }
inline const char *strerror (int code_) { return zlink_strerror (code_); }
inline int has (const char *capability_) { return zlink_has (capability_); }

inline void *ctx_new () { return zlink_ctx_new (); }
inline int ctx_set (void *ctx_, int option_, int value_)
{
    return zlink_ctx_set (ctx_, option_, value_);
}
inline int ctx_shutdown (void *ctx_) { return zlink_ctx_shutdown (ctx_); }
inline int ctx_term (void *ctx_) { return zlink_ctx_term (ctx_); }

inline void *socket (void *ctx_, int type_) { return zlink_socket (ctx_, type_, NULL); }
inline int close (void *socket_) { return zlink_close (socket_); }
inline int bind (void *socket_, const char *endpoint_)
{
    return zlink_bind (socket_, endpoint_);
}
inline int connect (void *socket_, const char *endpoint_)
{
    return zlink_connect (socket_, endpoint_);
}
inline int send (void *socket_, const void *buf_, size_t len_, int flags_)
{
    return zlink_send (socket_, buf_, len_, flags_);
}
inline int recv (void *socket_, void *buf_, size_t len_, int flags_)
{
    return zlink_recv (socket_, buf_, len_, flags_);
}
inline int set_sockopt (void *socket_,
                        int option_,
                        const void *value_,
                        size_t len_)
{
    return zlink_setsockopt (socket_, option_, value_, len_);
}
inline int setsockopt (void *socket_,
                       int option_,
                       const void *value_,
                       size_t len_)
{
    return set_sockopt (socket_, option_, value_, len_);
}
inline int get_sockopt (void *socket_, int option_, void *value_, size_t *len_)
{
    return zlink_getsockopt (socket_, option_, value_, len_);
}
inline int getsockopt (void *socket_, int option_, void *value_, size_t *len_)
{
    return get_sockopt (socket_, option_, value_, len_);
}
inline int poll (zlink_pollitem_t *items_, int count_, long timeout_ms_)
{
    return zlink_poll (items_, count_, timeout_ms_);
}

inline int msg_init (zlink_msg_t *msg_) { return zlink_msg_init (msg_); }
inline int msg_close (zlink_msg_t *msg_) { return zlink_msg_close (msg_); }
inline int msg_recv (zlink_msg_t *msg_, void *socket_, int flags_)
{
    return zlink_msg_recv (msg_, socket_, flags_);
}
inline int msg_send (zlink_msg_t *msg_, void *socket_, int flags_)
{
    return zlink_msg_send (msg_, socket_, flags_);
}
inline void *msg_data (zlink_msg_t *msg_) { return zlink_msg_data (msg_); }
inline size_t msg_size (const zlink_msg_t *msg_)
{
    return zlink_msg_size (const_cast<zlink_msg_t *> (msg_));
}
inline int msg_more (const zlink_msg_t *msg_)
{
    return zlink_msg_more (const_cast<zlink_msg_t *> (msg_));
}
inline int msg_move (zlink_msg_t *dest_, zlink_msg_t *src_)
{
    return zlink_msg_move (dest_, src_);
}
inline int multipart_close (zlink_msg_t *parts_, size_t count_)
{
    if (!parts_)
        return 0;
    for (size_t i = 0; i < count_; ++i)
        zlink_msg_close (&parts_[i]);
    std::free (parts_);
    return 0;
}

inline void *socket_monitor_open (void *socket_, int events_)
{
    return zlink_socket_monitor_open (socket_, events_, NULL, NULL);
}
inline int socket_monitor (void *socket_, const char *addr_, int events_)
{
    return zlink_socket_monitor (socket_, addr_, events_);
}
inline int monitor_recv (void *monitor_, zlink_monitor_event_t *event_, int flags_)
{
    return zlink_monitor_recv (monitor_, event_, flags_);
}
inline int socket_peers (void *socket_, zlink_peer_info_t *peers_, size_t *count_)
{
    return zlink_socket_peers (socket_, peers_, count_);
}
inline int socket_peer_info (void *socket_,
                             const zlink_routing_id_t *routing_id_,
                             zlink_peer_info_t *info_)
{
    return zlink_socket_peer_info (
      socket_, const_cast<zlink_routing_id_t *> (routing_id_), info_);
}

inline void *registry_new (void *ctx_) { return zlink_registry_new (ctx_); }
inline int registry_destroy (void **registry_)
{
    return zlink_registry_destroy (registry_);
}
inline int registry_set_endpoints (void *registry_,
                                   const char *pub_,
                                   const char *router_)
{
    return zlink_registry_set_endpoints (registry_, pub_, router_);
}
inline int registry_set_broadcast_interval (void *registry_, uint32_t ivl_ms_)
{
    return zlink_registry_set_broadcast_interval (registry_, ivl_ms_);
}
inline int registry_set_heartbeat (void *registry_,
                                   uint32_t ivl_ms_,
                                   uint32_t timeout_ms_)
{
    return zlink_registry_set_heartbeat (registry_, ivl_ms_, timeout_ms_);
}
inline int registry_start (void *registry_) { return zlink_registry_start (registry_); }

inline void *discovery_new_typed (void *ctx_, uint16_t service_type_)
{
    return zlink_discovery_new_typed (ctx_, service_type_);
}
inline int discovery_destroy (void **discovery_)
{
    return zlink_discovery_destroy (discovery_);
}
inline int discovery_connect_registry (void *discovery_, const char *endpoint_)
{
    return zlink_discovery_connect_registry (discovery_, endpoint_);
}
inline int discovery_receiver_count (void *discovery_, const char *service_)
{
    return zlink_discovery_receiver_count (discovery_, service_);
}
inline int discovery_service_available (void *discovery_, const char *service_)
{
    return zlink_discovery_service_available (discovery_, service_);
}
inline int discovery_get_receivers (void *discovery_,
                                    const char *service_,
                                    zlink_receiver_info_t *providers_,
                                    size_t *count_)
{
    return zlink_discovery_get_receivers (
      discovery_, service_, providers_, count_);
}

inline void *receiver_new (void *ctx_, const char *routing_id_)
{
    return zlink_receiver_new (ctx_, routing_id_);
}
inline int receiver_destroy (void **receiver_)
{
    return zlink_receiver_destroy (receiver_);
}
inline int receiver_bind (void *receiver_, const char *endpoint_)
{
    return zlink_receiver_bind (receiver_, endpoint_);
}
inline int receiver_connect_registry (void *receiver_, const char *endpoint_)
{
    return zlink_receiver_connect_registry (receiver_, endpoint_);
}
inline int receiver_register (void *receiver_,
                              const char *service_,
                              const char *advertise_,
                              uint32_t weight_)
{
    return zlink_receiver_register (receiver_, service_, advertise_, weight_);
}
inline int receiver_setsockopt (void *receiver_,
                                int role_,
                                int option_,
                                const void *value_,
                                size_t len_)
{
    (void) role_;
    const int mapped = detail::receiver_option_from_socket_option (option_);
    if (mapped < 0)
        return -1;

    return zlink_receiver_set_option (receiver_, mapped, value_, len_);
}
inline void *receiver_router_socket_unsafe (void *receiver_)
{
    (void) receiver_;
    errno = ENOTSUP;
    return NULL;
}

inline void *gateway_new (void *ctx_, void *discovery_, const char *routing_id_)
{
    void *gw = zlink_gateway_new (ctx_, NULL, routing_id_, NULL, NULL);
    if (gw && discovery_) {
        if (zlink_gateway_attach_discovery (gw, discovery_) != 0) {
            zlink_gateway_destroy (&gw);
            return NULL;
        }
    }
    return gw;
}
inline int gateway_destroy (void **gateway_) { return zlink_gateway_destroy (gateway_); }
inline int gateway_send (void *gateway_,
                         const char *service_,
                         zlink_msg_t *parts_,
                         size_t count_,
                         int flags_)
{
    return zlink_gateway_send (gateway_, service_, parts_, count_, flags_);
}
inline int gateway_send_bytes (void *gateway_,
                               const char *service_,
                               const void *data_,
                               size_t size_,
                               int flags_)
{
    return zlink_gateway_send_bytes (gateway_, service_, data_, size_, flags_);
}
inline int gateway_send_rid_bytes (void *gateway_,
                                   const char *service_,
                                   const zlink_routing_id_t *routing_id_,
                                   const void *data_,
                                   size_t size_,
                                   int flags_)
{
    return zlink_gateway_send_rid_bytes (
      gateway_,
      service_,
      const_cast<zlink_routing_id_t *> (routing_id_),
      data_,
      size_,
      flags_);
}
inline int gateway_setsockopt (void *gateway_,
                               int option_,
                               const void *value_,
                               size_t len_)
{
    const int mapped = detail::gateway_option_from_socket_option (option_);
    if (mapped < 0)
        return -1;

    return zlink_gateway_set_option (gateway_, mapped, value_, len_);
}
inline int gateway_connection_count (void *gateway_, const char *service_)
{
    return zlink_gateway_connection_count (gateway_, service_);
}
inline void *gateway_router_socket_unsafe (void *gateway_)
{
    (void) gateway_;
    errno = ENOTSUP;
    return NULL;
}
inline void *gateway_router_socket (void *gateway_)
{
    (void) gateway_;
    errno = ENOTSUP;
    return NULL;
}

inline void *spot_node_new (void *ctx_) { return zlink_spot_node_new (ctx_, NULL, NULL, NULL); }
inline int spot_node_destroy (void **node_) { return zlink_spot_node_destroy (node_); }
inline int spot_node_bind (void *node_, const char *endpoint_)
{
    return zlink_spot_node_bind (node_, endpoint_);
}
inline int spot_node_register (void *node_,
                               const char *service_,
                               const char *advertise_)
{
    return zlink_spot_node_register (node_, service_, advertise_);
}
inline int spot_node_unregister (void *node_, const char *service_)
{
    return zlink_spot_node_unregister (node_, service_);
}
inline int spot_node_set_discovery (void *node_,
                                    void *discovery_,
                                    const char *service_)
{
    return zlink_spot_node_set_discovery (node_, discovery_, service_);
}
inline int spot_node_setsockopt (void *node_,
                                 int role_,
                                 int option_,
                                 const void *value_,
                                 size_t len_)
{
    switch (role_) {
    case 0:
        errno = EINVAL;
        return -1;
    case 1: {
        const int mapped = detail::spot_pub_option_from_socket_option (option_);
        if (mapped < 0)
            return -1;
        return zlink_spot_node_set_pub_option (node_, mapped, value_, len_);
    }
    case 2: {
        const int mapped = detail::spot_sub_option_from_socket_option (option_);
        if (mapped < 0)
            return -1;
        return zlink_spot_node_set_sub_option (node_, mapped, value_, len_);
    }
    default:
        errno = EINVAL;
        return -1;
    }
}
inline void *spot_node_pub_socket_unsafe (void *node_)
{
    return zlink_spot_node_default_pub (node_);
}
inline void *spot_node_pub_socket (void *node_)
{
    return zlink_spot_node_default_pub (node_);
}
inline void *spot_node_sub_socket_unsafe (void *node_)
{
    return zlink_spot_node_default_sub (node_);
}
inline void *spot_node_sub_socket (void *node_)
{
    return zlink_spot_node_default_sub (node_);
}
inline int spot_node_pub_peers (void *node_,
                                zlink_peer_info_t *peers_,
                                size_t *count_)
{
    void *pub = zlink_spot_node_default_pub (node_);
    if (!pub)
        return -1;

    return zlink_spot_pub_peers (pub, peers_, count_);
}
inline int spot_node_sub_peers (void *node_,
                                zlink_peer_info_t *peers_,
                                size_t *count_)
{
    void *sub = zlink_spot_node_default_sub (node_);
    if (!sub)
        return -1;

    return zlink_spot_sub_peers (sub, peers_, count_);
}

inline void *spot_pub_new (void *node_) { return zlink_spot_pub_new (node_); }
inline int spot_pub_destroy (void **spot_pub_)
{
    return zlink_spot_pub_destroy (spot_pub_);
}
inline int spot_pub_publish_bytes (void *spot_pub_,
                                   const char *topic_,
                                   const void *data_,
                                   size_t size_,
                                   int flags_)
{
    return zlink_spot_pub_publish_bytes (
      spot_pub_, topic_, data_, size_, flags_);
}

inline void *spot_sub_new (void *node_) { return zlink_spot_sub_new (node_); }
inline int spot_sub_destroy (void **spot_sub_)
{
    return zlink_spot_sub_destroy (spot_sub_);
}
inline int spot_sub_subscribe (void *spot_sub_, const char *topic_)
{
    return zlink_spot_sub_subscribe (spot_sub_, topic_);
}
inline int spot_sub_recv (void *spot_sub_,
                          zlink_msg_t **parts_,
                          size_t *count_,
                          int flags_,
                          char *topic_,
                          size_t *topic_len_)
{
    return zlink_spot_sub_recv (
      spot_sub_, parts_, count_, flags_, topic_, topic_len_);
}

inline int stream_attach_raw (void *socket_, zlink_stream_on_raw_fn on_raw_)
{
    return zlink_stream_attach_raw (socket_, on_raw_, NULL);
}
inline int stream_attach_len32be (void *socket_,
                                  zlink_stream_on_packets_fn on_packets_)
{
    return zlink_stream_attach_len32be (socket_, on_packets_);
}
inline int stream_detach (void *socket_) { return zlink_stream_detach (socket_); }
inline int stream_send (void *socket_,
                        const zlink_routing_id_t *routing_id_,
                        const void *data_,
                        size_t size_,
                        int flags_)
{
    return zlink_stream_send (
      socket_,
      const_cast<zlink_routing_id_t *> (routing_id_),
      data_,
      size_,
      flags_);
}

} // namespace compat
} // namespace zlink

#endif
