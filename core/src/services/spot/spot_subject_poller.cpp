/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_subject_access.hpp"
#include "services/spot/spot_monitor_internal.hpp"

#include "api/service_handle_internal.hpp"
#include "api/service_mode_internal.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

zlink::socket_base_t *spot_pub_poller_socket (void *spot_pub_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return NULL;
    }
    return pub->poller_socket ();
}

zlink::socket_base_t *spot_sub_poller_socket (void *spot_sub_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return NULL;
    }
    return sub->poller_socket ();
}

zlink::socket_base_t *resolve_spot_pub_subject_poller_socket (
  void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        return pub ? pub->poller_socket () : NULL;
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        zlink::spot_pub_t *pub = node->ensure_default_pub ();
        return pub ? pub->poller_socket () : NULL;
    }
    errno = EFAULT;
    return NULL;
}

zlink::socket_base_t *resolve_spot_sub_subject_poller_socket (
  void *spot_or_node_)
{
    if (is_registered_spot_handle (spot_or_node_)) {
        spot_handle_t *spot = static_cast<spot_handle_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return NULL;
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        return sub ? sub->poller_socket () : NULL;
    }
    if (is_registered_spot_node_handle (spot_or_node_)) {
        zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (spot_or_node_);
        zlink::service_public_api_scope_t admission (node->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
        zlink::spot_internal_receiver_t *receiver =
          zlink::spot_node_access_t::ensure_internal_receiver (node);
        if (receiver && receiver->impl ())
            return receiver->impl ()->poller_socket ();
        zlink::spot_sub_t *sub = node->ensure_default_sub ();
        return sub ? sub->poller_socket () : NULL;
    }
    errno = EFAULT;
    return NULL;
}

int infer_spot_monitor_role (void *target_, uint32_t events_)
{
    const bool want_pub =
      (events_ & (zlink_spot_monitor_event_pub_queue_full
                  | zlink_spot_monitor_event_pub_queue_drained))
                           != 0;
    const bool want_sub =
      (events_ & zlink_spot_monitor_event_sub_filter_applied)
      != 0;
    if (want_pub && want_sub) {
        errno = EINVAL;
        return -1;
    }
    if (want_pub)
        return ZLINK_SPOT_ROLE_PUB;
    if (want_sub)
        return ZLINK_SPOT_ROLE_SUB;

    if (as_spot_pub_side_handle (target_))
        return ZLINK_SPOT_ROLE_PUB;
    if (as_spot_sub_side_handle (target_))
        return ZLINK_SPOT_ROLE_SUB;

    spot_handle_t *spot = as_spot_handle (target_);
    if (!spot) {
        errno = EFAULT;
        return -1;
    }
    if (spot->pub && !spot->sub)
        return ZLINK_SPOT_ROLE_PUB;
    if (spot->sub && !spot->pub)
        return ZLINK_SPOT_ROLE_SUB;

    return ZLINK_SPOT_ROLE_PUB;
}

void *spot_pub_monitor_open (void *spot_pub_, int events_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return NULL;
    }
    if (pub->node ()) {
        zlink::service_public_api_scope_t admission (
          pub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
    }
    return pub->monitor_open (events_);
}

void *spot_sub_monitor_open (void *spot_sub_, int events_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return NULL;
    }
    if (sub->node ()) {
        zlink::service_public_api_scope_t admission (
          sub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return NULL;
    }
    return sub->monitor_open (events_);
}

void *spot_handle_monitor_open (void *spot_,
                                zlink_spot_role_t role_,
                                int events_,
                                void **snapshot_subject_out_)
{
    if (snapshot_subject_out_)
        *snapshot_subject_out_ = NULL;

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return NULL;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return NULL;

    if (role_ == ZLINK_SPOT_ROLE_PUB) {
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub) {
            errno = ENOTSUP;
            return NULL;
        }
        if (snapshot_subject_out_)
            *snapshot_subject_out_ = pub;
        return pub->monitor_open (events_);
    }
    if (role_ == ZLINK_SPOT_ROLE_SUB) {
        zlink::spot_sub_t *sub = ensure_spot_sub (spot);
        if (!sub) {
            errno = ENOTSUP;
            return NULL;
        }
        if (snapshot_subject_out_)
            *snapshot_subject_out_ = sub;
        return sub->monitor_open (events_);
    }

    errno = EINVAL;
    return NULL;
}
