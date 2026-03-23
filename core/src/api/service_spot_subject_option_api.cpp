/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

#include <string.h>

#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

namespace
{
static int validate_spot_sub_option (int option_,
                                     const void *optval_,
                                     size_t optvallen_)
{
    if (!optval_ || optvallen_ == 0 || optvallen_ > sizeof (int)) {
        errno = EINVAL;
        return -1;
    }
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
        case ZLINK_SPOT_SUB_OPT_LINGER:
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

static void copy_spot_option_setting (zlink::spot_node_t::option_setting_t *dst_,
                                      const void *optval_,
                                      size_t optvallen_)
{
    if (!dst_)
        return;
    dst_->enabled = true;
    dst_->value = 0;
    dst_->size = optvallen_;
    memcpy (&dst_->value, optval_, optvallen_);
}

static void store_spot_pending_sub_option (spot_handle_t *spot_,
                                           int option_,
                                           const void *optval_,
                                           size_t optvallen_)
{
    if (!spot_)
        return;
    switch (option_) {
        case ZLINK_SPOT_SUB_OPT_RCVHWM:
            copy_spot_option_setting (&spot_->pending_sub_defaults.rcvhwm,
                                      optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_LINGER:
            copy_spot_option_setting (&spot_->pending_sub_defaults.linger,
                                      optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_SNDBUF:
            copy_spot_option_setting (&spot_->pending_sub_defaults.sndbuf,
                                      optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_RCVBUF:
            copy_spot_option_setting (&spot_->pending_sub_defaults.rcvbuf,
                                      optval_, optvallen_);
            return;
        case ZLINK_SPOT_SUB_OPT_RCVTIMEO:
            copy_spot_option_setting (&spot_->pending_sub_defaults.rcvtimeo,
                                      optval_, optvallen_);
            return;
        default:
            return;
    }
}

static int spot_set_pub_option_internal (void *spot_,
                                         zlink_spot_pub_option_t option_,
                                         const void *optval_,
                                         size_t optvallen_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_))
        return pub->set_option (option_, optval_, optvallen_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    zlink::spot_pub_t *pub = ensure_spot_pub (spot);
    if (!pub) {
        errno = ENOTSUP;
        return -1;
    }
    return pub->set_option (option_, optval_, optvallen_);
}

static int spot_set_sub_option_internal (void *spot_,
                                         zlink_spot_sub_option_t option_,
                                         const void *optval_,
                                         size_t optvallen_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_))
        return sub->set_option (option_, optval_, optvallen_);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    if (spot->sub)
        return spot->sub->set_option (option_, optval_, optvallen_);
    if (validate_spot_sub_option (option_, optval_, optvallen_) != 0)
        return -1;
    store_spot_pending_sub_option (spot, option_, optval_, optvallen_);
    return 0;
}

static int spot_node_set_pub_option_internal (void *node_,
                                              zlink_spot_pub_option_t option_,
                                              const void *optval_,
                                              size_t optvallen_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_pub_option (option_, optval_, optvallen_);
}

static int spot_node_set_sub_option_internal (void *node_,
                                              zlink_spot_sub_option_t option_,
                                              const void *optval_,
                                              size_t optvallen_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    return node->set_sub_option (option_, optval_, optvallen_);
}

static int map_common_to_spot_pub_option (zlink_option_t option_)
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
        default:
            return -1;
    }
}

static int map_common_to_socket_option (zlink_option_t option_)
{
    switch (option_) {
        case ZLINK_OPT_SNDHWM:
            return ZLINK_INTERNAL_OPT_SNDHWM;
        case ZLINK_OPT_RCVHWM:
            return ZLINK_INTERNAL_OPT_RCVHWM;
        case ZLINK_OPT_LINGER:
            return ZLINK_INTERNAL_OPT_LINGER;
        case ZLINK_OPT_SNDTIMEO:
            return ZLINK_INTERNAL_OPT_SNDTIMEO;
        case ZLINK_OPT_RCVTIMEO:
            return ZLINK_INTERNAL_OPT_RCVTIMEO;
        case ZLINK_OPT_SNDBUF:
            return ZLINK_INTERNAL_OPT_SNDBUF;
        case ZLINK_OPT_RCVBUF:
            return ZLINK_INTERNAL_OPT_RCVBUF;
        default:
            return -1;
    }
}

static int map_pub_to_socket_option (zlink_pub_option_t option_)
{
    switch (option_) {
        case ZLINK_PUB_OPT_NODROP:
            return ZLINK_INTERNAL_OPT_XPUB_NODROP;
        case ZLINK_PUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        default:
            return -1;
    }
}

static int map_common_to_spot_sub_option (zlink_option_t option_)
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
            return -1;
    }
}
}

