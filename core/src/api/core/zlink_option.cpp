/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/core/config_result_internal.hpp"
#include "api/service/service_option_surface_internal.hpp"
#include "api/core/zlink_option_internal.hpp"
#include "utils/err.hpp"

#include "core/ctx.hpp"

#include <stddef.h>
#include <string.h>

namespace
{
struct option_descriptor_t
{
    int public_option;
    int internal_option;
    bool service_only;
};

template <size_t N>
const option_descriptor_t *lookup_option_descriptor (
  const option_descriptor_t (&table_)[N], int option_)
{
    for (size_t i = 0; i < N; ++i) {
        if (table_[i].public_option == option_)
            return &table_[i];
    }

    errno = EINVAL;
    return NULL;
}

const option_descriptor_t common_option_table[] = {
  {ZLINK_OPT_AFFINITY, ZLINK_INTERNAL_OPT_AFFINITY, false},
  {ZLINK_OPT_RATE, ZLINK_INTERNAL_OPT_RATE, false},
  {ZLINK_OPT_RECOVERY_IVL, ZLINK_INTERNAL_OPT_RECOVERY_IVL, false},
  {ZLINK_OPT_SNDBUF, ZLINK_INTERNAL_OPT_SNDBUF, false},
  {ZLINK_OPT_RCVBUF, ZLINK_INTERNAL_OPT_RCVBUF, false},
  {ZLINK_OPT_FD, ZLINK_INTERNAL_OPT_FD, false},
  {ZLINK_OPT_EVENTS, ZLINK_INTERNAL_OPT_EVENTS, false},
  {ZLINK_OPT_TYPE, ZLINK_INTERNAL_OPT_TYPE, false},
  {ZLINK_OPT_LINGER, ZLINK_INTERNAL_OPT_LINGER, false},
  {ZLINK_OPT_RECONNECT_IVL, ZLINK_INTERNAL_OPT_RECONNECT_IVL, false},
  {ZLINK_OPT_BACKLOG, ZLINK_INTERNAL_OPT_BACKLOG, false},
  {ZLINK_OPT_RECONNECT_IVL_MAX, ZLINK_INTERNAL_OPT_RECONNECT_IVL_MAX, false},
  {ZLINK_OPT_MAXMSGSIZE, ZLINK_INTERNAL_OPT_MAXMSGSIZE, false},
  {ZLINK_OPT_SNDHWM, ZLINK_INTERNAL_OPT_SNDHWM, false},
  {ZLINK_OPT_RCVHWM, ZLINK_INTERNAL_OPT_RCVHWM, false},
  {ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES,
   ZLINK_INTERNAL_OPT_AUTO_HWM_MSG_UNIT_BYTES, false},
  {ZLINK_OPT_MULTICAST_HOPS, ZLINK_INTERNAL_OPT_MULTICAST_HOPS, false},
  {ZLINK_OPT_RCVTIMEO, ZLINK_INTERNAL_OPT_RCVTIMEO, false},
  {ZLINK_OPT_SNDTIMEO, ZLINK_INTERNAL_OPT_SNDTIMEO, false},
  {ZLINK_OPT_LAST_ENDPOINT, ZLINK_INTERNAL_OPT_LAST_ENDPOINT, false},
  {ZLINK_OPT_TCP_KEEPALIVE, ZLINK_INTERNAL_OPT_TCP_KEEPALIVE, false},
  {ZLINK_OPT_TCP_KEEPALIVE_CNT, ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_CNT, false},
  {ZLINK_OPT_TCP_KEEPALIVE_IDLE, ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_IDLE, false},
  {ZLINK_OPT_TCP_KEEPALIVE_INTVL, ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_INTVL, false},
  {ZLINK_OPT_IMMEDIATE, ZLINK_INTERNAL_OPT_IMMEDIATE, false},
  {ZLINK_OPT_IPV6, ZLINK_INTERNAL_OPT_IPV6, false},
  {ZLINK_OPT_CONFLATE, ZLINK_INTERNAL_OPT_CONFLATE, false},
  {ZLINK_OPT_TOS, ZLINK_INTERNAL_OPT_TOS, false},
  {ZLINK_OPT_HANDSHAKE_IVL, ZLINK_INTERNAL_OPT_HANDSHAKE_IVL, false},
  {ZLINK_OPT_BLOCKY, ZLINK_INTERNAL_OPT_BLOCKY, false},
  {ZLINK_OPT_INVERT_MATCHING, ZLINK_INTERNAL_OPT_INVERT_MATCHING, false},
  {ZLINK_OPT_HEARTBEAT_IVL, ZLINK_INTERNAL_OPT_HEARTBEAT_IVL, false},
  {ZLINK_OPT_HEARTBEAT_TTL, ZLINK_INTERNAL_OPT_HEARTBEAT_TTL, false},
  {ZLINK_OPT_HEARTBEAT_TIMEOUT, ZLINK_INTERNAL_OPT_HEARTBEAT_TIMEOUT, false},
  {ZLINK_OPT_CONNECT_TIMEOUT, ZLINK_INTERNAL_OPT_CONNECT_TIMEOUT, false},
  {ZLINK_OPT_TCP_MAXRT, ZLINK_INTERNAL_OPT_TCP_MAXRT, false},
  {ZLINK_OPT_MULTICAST_MAXTPDU, ZLINK_INTERNAL_OPT_MULTICAST_MAXTPDU, false},
  {ZLINK_OPT_BINDTODEVICE, ZLINK_INTERNAL_OPT_BINDTODEVICE, false},
  {ZLINK_OPT_TLS_CERT, ZLINK_INTERNAL_OPT_TLS_CERT, false},
  {ZLINK_OPT_TLS_KEY, ZLINK_INTERNAL_OPT_TLS_KEY, false},
  {ZLINK_OPT_TLS_CA, ZLINK_INTERNAL_OPT_TLS_CA, false},
  {ZLINK_OPT_TLS_VERIFY, ZLINK_INTERNAL_OPT_TLS_VERIFY, false},
  {ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT,
   ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT, false},
  {ZLINK_OPT_TLS_HOSTNAME, ZLINK_INTERNAL_OPT_TLS_HOSTNAME, false},
  {ZLINK_OPT_TLS_TRUST_SYSTEM, ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM, false},
  {ZLINK_OPT_TLS_PASSWORD, ZLINK_INTERNAL_OPT_TLS_PASSWORD, false},
  {ZLINK_OPT_ZMP_METADATA, ZLINK_INTERNAL_OPT_ZMP_METADATA, false},
  {ZLINK_OPT_TCP_NODELAY, ZLINK_INTERNAL_OPT_TCP_NODELAY, false},
  {ZLINK_OPT_RID_DUPLICATE_POLICY, ZLINK_INTERNAL_OPT_RID_DUPLICATE_POLICY,
   false},
  {ZLINK_OPT_ROUTE_VALUE_MAX_SIZE, ZLINK_OPT_ROUTE_VALUE_MAX_SIZE, true},
  {ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC, ZLINK_OPT_DISCOVERY_SPOT_OWNER_SYNC,
   true},
  {ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC, ZLINK_OPT_DISCOVERY_ACTOR_ROUTE_SYNC,
   true},
};

const option_descriptor_t router_option_table[] = {
  {ZLINK_ROUTER_OPT_MANDATORY, ZLINK_INTERNAL_OPT_ROUTER_MANDATORY, false},
  {ZLINK_ROUTER_OPT_PROBE, ZLINK_INTERNAL_OPT_PROBE_ROUTER, false},
  {ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
   ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID, false},
  {ZLINK_ROUTER_OPT_WEIGHT, ZLINK_INTERNAL_OPT_PEER_WEIGHT, false},
};

const option_descriptor_t dealer_option_table[] = {
  {ZLINK_DEALER_OPT_PROBE, ZLINK_INTERNAL_OPT_PROBE_ROUTER, false},
  {ZLINK_DEALER_OPT_WEIGHT, ZLINK_INTERNAL_OPT_PEER_WEIGHT, false},
};

const option_descriptor_t stream_option_table[] = {
  {ZLINK_STREAM_OPT_NOTIFY, ZLINK_INTERNAL_OPT_STREAM_NOTIFY, false},
};

const option_descriptor_t pub_option_table[] = {
  {ZLINK_PUB_OPT_VERBOSE, ZLINK_INTERNAL_OPT_XPUB_VERBOSE, false},
  {ZLINK_PUB_OPT_VERBOSER, ZLINK_INTERNAL_OPT_XPUB_VERBOSER, false},
  {ZLINK_PUB_OPT_MANUAL, ZLINK_INTERNAL_OPT_XPUB_MANUAL, false},
  {ZLINK_PUB_OPT_MANUAL_LAST_VALUE,
   ZLINK_INTERNAL_OPT_XPUB_MANUAL_LAST_VALUE, false},
  {ZLINK_PUB_OPT_NODROP, ZLINK_INTERNAL_OPT_XPUB_NODROP, false},
  {ZLINK_PUB_OPT_WELCOME_MSG, ZLINK_INTERNAL_OPT_XPUB_WELCOME_MSG, false},
  {ZLINK_PUB_OPT_TOPICS_COUNT, ZLINK_INTERNAL_OPT_TOPICS_COUNT, false},
  {ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, ZLINK_INTERNAL_OPT_SUBSCRIBE, false},
  {ZLINK_PUB_OPT_REJECT_SUBSCRIBE, ZLINK_INTERNAL_OPT_UNSUBSCRIBE, false},
};

const option_descriptor_t sub_option_table[] = {
  {ZLINK_SUB_OPT_TOPICS_COUNT, ZLINK_INTERNAL_OPT_TOPICS_COUNT, false},
};
}

