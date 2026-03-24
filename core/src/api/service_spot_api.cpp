/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/service_api_internal.hpp"

int spot_publish_internal (void *spot_,
                           const char *topic_id_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           zlink_send_flags_t flags_)
{
    return spot_subject_publish (spot_, topic_id_, parts_, part_count_, flags_);
}

int spot_pub_publish_internal (void *spot_pub_,
                               const char *topic_id_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_)
{
    return spot_subject_publish (spot_pub_, topic_id_, parts_, part_count_,
                                 flags_);
}

int spot_node_publish_internal (void *node_,
                                const char *topic_id_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_)
{
    return spot_subject_publish (node_, topic_id_, parts_, part_count_, flags_);
}

int spot_pub_send_ready_handler_internal (void *spot_pub_,
                                          zlink_send_ready_handler_fn handler_,
                                          void *userdata_)
{
    return spot_pub_install_send_ready_handler (spot_pub_, handler_, userdata_);
}

int spot_sub_subscribe_internal (void *spot_sub_, const char *topic_id_)
{
    return spot_subject_set_subscription (spot_sub_, topic_id_);
}

int spot_sub_subscribe_pattern_internal (void *spot_sub_,
                                         const char *pattern_)
{
    return spot_subject_set_subscription (spot_sub_, pattern_);
}

int spot_sub_unsubscribe_internal (void *spot_sub_,
                                   const char *topic_id_or_pattern_)
{
    return spot_subject_unset_subscription (spot_sub_, topic_id_or_pattern_);
}

int spot_sub_recv_internal (void *sub_,
                            zlink_routing_id_t *source_rid_out_,
                            zlink_msg_t **parts_,
                            size_t *part_count_,
                            char *topic_id_out_,
                            size_t *topic_id_len_,
                            zlink_send_flags_t flags_)
{
    return spot_subject_recv (sub_, source_rid_out_, parts_, part_count_,
                              topic_id_out_, topic_id_len_, flags_);
}

int spot_node_recv_internal (void *node_,
                             zlink_routing_id_t *source_rid_out_,
                             zlink_msg_t **parts_,
                             size_t *part_count_,
                             char *topic_id_out_,
                             size_t *topic_id_len_,
                             zlink_send_flags_t flags_)
{
    return spot_subject_recv (node_, source_rid_out_, parts_, part_count_,
                              topic_id_out_, topic_id_len_, flags_);
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
