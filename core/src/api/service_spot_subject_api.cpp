/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_api_internal.hpp"

int zlink_service_publish_internal (void *subject_,
                                    const char *topic_id_,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    zlink_send_flags_t flags_)
{
    return spot_subject_publish (subject_, topic_id_, parts_, part_count_,
                                 flags_);
}

int zlink_service_subscribe_recv_internal (void *subject_,
                                           zlink_routing_id_t *source_rid_out_,
                                           zlink_msg_t **parts_out_,
                                           size_t *part_count_out_,
                                           char *topic_id_out_,
                                           size_t *topic_id_len_out_,
                                           zlink_recv_flags_t flags_)
{
    return spot_subject_recv (subject_, source_rid_out_, parts_out_,
                              part_count_out_, topic_id_out_, topic_id_len_out_,
                              flags_);
}
