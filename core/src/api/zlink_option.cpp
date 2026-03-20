/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "core/ctx.hpp"
#include "core/msg.hpp"
#include "api/legacy_api_internal.hpp"
#include "sockets/socket_base.hpp"
#include "services/discovery/discovery.hpp"
#include "services/discovery/registry.hpp"
#include "services/common/service_public_api.hpp"
#include "services/gateway/gateway.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"
#include "sockets/xsub.hpp"

#include <string.h>
#include <algorithm>
#include <string>

int zlink_spot_subject_set_common_option_internal (
  void *handle_,
  zlink_option_t option_,
  const void *optval_,
  size_t optvallen_);
int zlink_spot_subject_get_common_option_internal (
  void *handle_,
  zlink_option_t option_,
  void *optval_,
  size_t *optvallen_);
int zlink_spot_subject_set_pub_option_internal (
  void *handle_,
  zlink_pub_option_t option_,
  const void *optval_,
  size_t optvallen_);
int zlink_spot_subject_get_pub_option_internal (
  void *handle_,
  zlink_pub_option_t option_,
  void *optval_,
  size_t *optvallen_);
int zlink_spot_subject_set_sub_option_internal (
  void *handle_,
  zlink_sub_option_t option_,
  const void *optval_,
  size_t optvallen_);
int zlink_spot_subject_get_sub_option_internal (
  void *handle_,
  zlink_sub_option_t option_,
  void *optval_,
  size_t *optvallen_);
int zlink_spot_subject_set_routing_id_internal (void *handle_,
                                                const void *data_,
                                                size_t size_);
int zlink_spot_subject_get_routing_id_internal (void *handle_,
                                                zlink_routing_id_t *out_);
int zlink_spot_subject_set_tls_server_internal (void *handle_,
                                                const char *cert_,
                                                const char *key_,
                                                int require_client_cert_);
int zlink_spot_subject_set_tls_client_internal (void *handle_,
                                                const char *ca_cert_,
                                                const char *hostname_,
                                                int trust_system_);
int zlink_spot_subject_set_subscription_internal (void *handle_,
                                                  const char *filter_);
int zlink_spot_subject_unset_subscription_internal (void *handle_,
                                                    const char *filter_);
int zlink_spot_subject_subscription_at_internal (void *handle_,
                                                 size_t index_,
                                                 char *filter_out_,
                                                 size_t *filter_len_inout_,
                                                 int *is_pattern_out_);

