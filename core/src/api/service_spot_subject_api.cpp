/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_api_internal.hpp"

#include "services/spot/spot_pub.hpp"

int zlink_service_publish_internal (void *subject_,
                                    const char *topic_id_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    zlink_send_flags_t flags_)
{
    if (spot_handle_t *spot = as_spot_handle (subject_)) {
        if (!topic_id_) {
            errno = EINVAL;
            return -1;
        }
        return spot_publish_internal (spot, topic_id_, parts_, part_count_,
                                      flags_);
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (subject_)) {
        if (!topic_id_) {
            errno = EINVAL;
            return -1;
        }
        return spot_pub_publish_internal (pub, topic_id_, parts_,
                                          part_count_, flags_);
    }

    if (zlink::spot_node_t *node = as_spot_node_handle (subject_)) {
        if (!topic_id_) {
            errno = EINVAL;
            return -1;
        }
        return spot_node_publish_internal (node, topic_id_, parts_,
                                           part_count_, flags_);
    }

    errno = EFAULT;
    return -1;
}

int zlink_service_subscribe_recv_internal (void *subject_,
                                           zlink_routing_id_t *source_rid_out_,
                                           zlink_msg_t **parts_out_,
                                           size_t *part_count_out_,
                                           char *topic_id_out_,
                                           size_t *topic_id_len_out_,
                                           zlink_send_flags_t flags_)
{
    if (zlink::spot_sub_t *sub = as_spot_sub_side_handle (subject_))
        return spot_sub_recv_internal (sub, source_rid_out_, parts_out_,
                                       part_count_out_, topic_id_out_,
                                       topic_id_len_out_, flags_);

    if (spot_handle_t *spot = as_spot_handle (subject_))
        return spot_sub_recv_internal (spot, source_rid_out_, parts_out_,
                                       part_count_out_, topic_id_out_,
                                       topic_id_len_out_, flags_);

    if (zlink::spot_node_t *node = as_spot_node_handle (subject_))
        return spot_node_recv_internal (node, source_rid_out_, parts_out_,
                                        part_count_out_, topic_id_out_,
                                        topic_id_len_out_, flags_);

    errno = EFAULT;
    return -1;
}