zlink::socket_base_t *as_socket (void *handle_)
{
    if (!handle_) {
        errno = EFAULT;
        return NULL;
    }
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (handle_);
    if (!socket->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return socket;
}

int socket_type_of (zlink::socket_base_t *socket_)
{
    int type = 0;
    size_t size = sizeof (type);
    if (!socket_
        || socket_->getsockopt (ZLINK_INTERNAL_OPT_TYPE, &type, &size) != 0) {
        return -1;
    }
    return type;
}

int map_common_option (zlink_option_t option_)
{
    const option_descriptor_t *descriptor =
      lookup_option_descriptor (common_option_table, option_);
    return descriptor ? descriptor->internal_option : -1;
}

int map_router_option (zlink_router_option_t option_)
{
    const option_descriptor_t *descriptor =
      lookup_option_descriptor (router_option_table, option_);
    return descriptor ? descriptor->internal_option : -1;
}

int map_dealer_option (zlink_dealer_option_t option_)
{
    const option_descriptor_t *descriptor =
      lookup_option_descriptor (dealer_option_table, option_);
    return descriptor ? descriptor->internal_option : -1;
}

int map_stream_option (zlink_stream_option_t option_)
{
    const option_descriptor_t *descriptor =
      lookup_option_descriptor (stream_option_table, option_);
    return descriptor ? descriptor->internal_option : -1;
}

int map_pub_option (int option_)
{
    const option_descriptor_t *descriptor =
      lookup_option_descriptor (pub_option_table, option_);
    return descriptor ? descriptor->internal_option : -1;
}

int map_sub_option (int option_)
{
    const option_descriptor_t *descriptor =
      lookup_option_descriptor (sub_option_table, option_);
    return descriptor ? descriptor->internal_option : -1;
}

const option_descriptor_t *lookup_common_option (zlink_option_t option_)
{
    return lookup_option_descriptor (common_option_table, option_);
}

int set_socket_option_checked (zlink::socket_base_t *socket_,
                               int type_,
                               int expected_a_,
                               int expected_b_,
                               int option_,
                               const void *optval_,
                               size_t optvallen_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }
    if (type_ != expected_a_ && type_ != expected_b_) {
        errno = EINVAL;
        return -1;
    }
    return socket_->setsockopt (option_, optval_, optvallen_);
}

