/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"
#include "sockets/socket_base.hpp"

#include <algorithm>
#include <string>
#include <string.h>

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

struct subscription_snapshot_entry_t
{
    subscription_snapshot_entry_t () : is_pattern (false) {}

    std::string filter;
    bool is_pattern;
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

int zlink_service_spot_set_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   int socket_option_,
                                                   const void *optval_,
                                                   size_t optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->set_option (map_spot_pub_option (option_), optval_,
                                optvallen_);
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_))
        return sub->set_option (map_spot_sub_option (option_), optval_,
                                optvallen_);
    return zlink_spot_subject_set_common_option_internal (handle_, option_,
                                                          optval_, optvallen_);
}

int zlink_service_spot_get_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   int socket_option_,
                                                   void *optval_,
                                                   size_t *optvallen_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_)) {
        if (map_spot_pub_option (option_) < 0)
            return -1;
        zlink::socket_base_t *socket = pub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option_, optval_, optvallen_);
    }
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_)) {
        if (map_spot_sub_option (option_) < 0)
            return -1;
        zlink::socket_base_t *socket = sub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option_, optval_, optvallen_);
    }
    return zlink_spot_subject_get_common_option_internal (handle_, option_,
                                                          optval_, optvallen_);
}

int zlink_service_spot_set_routing_id_internal (void *handle_,
                                                const void *data_,
                                                size_t size_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->set_routing_id (data_, size_);
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_))
        return sub->set_routing_id (data_, size_);
    return zlink_spot_subject_set_routing_id_internal (handle_, data_, size_);
}

int zlink_service_spot_get_routing_id_internal (void *handle_,
                                                zlink_routing_id_t *out_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->routing_id (out_);
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_))
        return sub->routing_id (out_);
    return zlink_spot_subject_get_routing_id_internal (handle_, out_);
}

int zlink_service_spot_set_tls_server_internal (void *handle_,
                                                const char *cert_,
                                                const char *key_,
                                                int require_client_cert_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->node () ? pub->node ()->set_tls_server (cert_, key_) : -1;
    return zlink_spot_subject_set_tls_server_internal (
      handle_, cert_, key_, require_client_cert_);
}

int zlink_service_spot_set_tls_client_internal (void *handle_,
                                                const char *ca_cert_,
                                                const char *hostname_,
                                                int trust_system_)
{
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
    return zlink_spot_subject_set_tls_client_internal (
      handle_, ca_cert_, hostname_, trust_system_);
}

int zlink_service_spot_set_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                int socket_option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_))
        return pub->set_option (map_spot_pub_option (option_), optval_,
                                optvallen_);
    return zlink_spot_subject_set_pub_option_internal (handle_, option_, optval_,
                                                       optvallen_);
}

int zlink_service_spot_get_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                int socket_option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    LIBZLINK_UNUSED (option_);
    if (zlink::spot_pub_t *pub = as_spot_pub (handle_)) {
        zlink::socket_base_t *socket = pub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option_, optval_, optvallen_);
    }
    return zlink_spot_subject_get_pub_option_internal (handle_, option_, optval_,
                                                       optvallen_);
}

int zlink_service_spot_set_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                int socket_option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    LIBZLINK_UNUSED (socket_option_);
    if (as_spot_sub (handle_)) {
        errno = EINVAL;
        return -1;
    }
    return zlink_spot_subject_set_sub_option_internal (handle_, option_, optval_,
                                                       optvallen_);
}

int zlink_service_spot_get_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                int socket_option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub (handle_)) {
        zlink::socket_base_t *socket = sub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option_, optval_, optvallen_);
    }
    return zlink_spot_subject_get_sub_option_internal (handle_, option_, optval_,
                                                       optvallen_);
}

int zlink_service_spot_set_subscription_internal (void *handle_,
                                                  const char *filter_)
{
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

    return zlink_spot_subject_set_subscription_internal (handle_, filter_);
}

int zlink_service_spot_unset_subscription_internal (void *handle_,
                                                    const char *filter_)
{
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

    return zlink_spot_subject_unset_subscription_internal (handle_, filter_);
}

int zlink_service_spot_subscription_at_internal (void *handle_,
                                                 size_t index_,
                                                 char *filter_out_,
                                                 size_t *filter_len_inout_,
                                                 int *is_pattern_out_)
{
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

    return zlink_spot_subject_subscription_at_internal (
      handle_, index_, filter_out_, filter_len_inout_, is_pattern_out_);
}
