/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "sockets/socket_base.hpp"
#include "sockets/xsub.hpp"

#include <string.h>
#include <algorithm>
#include <string>

namespace
{
static zlink::socket_base_t *as_socket (void *handle_)
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

static int socket_type_of (zlink::socket_base_t *socket_)
{
    int type = 0;
    size_t size = sizeof (type);
    if (!socket_ || socket_->getsockopt (ZLINK_INTERNAL_OPT_TYPE, &type, &size) != 0)
        return -1;
    return type;
}

static bool is_pub_socket_type (int type_)
{
    return type_ == ZLINK_CORE_SOCKET_PUB || type_ == ZLINK_CORE_SOCKET_XPUB;
}

static bool is_sub_socket_type (int type_)
{
    return type_ == ZLINK_CORE_SOCKET_SUB || type_ == ZLINK_CORE_SOCKET_XSUB;
}

static int map_common_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_AFFINITY:
            return ZLINK_INTERNAL_OPT_AFFINITY;
        case ZLINK_OPT_RATE:
            return ZLINK_INTERNAL_OPT_RATE;
        case ZLINK_OPT_RECOVERY_IVL:
            return ZLINK_INTERNAL_OPT_RECOVERY_IVL;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_INTERNAL_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_INTERNAL_OPT_RCVBUF;
        case ZLINK_OPT_FD:
            return ZLINK_INTERNAL_OPT_FD;
        case ZLINK_OPT_EVENTS:
            return ZLINK_INTERNAL_OPT_EVENTS;
        case ZLINK_OPT_TYPE:
            return ZLINK_INTERNAL_OPT_TYPE;
        case ZLINK_OPT_LINGER:
            return ZLINK_INTERNAL_OPT_LINGER;
        case ZLINK_OPT_RECONNECT_IVL:
            return ZLINK_INTERNAL_OPT_RECONNECT_IVL;
        case ZLINK_OPT_BACKLOG:
            return ZLINK_INTERNAL_OPT_BACKLOG;
        case ZLINK_OPT_RECONNECT_IVL_MAX:
            return ZLINK_INTERNAL_OPT_RECONNECT_IVL_MAX;
        case ZLINK_OPT_MAXMSGSIZE:
            return ZLINK_INTERNAL_OPT_MAXMSGSIZE;
        case ZLINK_OPT_SNDHWM:
            return ZLINK_INTERNAL_OPT_SNDHWM;
        case ZLINK_OPT_RCVHWM:
            return ZLINK_INTERNAL_OPT_RCVHWM;
        case ZLINK_OPT_MULTICAST_HOPS:
            return ZLINK_INTERNAL_OPT_MULTICAST_HOPS;
        case ZLINK_OPT_RCVTIMEO:
            return ZLINK_INTERNAL_OPT_RCVTIMEO;
        case ZLINK_OPT_SNDTIMEO:
            return ZLINK_INTERNAL_OPT_SNDTIMEO;
        case ZLINK_OPT_LAST_ENDPOINT:
            return ZLINK_INTERNAL_OPT_LAST_ENDPOINT;
        case ZLINK_OPT_TCP_KEEPALIVE:
            return ZLINK_INTERNAL_OPT_TCP_KEEPALIVE;
        case ZLINK_OPT_TCP_KEEPALIVE_CNT:
            return ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_CNT;
        case ZLINK_OPT_TCP_KEEPALIVE_IDLE:
            return ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_IDLE;
        case ZLINK_OPT_TCP_KEEPALIVE_INTVL:
            return ZLINK_INTERNAL_OPT_TCP_KEEPALIVE_INTVL;
        case ZLINK_OPT_IMMEDIATE:
            return ZLINK_INTERNAL_OPT_IMMEDIATE;
        case ZLINK_OPT_IPV6:
            return ZLINK_INTERNAL_OPT_IPV6;
        case ZLINK_OPT_CONFLATE:
            return ZLINK_INTERNAL_OPT_CONFLATE;
        case ZLINK_OPT_TOS:
            return ZLINK_INTERNAL_OPT_TOS;
        case ZLINK_OPT_HANDSHAKE_IVL:
            return ZLINK_INTERNAL_OPT_HANDSHAKE_IVL;
        case ZLINK_OPT_BLOCKY:
            return ZLINK_INTERNAL_OPT_BLOCKY;
        case ZLINK_OPT_INVERT_MATCHING:
            return ZLINK_INTERNAL_OPT_INVERT_MATCHING;
        case ZLINK_OPT_HEARTBEAT_IVL:
            return ZLINK_INTERNAL_OPT_HEARTBEAT_IVL;
        case ZLINK_OPT_HEARTBEAT_TTL:
            return ZLINK_INTERNAL_OPT_HEARTBEAT_TTL;
        case ZLINK_OPT_HEARTBEAT_TIMEOUT:
            return ZLINK_INTERNAL_OPT_HEARTBEAT_TIMEOUT;
        case ZLINK_OPT_CONNECT_TIMEOUT:
            return ZLINK_INTERNAL_OPT_CONNECT_TIMEOUT;
        case ZLINK_OPT_TCP_MAXRT:
            return ZLINK_INTERNAL_OPT_TCP_MAXRT;
        case ZLINK_OPT_MULTICAST_MAXTPDU:
            return ZLINK_INTERNAL_OPT_MULTICAST_MAXTPDU;
        case ZLINK_OPT_BINDTODEVICE:
            return ZLINK_INTERNAL_OPT_BINDTODEVICE;
        case ZLINK_OPT_TLS_CERT:
            return ZLINK_INTERNAL_OPT_TLS_CERT;
        case ZLINK_OPT_TLS_KEY:
            return ZLINK_INTERNAL_OPT_TLS_KEY;
        case ZLINK_OPT_TLS_CA:
            return ZLINK_INTERNAL_OPT_TLS_CA;
        case ZLINK_OPT_TLS_VERIFY:
            return ZLINK_INTERNAL_OPT_TLS_VERIFY;
        case ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT:
            return ZLINK_INTERNAL_OPT_TLS_REQUIRE_CLIENT_CERT;
        case ZLINK_OPT_TLS_HOSTNAME:
            return ZLINK_INTERNAL_OPT_TLS_HOSTNAME;
        case ZLINK_OPT_TLS_TRUST_SYSTEM:
            return ZLINK_INTERNAL_OPT_TLS_TRUST_SYSTEM;
        case ZLINK_OPT_TLS_PASSWORD:
            return ZLINK_INTERNAL_OPT_TLS_PASSWORD;
        case ZLINK_OPT_ZMP_METADATA:
            return ZLINK_INTERNAL_OPT_ZMP_METADATA;
        case ZLINK_OPT_TCP_NODELAY:
            return ZLINK_INTERNAL_OPT_TCP_NODELAY;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int map_router_option (zlink_router_option_t option_)
{
    switch (option_) {
        case ZLINK_ROUTER_OPT_MANDATORY:
            return ZLINK_INTERNAL_OPT_ROUTER_MANDATORY;
        case ZLINK_ROUTER_OPT_HANDOVER:
            return ZLINK_INTERNAL_OPT_ROUTER_HANDOVER;
        case ZLINK_ROUTER_OPT_PROBE:
            return ZLINK_INTERNAL_OPT_PROBE_ROUTER;
        case ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID:
            return ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int map_dealer_option (zlink_dealer_option_t option_)
{
    switch (option_) {
        case ZLINK_DEALER_OPT_PROBE:
            return ZLINK_INTERNAL_OPT_PROBE_ROUTER;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int map_stream_option (zlink_stream_option_t option_)
{
    switch (option_) {
        case ZLINK_STREAM_OPT_NOTIFY:
            return ZLINK_INTERNAL_OPT_STREAM_NOTIFY;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int map_pub_option (int option_)
{
    switch (option_) {
        case ZLINK_PUB_OPT_VERBOSE:
            return ZLINK_INTERNAL_OPT_XPUB_VERBOSE;
        case ZLINK_PUB_OPT_VERBOSER:
            return ZLINK_INTERNAL_OPT_XPUB_VERBOSER;
        case ZLINK_PUB_OPT_MANUAL:
            return ZLINK_INTERNAL_OPT_XPUB_MANUAL;
        case ZLINK_PUB_OPT_MANUAL_LAST_VALUE:
            return ZLINK_INTERNAL_OPT_XPUB_MANUAL_LAST_VALUE;
        case ZLINK_PUB_OPT_NODROP:
            return ZLINK_INTERNAL_OPT_XPUB_NODROP;
        case ZLINK_PUB_OPT_WELCOME_MSG:
            return ZLINK_INTERNAL_OPT_XPUB_WELCOME_MSG;
        case ZLINK_PUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        case ZLINK_PUB_OPT_APPROVE_SUBSCRIBE:
            return ZLINK_INTERNAL_OPT_SUBSCRIBE;
        case ZLINK_PUB_OPT_REJECT_SUBSCRIBE:
            return ZLINK_INTERNAL_OPT_UNSUBSCRIBE;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int map_sub_option (int option_)
{
    switch (option_) {
        case ZLINK_SUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int set_socket_option_checked (zlink::socket_base_t *socket_,
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

static int get_socket_option_checked (zlink::socket_base_t *socket_,
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

static int xsub_update_subscription (zlink::socket_base_t *socket_,
                                     bool subscribe_,
                                     const void *optval_,
                                     size_t optvallen_)
{
    if (!socket_ || !optval_) {
        errno = EINVAL;
        return -1;
    }

    zlink::msg_t msg;
    if (msg.init_size (optvallen_ + 1) != 0)
        return -1;
    unsigned char *data = static_cast<unsigned char *> (msg.data ());
    data[0] = subscribe_ ? 1 : 0;
    if (optvallen_ > 0)
        memcpy (data + 1, optval_, optvallen_);
    const int rc = socket_->send (&msg, 0);
    if (rc != 0)
        msg.close ();
    return rc;
}

struct subscription_snapshot_entry_t
{
    subscription_snapshot_entry_t () : is_pattern (false) {}

    std::string filter;
    bool is_pattern;
};

static bool subscription_less (const subscription_snapshot_entry_t &lhs_,
                               const subscription_snapshot_entry_t &rhs_)
{
    if (lhs_.filter != rhs_.filter)
        return lhs_.filter < rhs_.filter;
    return lhs_.is_pattern < rhs_.is_pattern;
}

struct raw_subscription_less_t
{
    bool operator() (
      const zlink::xsub_t::subscription_descriptor_t &lhs_,
      const zlink::xsub_t::subscription_descriptor_t &rhs_) const
    {
        if (lhs_.filter != rhs_.filter)
            return lhs_.filter < rhs_.filter;
        return lhs_.is_pattern < rhs_.is_pattern;
    }
};

static int copy_subscription_entry (const subscription_snapshot_entry_t &entry_,
                                    char *filter_out_,
                                    size_t *filter_len_inout_,
                                    int *is_pattern_out_)
{
    if (!filter_len_inout_) {
        errno = EFAULT;
        return -1;
    }
    if (!filter_out_ && *filter_len_inout_ != 0) {
        errno = EFAULT;
        return -1;
    }
    if (*filter_len_inout_ < entry_.filter.size ()) {
        *filter_len_inout_ = entry_.filter.size ();
        errno = EINVAL;
        return -1;
    }
    if (filter_out_ && !entry_.filter.empty ())
        memcpy (filter_out_, entry_.filter.data (), entry_.filter.size ());
    *filter_len_inout_ = entry_.filter.size ();
    if (is_pattern_out_)
        *is_pattern_out_ = entry_.is_pattern ? 1 : 0;
    return 0;
}

static int raw_socket_subscription_at (zlink::socket_base_t *socket_,
                                       size_t index_,
                                       char *filter_out_,
                                       size_t *filter_len_inout_,
                                       int *is_pattern_out_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }

    std::vector<zlink::xsub_t::subscription_descriptor_t> entries;
    static_cast<zlink::xsub_t *> (socket_)->snapshot_subscriptions (&entries);
    std::sort (entries.begin (), entries.end (), raw_subscription_less_t ());

    if (index_ >= entries.size ()) {
        errno = ENOENT;
        return -1;
    }

    subscription_snapshot_entry_t entry;
    entry.filter = entries[index_].filter;
    entry.is_pattern = entries[index_].is_pattern;
    return copy_subscription_entry (entry, filter_out_, filter_len_inout_,
                                    is_pattern_out_);
}

}

int zlink_set_option (void *handle_,
                      zlink_option_t option_,
                      const void *optval_,
                      size_t optvallen_)
{
    if (!handle_) {
        errno = EFAULT;
        return -1;
    }

    const int socket_option = map_common_option (option_);
    if (socket_option < 0)
        return -1;
    if (option_ == ZLINK_OPT_LAST_ENDPOINT || option_ == ZLINK_OPT_FD
        || option_ == ZLINK_OPT_EVENTS || option_ == ZLINK_OPT_TYPE) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return socket->setsockopt (socket_option, optval_, optvallen_);
    errno = 0;

    return zlink_service_set_common_option (handle_, option_, socket_option,
                                            optval_, optvallen_);
}

int zlink_get_option (void *handle_,
                      zlink_option_t option_,
                      void *optval_,
                      size_t *optvallen_)
{
    if (!handle_) {
        errno = EFAULT;
        return -1;
    }

    const int socket_option = map_common_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return socket->getsockopt (socket_option, optval_, optvallen_);
    errno = 0;

    return zlink_service_get_common_option (handle_, option_, socket_option,
                                            optval_, optvallen_);
}

int zlink_set_routing_id (void *handle_, const void *data_, size_t size_)
{
    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_CORE_SOCKET_STREAM) {
            errno = EINVAL;
            return -1;
        }
        return socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, data_, size_);
    }
    errno = 0;

    return zlink_service_set_routing_id (handle_, data_, size_);
}

int zlink_get_routing_id (void *handle_, zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        size_t size = sizeof (out_->data);
        const int rc =
          socket->getsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, out_->data, &size);
        if (rc != 0)
            return rc;
        if (size > sizeof (out_->data)) {
            errno = EINVAL;
            return -1;
        }
        out_->size = static_cast<uint8_t> (size);
        return 0;
    }
    errno = 0;

    return zlink_service_get_routing_id (handle_, out_);
}

int zlink_set_tls_server (void *handle_,
                          const char *cert_,
                          const char *key_,
                          int require_client_cert_)
{
    return zlink_service_set_tls_server (handle_, cert_, key_,
                                         require_client_cert_);
}

int zlink_set_tls_client (void *handle_,
                          const char *ca_cert_,
                          const char *hostname_,
                          int trust_system_)
{
    return zlink_service_set_tls_client (handle_, ca_cert_, hostname_,
                                         trust_system_);
}

int zlink_set_router_option (void *handle_,
                             zlink_router_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int socket_option = map_router_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_CORE_SOCKET_DEALER && option_ != ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return -1;
        }
        if (type != ZLINK_CORE_SOCKET_ROUTER && type != ZLINK_CORE_SOCKET_DEALER) {
            errno = EINVAL;
            return -1;
        }
        return socket->setsockopt (socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_set_router_option (handle_, option_, socket_option,
                                            optval_, optvallen_);
}

int zlink_get_router_option (void *handle_,
                             zlink_router_option_t option_,
                             void *optval_,
                             size_t *optvallen_)
{
    const int socket_option = map_router_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_CORE_SOCKET_DEALER && option_ != ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return -1;
        }
        if (type != ZLINK_CORE_SOCKET_ROUTER && type != ZLINK_CORE_SOCKET_DEALER) {
            errno = EINVAL;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_get_router_option (handle_, option_, socket_option,
                                            optval_, optvallen_);
}

int zlink_set_dealer_option (void *handle_,
                             zlink_dealer_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int socket_option = map_dealer_option (option_);
    if (socket_option < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return set_socket_option_checked (socket, socket_type_of (socket),
                                      ZLINK_CORE_SOCKET_DEALER, ZLINK_CORE_SOCKET_DEALER, socket_option,
                                      optval_, optvallen_);
}

int zlink_set_stream_option (void *handle_,
                             zlink_stream_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int socket_option = map_stream_option (option_);
    if (socket_option < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return set_socket_option_checked (socket, socket_type_of (socket),
                                      ZLINK_CORE_SOCKET_STREAM, ZLINK_CORE_SOCKET_STREAM, socket_option,
                                      optval_, optvallen_);
}

int zlink_get_stream_option (void *handle_,
                             zlink_stream_option_t option_,
                             void *optval_,
                             size_t *optvallen_)
{
    const int socket_option = map_stream_option (option_);
    if (socket_option < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return get_socket_option_checked (socket, socket_type_of (socket),
                                      ZLINK_CORE_SOCKET_STREAM, ZLINK_CORE_SOCKET_STREAM, socket_option,
                                      optval_, optvallen_);
}

int zlink_set_pub_option (void *handle_,
                          zlink_pub_option_t option_,
                          const void *optval_,
                          size_t optvallen_)
{
    const int socket_option = map_pub_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return set_socket_option_checked (socket, socket_type_of (socket),
                                          ZLINK_CORE_SOCKET_PUB, ZLINK_CORE_SOCKET_XPUB, socket_option,
                                          optval_, optvallen_);
    errno = 0;

    return zlink_service_set_pub_option (handle_, option_, socket_option,
                                         optval_, optvallen_);
}

int zlink_get_pub_option (void *handle_,
                          zlink_pub_option_t option_,
                          void *optval_,
                          size_t *optvallen_)
{
    const int socket_option = map_pub_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return get_socket_option_checked (socket, socket_type_of (socket),
                                          ZLINK_CORE_SOCKET_PUB, ZLINK_CORE_SOCKET_XPUB, socket_option,
                                          optval_, optvallen_);
    errno = 0;

    return zlink_service_get_pub_option (handle_, option_, socket_option,
                                         optval_, optvallen_);
}

int zlink_set_sub_option (void *handle_,
                          zlink_sub_option_t option_,
                          const void *optval_,
                          size_t optvallen_)
{
    const int socket_option = map_sub_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
    {
        const int type = socket_type_of (socket);
        if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
            errno = EINVAL;
            return -1;
        }
        return socket->setsockopt (socket_option, optval_, optvallen_);
    }
    errno = 0;

    return zlink_service_set_sub_option (handle_, option_, socket_option,
                                         optval_, optvallen_);
}

int zlink_get_sub_option (void *handle_,
                          zlink_sub_option_t option_,
                          void *optval_,
                          size_t *optvallen_)
{
    const int socket_option = map_sub_option (option_);
    if (socket_option < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return get_socket_option_checked (socket, socket_type_of (socket),
                                          ZLINK_CORE_SOCKET_SUB, ZLINK_CORE_SOCKET_XSUB, socket_option,
                                          optval_, optvallen_);
    errno = 0;

    return zlink_service_get_sub_option (handle_, option_, socket_option,
                                         optval_, optvallen_);
}

int zlink_set_subscription (void *handle_, const char *filter_)
{
    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        if (!filter_) {
            errno = EINVAL;
            return -1;
        }
        const int type = socket_type_of (socket);
        if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
            errno = EINVAL;
            return -1;
        }
        const size_t filter_len = strlen (filter_);
        if (type == ZLINK_CORE_SOCKET_XSUB)
            return xsub_update_subscription (socket, true, filter_, filter_len);
        return socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, filter_, filter_len);
    }
    errno = 0;

    return zlink_service_set_subscription (handle_, filter_);
}

int zlink_unset_subscription (void *handle_, const char *filter_)
{
    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        if (!filter_) {
            errno = EINVAL;
            return -1;
        }
        const int type = socket_type_of (socket);
        if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
            errno = EINVAL;
            return -1;
        }
        const size_t filter_len = strlen (filter_);
        if (type == ZLINK_CORE_SOCKET_XSUB)
            return xsub_update_subscription (socket, false, filter_, filter_len);
        return socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, filter_, filter_len);
    }
    errno = 0;

    return zlink_service_unset_subscription (handle_, filter_);
}

int zlink_subscription_at (void *handle_,
                           size_t index_,
                           char *filter_out_,
                           size_t *filter_len_inout_,
                           int *is_pattern_out_)
{
    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type != ZLINK_CORE_SOCKET_SUB && type != ZLINK_CORE_SOCKET_XSUB) {
            errno = EINVAL;
            return -1;
        }
        return raw_socket_subscription_at (socket, index_, filter_out_,
                                           filter_len_inout_, is_pattern_out_);
    }
    errno = 0;

    return zlink_service_subscription_at (handle_, index_, filter_out_,
                                          filter_len_inout_, is_pattern_out_);
}