int get_socket_option_checked (zlink::socket_base_t *socket_,
                               int type_,
                               int expected_a_,
                               int expected_b_,
                               int option_,
                               void *optval_,
                               size_t *optvallen_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }
    if (type_ != expected_a_ && type_ != expected_b_) {
        errno = EINVAL;
        return -1;
    }
    return socket_->getsockopt (option_, optval_, optvallen_);
}

zlink_config_result_t zlink_set_option (void *handle_,
                                       zlink_option_t option_,
                                       const void *optval_,
                                       size_t optvallen_)
{
    if (!handle_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    const option_descriptor_t *descriptor = lookup_common_option (option_);
    if (!descriptor)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    const int socket_option = descriptor->internal_option;
    if (option_ == ZLINK_OPT_LAST_ENDPOINT || option_ == ZLINK_OPT_FD
        || option_ == ZLINK_OPT_EVENTS || option_ == ZLINK_OPT_TYPE) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    errno = 0;
    if (zlink_service_set_common_option (handle_, option_, socket_option, optval_,
                                         optvallen_)
        == 0) {
        return ZLINK_CONFIG_OK;
    }
    if (errno != EFAULT)
        return zlink::config_result_internal::from_rc (-1);

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        if (descriptor->service_only) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          socket->setsockopt (socket_option, optval_, optvallen_));
    }
    errno = EFAULT;
    return ZLINK_CONFIG_INVALID_HANDLE;
}

zlink_config_result_t zlink_get_option (void *handle_,
                                       zlink_option_t option_,
                                       void *optval_,
                                       size_t *optvallen_)
{
    if (!handle_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    const option_descriptor_t *descriptor = lookup_common_option (option_);
    if (!descriptor)
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    const int socket_option = descriptor->internal_option;

    errno = 0;
    if (zlink_service_get_common_option (handle_, option_, socket_option, optval_,
                                         optvallen_)
        == 0) {
        return ZLINK_CONFIG_OK;
    }
    if (errno != EFAULT)
        return zlink::config_result_internal::from_rc (-1);

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        if (descriptor->service_only) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          socket->getsockopt (socket_option, optval_, optvallen_));
    }
    errno = EFAULT;
    return ZLINK_CONFIG_INVALID_HANDLE;
}