int zlink_spot_subject_set_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   const void *optval_,
                                                   size_t optvallen_)
{
    const int pub_option = map_common_to_spot_pub_option (option_);
    const int sub_option = map_common_to_spot_sub_option (option_);
    if (pub_option < 0 && sub_option < 0) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = ensure_spot_pub (spot);
            if (!pub)
                return -1;
            if (pub->set_option (pub_option, optval_, optvallen_) != 0)
                return -1;
        }
        if (sub_option >= 0) {
            zlink::spot_sub_t *sub = ensure_spot_sub (spot);
            if (!sub)
                return -1;
            if (sub->set_option (sub_option, optval_, optvallen_) != 0)
                return -1;
        }
        return 0;
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0
            && node->set_pub_option (pub_option, optval_, optvallen_) != 0)
            return -1;
        if (sub_option >= 0
            && node->set_sub_option (sub_option, optval_, optvallen_) != 0)
            return -1;
        return 0;
    }

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_get_common_option_internal (void *handle_,
                                                   zlink_option_t option_,
                                                   void *optval_,
                                                   size_t *optvallen_)
{
    const int socket_option = map_common_to_socket_option (option_);
    const int pub_option = map_common_to_spot_pub_option (option_);
    const int sub_option = map_common_to_spot_sub_option (option_);
    if (socket_option < 0 || (pub_option < 0 && sub_option < 0)) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = ensure_spot_pub (spot);
            if (!pub || !pub->poller_socket ()) {
                errno = EFAULT;
                return -1;
            }
            return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                      optvallen_);
        }
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            zlink::spot_pub_t *pub = node->ensure_default_pub ();
            if (!pub || !pub->poller_socket ()) {
                errno = EFAULT;
                return -1;
            }
            return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                      optvallen_);
        }
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        if (!sub || !sub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return sub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    switch (option_) {
        case ZLINK_PUB_OPT_NODROP:
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (is_registered_spot_handle (handle_))
        return spot_set_pub_option_internal (handle_, ZLINK_SPOT_PUB_OPT_NODROP,
                                             optval_, optvallen_);
    if (is_registered_spot_node_handle (handle_))
        return spot_node_set_pub_option_internal (
          handle_, ZLINK_SPOT_PUB_OPT_NODROP, optval_, optvallen_);

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_get_pub_option_internal (void *handle_,
                                                zlink_pub_option_t option_,
                                                void *optval_,
                                                size_t *optvallen_)
{
    const int socket_option = map_pub_to_socket_option (option_);
    if (socket_option < 0) {
        errno = EINVAL;
        return -1;
    }
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub || !pub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        if (!pub || !pub->poller_socket ()) {
            errno = EFAULT;
            return -1;
        }
        return pub->poller_socket ()->getsockopt (socket_option, optval_,
                                                  optvallen_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_sub_option_internal (void *handle_,
                                                zlink_sub_option_t option_,
                                                const void *optval_,
                                                size_t optvallen_)
{
    if (option_ != ZLINK_SUB_OPT_TOPICS_COUNT) {
        errno = EINVAL;
        return -1;
    }
    if (optval_ || optvallen_ != 0) {
        errno = EINVAL;
        return -1;
    }
    errno = EINVAL;
    return -1;
}

int zlink_spot_subject_set_routing_id_internal (void *handle_,
                                                const void *data_,
                                                size_t size_)
{
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        return pub ? pub->set_routing_id (data_, size_) : -1;
    }
    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        return pub ? pub->set_routing_id (data_, size_) : -1;
    }
    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_get_routing_id_internal (void *handle_,
                                                zlink_routing_id_t *out_)
{
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        return pub ? pub->routing_id (out_) : -1;
    }
    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        return pub ? pub->routing_id (out_) : -1;
    }
    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_tls_server_internal (void *handle_,
                                                const char *cert_,
                                                const char *key_,
                                                int require_client_cert_)
{
    LIBZLINK_UNUSED (require_client_cert_);
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        if (!spot->node) {
            errno = EFAULT;
            return -1;
        }
        return spot->node->set_tls_server (cert_, key_);
    }
    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_tls_server (
          cert_, key_);
    errno = EFAULT;
    return -1;
}

int zlink_spot_subject_set_tls_client_internal (void *handle_,
                                                const char *ca_cert_,
                                                const char *hostname_,
                                                int trust_system_)
{
    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        if (!spot->node) {
            errno = EFAULT;
            return -1;
        }
        return spot->node->set_tls_client (ca_cert_, hostname_, trust_system_);
    }
    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_tls_client (
          ca_cert_, hostname_, trust_system_);
    errno = EFAULT;
    return -1;
}
