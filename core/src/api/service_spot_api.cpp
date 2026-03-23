/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"
#include "api/monitor_api_internal.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

int spot_publish_internal (void *spot_,
                           const char *topic_id_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           zlink_send_flags_t flags_)
{
    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_))
        return pub->publish (topic_id_, parts_, part_count_, flags_);

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
    return pub->publish (topic_id_, parts_, part_count_, flags_);
}

int spot_pub_publish_internal (void *spot_pub_,
                               const char *topic_id_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return -1;
    }
    return pub->publish (topic_id_, parts_, part_count_, flags_);
}

int spot_node_publish_internal (void *node_,
                                const char *topic_id_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_)
{
    if (!node_)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_pub_t *pub = node->ensure_default_pub ();
    if (!pub)
        return -1;
    return pub->publish (topic_id_, parts_, part_count_, flags_);
}

int spot_pub_send_ready_handler_internal (void *spot_pub_,
                                          zlink_send_ready_handler_fn handler_,
                                          void *userdata_)
{
    zlink::spot_pub_t *pub = as_spot_pub_side_handle (spot_pub_);
    if (!pub) {
        errno = EFAULT;
        return -1;
    }
    if (pub->node ()) {
        zlink::service_public_api_scope_t admission (
          pub->node ()->public_api_guard ());
        if (!admission.acquired ())
            return -1;
    }
    void *subject = spot_pub_;
    if (pub->is_node_owned_default () && pub->node ())
        subject = pub->node ();
    return pub->set_send_ready_handler (handler_, subject, userdata_);
}

int spot_sub_subscribe_internal (void *spot_sub_, const char *topic_id_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return -1;
    }
    return sub->subscribe (topic_id_);
}

int spot_sub_subscribe_pattern_internal (void *spot_sub_,
                                         const char *pattern_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return -1;
    }
    return sub->subscribe_pattern (pattern_);
}

int spot_sub_unsubscribe_internal (void *spot_sub_,
                                   const char *topic_id_or_pattern_)
{
    zlink::spot_sub_t *sub = as_spot_sub_side_handle (spot_sub_);
    if (!sub) {
        errno = EFAULT;
        return -1;
    }
    return sub->unsubscribe (topic_id_or_pattern_);
}

int spot_sub_recv_internal (void *sub_,
                            zlink_routing_id_t *source_rid_out_,
                            zlink_msg_t **parts_,
                            size_t *part_count_,
                            char *topic_id_out_,
                            size_t *topic_id_len_,
                            zlink_send_flags_t flags_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (sub_)) {
        if (validate_recv_flags (flags_) != 0)
            return -1;
        return sub->recv (source_rid_out_, parts_, part_count_, flags_,
                          topic_id_out_, topic_id_len_);
    }

    spot_handle_t *spot = as_spot_handle (sub_);
    if (!spot)
        return -1;
    if (validate_recv_flags (flags_) != 0)
        return -1;
    zlink::service_public_api_scope_t admission (spot->public_api);
    if (!admission.acquired ())
        return -1;
    if (spot_require_recv_model (spot) != 0)
        return -1;
    zlink::spot_sub_t *sub = ensure_spot_sub (spot);
    if (!sub) {
        errno = ENOTSUP;
        return -1;
    }
    return sub->recv (source_rid_out_, parts_, part_count_, flags_, topic_id_out_,
                      topic_id_len_);
}

int spot_node_recv_internal (void *node_,
                             zlink_routing_id_t *source_rid_out_,
                             zlink_msg_t **parts_,
                             size_t *part_count_,
                             char *topic_id_out_,
                             size_t *topic_id_len_,
                             zlink_send_flags_t flags_)
{
    if (!node_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->check_tag ()) {
        errno = EFAULT;
        return -1;
    }
    zlink::service_public_api_scope_t admission (node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    if (spot_node_require_recv_model (node) != 0)
        return -1;
    zlink::spot_internal_receiver_t *receiver =
      zlink::spot_node_access_t::ensure_internal_receiver (node);
    if (!receiver || !receiver->impl ()) {
        errno = ENOTSUP;
        return -1;
    }
    return receiver->impl ()->recv (source_rid_out_, parts_, part_count_, flags_,
                                    topic_id_out_, topic_id_len_);
}

int zlink_service_recv_handler_internal (void *handle_,
                                         zlink_subscribe_handler_fn handler_,
                                         void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return -1;
    }

    if (is_registered_spot_handle (handle_)) {
        return spot_install_recv_handler (
          static_cast<spot_handle_t *> (handle_), handler_, userdata_);
    }

    if (is_registered_spot_node_handle (handle_)) {
        return spot_node_install_recv_handler (
          static_cast<zlink::spot_node_t *> (handle_), handler_, userdata_);
    }

    errno = EFAULT;
    return -1;
}