zlink_config_result_t zlink_set_routing_id (void *handle_, const void *data_, size_t size_)
{
    errno = 0;
    if (zlink_service_set_routing_id (handle_, data_, size_) == 0)
        return ZLINK_CONFIG_OK;
    if (errno != EFAULT)
        return zlink::config_result_internal::from_rc (-1);

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_CORE_SOCKET_STREAM) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        return zlink::config_result_internal::from_rc (
          socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, data_, size_));
    }
    errno = EFAULT;
    return ZLINK_CONFIG_INVALID_HANDLE;
}

zlink_config_result_t zlink_get_routing_id (void *handle_, zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    errno = 0;
    if (zlink_service_get_routing_id (handle_, out_) == 0)
        return ZLINK_CONFIG_OK;
    if (errno != EFAULT)
        return zlink::config_result_internal::from_rc (-1);

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        size_t size = sizeof (out_->data);
        const int rc =
          socket->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, out_->data, &size);
        if (rc != 0)
            return zlink::config_result_internal::from_rc (rc);
        if (size > sizeof (out_->data)) {
            errno = EINVAL;
            return ZLINK_CONFIG_INVALID_ARGUMENT;
        }
        out_->size = static_cast<uint8_t> (size);
        return ZLINK_CONFIG_OK;
    }
    errno = EFAULT;
    return ZLINK_CONFIG_INVALID_HANDLE;
}

zlink_config_result_t zlink_socket_set_channel_name (void *socket_,
                                                     const char *channel_name_)
{
    zlink::socket_base_t *socket = as_socket (socket_);
    if (!socket)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (
      socket->set_channel_name_metadata (channel_name_));
}

zlink_config_result_t zlink_socket_get_channel_name (
  void *socket_,
  char *channel_name_buf_,
  size_t channel_name_capacity_,
  size_t *channel_name_len_out_)
{
    zlink::socket_base_t *socket = as_socket (socket_);
    if (!socket)
        return ZLINK_CONFIG_INVALID_HANDLE;
    return zlink::config_result_internal::from_rc (
      socket->get_channel_name_metadata (channel_name_buf_,
                                         channel_name_capacity_,
                                         channel_name_len_out_));
}

zlink_config_result_t zlink_set_tls_server (void *handle_,
                                           const char *cert_,
                                           const char *key_,
                                           int require_client_cert_)
{
    errno = 0;
    if (zlink_service_set_tls_server (handle_, cert_, key_,
                                      require_client_cert_)
        == 0)
        return ZLINK_CONFIG_OK;
    if (errno != EFAULT)
        return zlink::config_result_internal::from_rc (-1);

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        if (!cert_ || !key_) {
            errno = EFAULT;
            return ZLINK_CONFIG_INVALID_HANDLE;
        }

        if (socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_CERT, cert_,
                                strlen (cert_))
              != 0
            || socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_KEY, key_,
                                   strlen (key_))
                 != 0
            || socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT,
                                   &require_client_cert_,
                                   sizeof (require_client_cert_))
                 != 0) {
            return zlink::config_result_internal::from_rc (-1);
        }

        return ZLINK_CONFIG_OK;
    }
    errno = EFAULT;
    return ZLINK_CONFIG_INVALID_HANDLE;
}

zlink_config_result_t zlink_set_tls_client (void *handle_,
                                            const char *ca_cert_,
                                            const char *hostname_,
                                            int trust_system_)
{
    errno = 0;
    if (zlink_service_set_tls_client (handle_, ca_cert_, hostname_,
                                      trust_system_)
        == 0)
        return ZLINK_CONFIG_OK;
    if (errno != EFAULT)
        return zlink::config_result_internal::from_rc (-1);

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        if (!ca_cert_ || !hostname_) {
            errno = EFAULT;
            return ZLINK_CONFIG_INVALID_HANDLE;
        }

        if (socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_CA, ca_cert_,
                                strlen (ca_cert_))
              != 0
            || socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_HOSTNAME, hostname_,
                                   strlen (hostname_))
                 != 0
            || socket->setsockopt (ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM,
                                   &trust_system_,
                                   sizeof (trust_system_))
                 != 0) {
            return zlink::config_result_internal::from_rc (-1);
        }

        return ZLINK_CONFIG_OK;
    }
    errno = EFAULT;
    return ZLINK_CONFIG_INVALID_HANDLE;
}
