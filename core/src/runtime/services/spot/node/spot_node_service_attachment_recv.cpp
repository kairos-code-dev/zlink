/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/node/spot_node.hpp"

#include "api/message/recv_result_internal.hpp"
#include "api/socket/socket_message_api_internal.hpp"
#include "core/recv_internal.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
typedef spot_node_service_attachment_state_t service_attachment_state_t;

int spot_node_t::service_subscribe_recv (zlink_routing_id_t *source_rid_out_,
                                         zlink_msg_t **parts_out_,
                                         size_t *part_count_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_)
{
    if (!parts_out_ || !part_count_out_ || !topic_id_len_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<service_attachment_state_t::service_sub_recv_cache_t>
          sub_recv_cache;
        {
            scoped_lock_t lock (_sync);
            sub_recv_cache = _service_attachment_state.sub_recv_cache;
        }
        socket_base_t *ready_socket = NULL;
        if (!sub_recv_cache
            || !sub_recv_cache->wait_ready_socket (flags_, &ready_socket)) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }

        zlink_recv_result_t rc =
          recv_result_internal::from_rc (zlink_socket_subscribe_recv_internal (
            ready_socket, source_rid_out_, parts_out_, part_count_out_,
            topic_id_out_, topic_id_len_out_,
            static_cast<zlink_send_flags_t> (ZLINK_DONTWAIT)));
        if (rc == ZLINK_RECV_NO_DATA) {
            if ((flags_ & ZLINK_DONTWAIT) != 0) {
                errno = EAGAIN;
                return -1;
            }
            continue;
        }
        if (rc != ZLINK_RECV_OK)
            return -1;
        return 0;
    }
}

int spot_node_t::service_subscription_event_recv (
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    if (!subscribed_out_ || !topic_id_len_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<service_attachment_state_t::service_sub_recv_cache_t>
          sub_recv_cache;
        {
            scoped_lock_t lock (_sync);
            sub_recv_cache = _service_attachment_state.sub_recv_cache;
        }
        socket_base_t *ready_socket = NULL;
        if (!sub_recv_cache
            || !sub_recv_cache->wait_ready_socket (flags_, &ready_socket)) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }

        zlink_recv_result_t rc =
          recv_result_internal::from_rc (zlink_socket_xpub_recv_internal (
            ready_socket, source_rid_out_, subscribed_out_, topic_id_out_,
            topic_id_len_out_,
            static_cast<zlink_send_flags_t> (ZLINK_DONTWAIT)));
        if (rc == ZLINK_RECV_NO_DATA) {
            if ((flags_ & ZLINK_DONTWAIT) != 0) {
                errno = EAGAIN;
                return -1;
            }
            continue;
        }
        if (rc != ZLINK_RECV_OK)
            return -1;
        return 0;
    }
}

}
