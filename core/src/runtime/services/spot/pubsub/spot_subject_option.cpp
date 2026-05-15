/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_subject_access.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/pubsub/spot_sub.hpp"
#include "services/spot/pubsub/spot_subject_subscription_internal.hpp"

#include "api/service/service_handle_internal.hpp"

#include <algorithm>

namespace
{
void store_pending_pub_option (spot_handle_t *spot_,
                               int option_,
                               const void *optval_,
                               size_t optvallen_)
{
    if (!spot_)
        return;

    (void) zlink::store_spot_pub_default (&spot_->pending_pub_defaults, option_,
                                          optval_, optvallen_);
}

void store_pending_sub_option (spot_handle_t *spot_,
                               int option_,
                               const void *optval_,
                               size_t optvallen_)
{
    if (!spot_)
        return;

    (void) zlink::store_spot_sub_default (&spot_->pending_sub_defaults, option_,
                                          optval_, optvallen_);
}

int map_common_to_spot_pub_option (zlink_option_t option_)
{
    switch (option_) {
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

int map_common_to_spot_sub_option (zlink_option_t option_)
{
    switch (option_) {
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

int map_common_to_socket_option (zlink_option_t option_)
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
        case ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES:
            return ZLINK_INTERNAL_OPT_AUTO_HWM_MSG_UNIT_BYTES;
        default:
            return -1;
    }
}

int map_pub_to_socket_option (zlink_pub_option_t option_)
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

int map_sub_to_socket_option (zlink_sub_option_t option_)
{
    switch (option_) {
        case ZLINK_SUB_OPT_TOPICS_COUNT:
            return ZLINK_INTERNAL_OPT_TOPICS_COUNT;
        default:
            return -1;
    }
}

int copy_option_setting_value (
  const zlink::spot_node_t::option_setting_t &setting_,
  void *optval_,
  size_t *optvallen_)
{
    if (!setting_.enabled) {
        errno = EINVAL;
        return -1;
    }
    if (!optvallen_) {
        errno = EFAULT;
        return -1;
    }
    if (!optval_ || *optvallen_ < setting_.size) {
        *optvallen_ = setting_.size;
        errno = EINVAL;
        return -1;
    }

    memcpy (optval_, &setting_.value, setting_.size);
    *optvallen_ = setting_.size;
    return 0;
}
} // namespace

int spot_subject_set_common_option (void *handle_,
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

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_)) {
        if (pub_option < 0) {
            errno = EINVAL;
            return -1;
        }
        return pub->set_option (pub_option, optval_, optvallen_);
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub_option < 0) {
            errno = EINVAL;
            return -1;
        }
        return sub->set_option (sub_option, optval_, optvallen_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0) {
            store_pending_pub_option (spot, pub_option, optval_, optvallen_);
        }
        if (sub_option >= 0) {
            store_pending_sub_option (spot, sub_option, optval_, optvallen_);
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

int spot_subject_get_common_option (void *handle_,
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

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_)) {
        if (pub_option < 0) {
            errno = EINVAL;
            return -1;
        }
        zlink::socket_base_t *socket = pub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        if (sub_option < 0) {
            errno = EINVAL;
            return -1;
        }
        zlink::socket_base_t *socket = sub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        errno = ENOTSUP;
        return -1;
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
        if (pub_option >= 0 && sub_option < 0) {
            errno = ENOTSUP;
            return -1;
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

int spot_subject_set_pub_option (void *handle_,
                                 zlink_pub_option_t option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    if (option_ != ZLINK_PUB_OPT_NODROP) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_))
        return pub->set_option (ZLINK_SPOT_PUB_OPT_NODROP, optval_, optvallen_);

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        store_pending_pub_option (spot, ZLINK_SPOT_PUB_OPT_NODROP, optval_,
                                  optvallen_);
        return 0;
    }

    if (is_registered_spot_node_handle (handle_))
        return static_cast<zlink::spot_node_t *> (handle_)->set_pub_option (
          ZLINK_SPOT_PUB_OPT_NODROP, optval_, optvallen_);

    errno = EFAULT;
    return -1;
}

int spot_subject_get_pub_option (void *handle_,
                                 zlink_pub_option_t option_,
                                 void *optval_,
                                 size_t *optvallen_)
{
    const int socket_option = map_pub_to_socket_option (option_);
    if (socket_option < 0) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (handle_)) {
        zlink::socket_base_t *socket = pub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        if (option_ == ZLINK_PUB_OPT_NODROP
            && spot->pending_pub_defaults.nodrop.enabled) {
            return copy_option_setting_value (spot->pending_pub_defaults.nodrop,
                                              optval_, optvallen_);
        }
        errno = ENOTSUP;
        return -1;
    }

    if (is_registered_spot_node_handle (handle_)) {
        errno = ENOTSUP;
        return -1;
    }

    errno = EFAULT;
    return -1;
}

int spot_subject_set_sub_option (void *handle_,
                                 zlink_sub_option_t option_,
                                 const void *optval_,
                                 size_t optvallen_)
{
    LIBZLINK_UNUSED (handle_);

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

int spot_subject_get_sub_option (void *handle_,
                                 zlink_sub_option_t option_,
                                 void *optval_,
                                 size_t *optvallen_)
{
    if (option_ == ZLINK_SUB_OPT_TOPICS_COUNT) {
        if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
            if (optvallen_)
                *optvallen_ = sizeof (int);
            errno = EINVAL;
            return -1;
        }
        std::vector<zlink::spot_sub_t::subject_descriptor_t> subjects;
        if (spot_append_subscription_subjects (handle_, &subjects) != 0)
            return -1;
        std::sort (subjects.begin (), subjects.end (),
                   [] (const zlink::spot_sub_t::subject_descriptor_t &lhs_,
                       const zlink::spot_sub_t::subject_descriptor_t &rhs_) {
                       if (lhs_.subject != rhs_.subject)
                           return lhs_.subject < rhs_.subject;
                       return lhs_.subject_kind < rhs_.subject_kind;
                   });
        subjects.erase (
          std::unique (subjects.begin (), subjects.end (),
                       [] (const zlink::spot_sub_t::subject_descriptor_t &lhs_,
                           const zlink::spot_sub_t::subject_descriptor_t &rhs_) {
                           return lhs_.subject == rhs_.subject
                                  && lhs_.subject_kind == rhs_.subject_kind;
                       }),
          subjects.end ());
        *static_cast<int *> (optval_) = static_cast<int> (subjects.size ());
        *optvallen_ = sizeof (int);
        return 0;
    }

    const int socket_option = map_sub_to_socket_option (option_);
    if (socket_option < 0) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (handle_)) {
        zlink::socket_base_t *socket = sub->poller_socket ();
        if (!socket) {
            errno = EFAULT;
            return -1;
        }
        return socket->getsockopt (socket_option, optval_, optvallen_);
    }

    if (is_registered_spot_handle (handle_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (handle_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        errno = ENOTSUP;
        return -1;
    }

    if (is_registered_spot_node_handle (handle_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return -1;
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