namespace
{
static bool is_valid_pubsub_filter (const char *filter_,
                                    std::string *raw_filter_out_,
                                    bool *is_pattern_out_)
{
    if (!filter_ || filter_[0] == '\0')
        return false;

    const size_t len = strlen (filter_);
    if (len == 0 || len > 255)
        return false;

    const char *star = strchr (filter_, '*');
    if (!star) {
        if (raw_filter_out_)
            *raw_filter_out_ = std::string (filter_, len);
        if (is_pattern_out_)
            *is_pattern_out_ = false;
        return true;
    }

    if (star != filter_ + len - 1 || len < 2)
        return false;

    if (strchr (star + 1, '*'))
        return false;

    if (raw_filter_out_)
        *raw_filter_out_ = std::string (filter_, len - 1);
    if (is_pattern_out_)
        *is_pattern_out_ = true;
    return true;
}

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

static zlink::gateway_t *as_gateway (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (handle_);
    return gateway->check_tag () ? gateway : NULL;
}

static zlink::discovery_t *as_discovery (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::discovery_t *discovery = static_cast<zlink::discovery_t *> (handle_);
    return discovery->check_tag () ? discovery : NULL;
}

static zlink::registry_t *as_registry (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::registry_t *registry = static_cast<zlink::registry_t *> (handle_);
    return registry->check_tag () ? registry : NULL;
}

static zlink::spot_node_t *as_spot_node (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
    return node->check_tag () ? node : NULL;
}

static zlink::spot_pub_t *as_spot_pub (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (handle_);
    return pub->check_tag () ? pub : NULL;
}

static zlink::spot_sub_t *as_spot_sub (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (handle_);
    return sub->check_tag () ? sub : NULL;
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
    return type_ == ZLINK_PUB || type_ == ZLINK_XPUB;
}

static bool is_sub_socket_type (int type_)
{
    return type_ == ZLINK_SUB || type_ == ZLINK_XSUB;
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

static int map_spot_pub_option (int option_)
{
    switch (option_) {
        case ZLINK_OPT_SNDHWM:
            return ZLINK_SPOT_PUB_OPT_SNDHWM;
        case ZLINK_OPT_SNDTIMEO:
            return ZLINK_SPOT_PUB_OPT_SNDTIMEO;
        case ZLINK_OPT_LINGER:
            return ZLINK_SPOT_PUB_OPT_LINGER;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_SPOT_PUB_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_SPOT_PUB_OPT_RCVBUF;
        case ZLINK_PUB_OPT_NODROP:
            return ZLINK_SPOT_PUB_OPT_NODROP;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int map_spot_sub_option (int option_)
{
    switch (option_) {
        case ZLINK_OPT_RCVHWM:
            return ZLINK_SPOT_SUB_OPT_RCVHWM;
        case ZLINK_OPT_LINGER:
            return ZLINK_SPOT_SUB_OPT_LINGER;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_SPOT_SUB_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_SPOT_SUB_OPT_RCVBUF;
        case ZLINK_OPT_RCVTIMEO:
            return ZLINK_SPOT_SUB_OPT_RCVTIMEO;
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

struct spot_subject_less_t
{
    bool operator() (
      const zlink::spot_sub_t::subject_descriptor_t &lhs_,
      const zlink::spot_sub_t::subject_descriptor_t &rhs_) const
    {
        if (lhs_.subject != rhs_.subject)
            return lhs_.subject < rhs_.subject;
        return lhs_.subject_kind < rhs_.subject_kind;
    }
};

static int spot_sub_subscription_at (zlink::spot_sub_t *sub_,
                                     size_t index_,
                                     char *filter_out_,
                                     size_t *filter_len_inout_,
                                     int *is_pattern_out_)
{
    if (!sub_) {
        errno = EFAULT;
        return -1;
    }

    std::vector<zlink::spot_sub_t::subject_descriptor_t> subjects;
    sub_->append_all_subjects (&subjects);
    std::sort (subjects.begin (), subjects.end (), spot_subject_less_t ());
    if (index_ >= subjects.size ()) {
        errno = ENOENT;
        return -1;
    }

    subscription_snapshot_entry_t entry;
    entry.filter = subjects[index_].subject;
    entry.is_pattern =
      subjects[index_].subject_kind == ZLINK_SERVICE_EVENT_SUBJECT_PATTERN;
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

    const int legacy = map_common_option (option_);
    if (legacy < 0)
        return -1;
    if (option_ == ZLINK_OPT_LAST_ENDPOINT || option_ == ZLINK_OPT_FD
        || option_ == ZLINK_OPT_EVENTS || option_ == ZLINK_OPT_TYPE) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return socket->setsockopt (legacy, optval_, optvallen_);
    errno = 0;

    if (zlink::gateway_t *gateway = as_gateway (handle_))
        return gateway->set_socket_option (legacy, optval_, optvallen_);
    if (zlink::discovery_t *discovery = as_discovery (handle_))
        return discovery->set_option (legacy, optval_, optvallen_);
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->set_option (map_spot_pub_option (option_), optval_,
                                optvallen_);
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_))
        return sub->set_option (map_spot_sub_option (option_), optval_,
                                optvallen_);
    if (zlink_spot_subject_set_common_option_internal (handle_, option_, optval_,
                                                       optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
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

    const int legacy = map_common_option (option_);
    if (legacy < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return socket->getsockopt (legacy, optval_, optvallen_);
    errno = 0;

    if (zlink::gateway_t *gateway = as_gateway (handle_)) {
        if (option_ == ZLINK_OPT_LAST_ENDPOINT)
            return gateway->last_endpoint (static_cast<char *> (optval_),
                                           optvallen_);
        return gateway->get_socket_option (legacy, optval_, optvallen_);
    }
    if (as_discovery (handle_)) {
        errno = ENOTSUP;
        return -1;
    }
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_)) {
        if (map_spot_pub_option (option_) < 0)
            return -1;
        zlink::socket_base_t *socket = pub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (legacy, optval_, optvallen_);
    }
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_)) {
        if (map_spot_sub_option (option_) < 0)
            return -1;
        zlink::socket_base_t *socket = sub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (legacy, optval_, optvallen_);
    }
    if (zlink_spot_subject_get_common_option_internal (handle_, option_, optval_,
                                                       optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_set_routing_id (void *handle_, const void *data_, size_t size_)
{
    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_STREAM) {
            errno = EINVAL;
            return -1;
        }
        return socket->setsockopt (ZLINK_INTERNAL_OPT_ROUTING_ID, data_, size_);
    }
    errno = 0;

    if (zlink::gateway_t *gateway = as_gateway (handle_))
        return gateway->set_routing_id (data_, size_);
    if (zlink::discovery_t *discovery = as_discovery (handle_))
        return discovery->set_routing_id (data_, size_);
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->set_routing_id (data_, size_);
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_))
        return sub->set_routing_id (data_, size_);
    if (zlink_spot_subject_set_routing_id_internal (handle_, data_, size_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
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

    if (zlink::gateway_t *gateway = as_gateway (handle_))
        return gateway->routing_id (out_);
    if (zlink::discovery_t *discovery = as_discovery (handle_))
        return discovery->routing_id (out_);
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->routing_id (out_);
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_))
        return sub->routing_id (out_);
    if (zlink_spot_subject_get_routing_id_internal (handle_, out_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_set_tls_server (void *handle_,
                          const char *cert_,
                          const char *key_,
                          int require_client_cert_)
{
    if (zlink::gateway_t *gateway = as_gateway (handle_))
        return gateway->set_tls_server (cert_, key_);
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->node () ? pub->node ()->set_tls_server (cert_, key_) : -1;
    if (zlink_spot_subject_set_tls_server_internal (
          handle_, cert_, key_, require_client_cert_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;
    if (zlink::registry_t *registry = as_registry (handle_))
        return registry->set_tls_server (cert_, key_, require_client_cert_);

    errno = EFAULT;
    return -1;
}

int zlink_set_tls_client (void *handle_,
                          const char *ca_cert_,
                          const char *hostname_,
                          int trust_system_)
{
    if (zlink::gateway_t *gateway = as_gateway (handle_))
        return gateway->set_tls_client (ca_cert_, hostname_, trust_system_);
    if (zlink::discovery_t *discovery = as_discovery (handle_))
        return discovery->set_tls_client (ca_cert_, hostname_, trust_system_);
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->node ()
                 ? pub->node ()->set_tls_client (ca_cert_, hostname_,
                                                trust_system_)
                 : -1;
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_))
        return sub->node ()
                 ? sub->node ()->set_tls_client (ca_cert_, hostname_,
                                                trust_system_)
                 : -1;
    if (zlink_spot_subject_set_tls_client_internal (
          handle_, ca_cert_, hostname_, trust_system_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;
    if (zlink::registry_t *registry = as_registry (handle_))
        return registry->set_tls_client (ca_cert_, hostname_, trust_system_);

    errno = EFAULT;
    return -1;
}

int zlink_set_router_option (void *handle_,
                             zlink_router_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int legacy = map_router_option (option_);
    if (legacy < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_DEALER && option_ != ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return -1;
        }
        if (type != ZLINK_ROUTER && type != ZLINK_DEALER) {
            errno = EINVAL;
            return -1;
        }
        return socket->setsockopt (legacy, optval_, optvallen_);
    }
    errno = 0;

    if (zlink::gateway_t *gateway = as_gateway (handle_)) {
        if (option_ == ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return -1;
        }
        return gateway->set_socket_option (legacy, optval_, optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_get_router_option (void *handle_,
                             zlink_router_option_t option_,
                             void *optval_,
                             size_t *optvallen_)
{
    const int legacy = map_router_option (option_);
    if (legacy < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type == ZLINK_DEALER && option_ != ZLINK_ROUTER_OPT_PROBE) {
            errno = EINVAL;
            return -1;
        }
        if (type != ZLINK_ROUTER && type != ZLINK_DEALER) {
            errno = EINVAL;
            return -1;
        }
        return socket->getsockopt (legacy, optval_, optvallen_);
    }
    errno = 0;

    if (zlink::gateway_t *gateway = as_gateway (handle_)) {
        if (option_ != ZLINK_ROUTER_OPT_MANDATORY
            && option_ != ZLINK_ROUTER_OPT_HANDOVER) {
            errno = EINVAL;
            return -1;
        }
        return gateway->get_socket_option (legacy, optval_, optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_set_dealer_option (void *handle_,
                             zlink_dealer_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int legacy = map_dealer_option (option_);
    if (legacy < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return set_socket_option_checked (socket, socket_type_of (socket),
                                      ZLINK_DEALER, ZLINK_DEALER, legacy,
                                      optval_, optvallen_);
}

int zlink_set_stream_option (void *handle_,
                             zlink_stream_option_t option_,
                             const void *optval_,
                             size_t optvallen_)
{
    const int legacy = map_stream_option (option_);
    if (legacy < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return set_socket_option_checked (socket, socket_type_of (socket),
                                      ZLINK_STREAM, ZLINK_STREAM, legacy,
                                      optval_, optvallen_);
}

int zlink_get_stream_option (void *handle_,
                             zlink_stream_option_t option_,
                             void *optval_,
                             size_t *optvallen_)
{
    const int legacy = map_stream_option (option_);
    if (legacy < 0)
        return -1;

    zlink::socket_base_t *socket = as_socket (handle_);
    if (!socket)
        return -1;
    return get_socket_option_checked (socket, socket_type_of (socket),
                                      ZLINK_STREAM, ZLINK_STREAM, legacy,
                                      optval_, optvallen_);
}

int zlink_set_pub_option (void *handle_,
                          zlink_pub_option_t option_,
                          const void *optval_,
                          size_t optvallen_)
{
    const int legacy = map_pub_option (option_);
    if (legacy < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return set_socket_option_checked (socket, socket_type_of (socket),
                                          ZLINK_PUB, ZLINK_XPUB, legacy,
                                          optval_, optvallen_);
    errno = 0;

    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->set_option (map_spot_pub_option (option_), optval_,
                                optvallen_);
    if (zlink_spot_subject_set_pub_option_internal (handle_, option_, optval_,
                                                    optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_get_pub_option (void *handle_,
                          zlink_pub_option_t option_,
                          void *optval_,
                          size_t *optvallen_)
{
    const int legacy = map_pub_option (option_);
    if (legacy < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return get_socket_option_checked (socket, socket_type_of (socket),
                                          ZLINK_PUB, ZLINK_XPUB, legacy,
                                          optval_, optvallen_);
    errno = 0;

    if (zlink::spot_pub_t *pub = as_spot_pub (handle_)) {
        zlink::socket_base_t *socket = pub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (legacy, optval_, optvallen_);
    }
    if (zlink_spot_subject_get_pub_option_internal (handle_, option_, optval_,
                                                    optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_set_sub_option (void *handle_,
                          zlink_sub_option_t option_,
                          const void *optval_,
                          size_t optvallen_)
{
    const int legacy = map_sub_option (option_);
    if (legacy < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
    {
        const int type = socket_type_of (socket);
        if (type != ZLINK_SUB && type != ZLINK_XSUB) {
            errno = EINVAL;
            return -1;
        }
        return socket->setsockopt (legacy, optval_, optvallen_);
    }
    errno = 0;

    if (zlink::spot_sub_t *sub = as_spot_sub (handle_)) {
        errno = EINVAL;
        return -1;
    }
    if (zlink_spot_subject_set_sub_option_internal (handle_, option_, optval_,
                                                    optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;
    errno = EFAULT;
    return -1;
}

int zlink_get_sub_option (void *handle_,
                          zlink_sub_option_t option_,
                          void *optval_,
                          size_t *optvallen_)
{
    const int legacy = map_sub_option (option_);
    if (legacy < 0)
        return -1;

    if (zlink::socket_base_t *socket = as_socket (handle_))
        return get_socket_option_checked (socket, socket_type_of (socket),
                                          ZLINK_SUB, ZLINK_XSUB, legacy,
                                          optval_, optvallen_);
    errno = 0;

    if (zlink::spot_sub_t *sub = as_spot_sub (handle_)) {
        zlink::socket_base_t *socket = sub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (legacy, optval_, optvallen_);
    }
    if (zlink_spot_subject_get_sub_option_internal (handle_, option_, optval_,
                                                    optvallen_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_set_subscription (void *handle_, const char *filter_)
{
    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        if (!filter_) {
            errno = EINVAL;
            return -1;
        }
        const int type = socket_type_of (socket);
        if (type != ZLINK_SUB && type != ZLINK_XSUB) {
            errno = EINVAL;
            return -1;
        }
        const size_t filter_len = strlen (filter_);
        if (type == ZLINK_XSUB)
            return xsub_update_subscription (socket, true, filter_, filter_len);
        return socket->setsockopt (ZLINK_INTERNAL_OPT_SUBSCRIBE, filter_, filter_len);
    }
    errno = 0;

    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (
              sub->node ()->public_api_guard ());
            if (!admission.acquired ())
                return -1;
        }
        return is_pattern ? sub->subscribe_pattern (filter_)
                          : sub->subscribe (filter_);
    }

    if (zlink_spot_subject_set_subscription_internal (handle_, filter_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_unset_subscription (void *handle_, const char *filter_)
{
    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        if (!filter_) {
            errno = EINVAL;
            return -1;
        }
        const int type = socket_type_of (socket);
        if (type != ZLINK_SUB && type != ZLINK_XSUB) {
            errno = EINVAL;
            return -1;
        }
        const size_t filter_len = strlen (filter_);
        if (type == ZLINK_XSUB)
            return xsub_update_subscription (socket, false, filter_, filter_len);
        return socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, filter_, filter_len);
    }
    errno = 0;

    std::string raw_filter;
    bool is_pattern = false;
    if (!is_valid_pubsub_filter (filter_, &raw_filter, &is_pattern)
        || raw_filter.empty ()) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (
              sub->node ()->public_api_guard ());
            if (!admission.acquired ())
                return -1;
        }
        return sub->unsubscribe (filter_);
    }

    if (zlink_spot_subject_unset_subscription_internal (handle_, filter_) == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}

int zlink_subscription_at (void *handle_,
                           size_t index_,
                           char *filter_out_,
                           size_t *filter_len_inout_,
                           int *is_pattern_out_)
{
    if (zlink::socket_base_t *socket = as_socket (handle_)) {
        const int type = socket_type_of (socket);
        if (type != ZLINK_SUB && type != ZLINK_XSUB) {
            errno = EINVAL;
            return -1;
        }
        return raw_socket_subscription_at (socket, index_, filter_out_,
                                           filter_len_inout_, is_pattern_out_);
    }
    errno = 0;

    if (zlink::spot_sub_t *sub = as_spot_sub (handle_)) {
        if (sub->node ()) {
            zlink::service_public_api_scope_t admission (
              sub->node ()->public_api_guard ());
            if (!admission.acquired ())
                return -1;
        }
        return spot_sub_subscription_at (sub, index_, filter_out_,
                                         filter_len_inout_, is_pattern_out_);
    }

    if (zlink_spot_subject_subscription_at_internal (
          handle_, index_, filter_out_, filter_len_inout_, is_pattern_out_)
        == 0)
        return 0;
    if (errno != EFAULT)
        return -1;

    errno = EFAULT;
    return -1;
}
