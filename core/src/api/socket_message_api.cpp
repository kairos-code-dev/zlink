/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_message_api_internal.hpp"
#include "core/recv_internal.hpp"

namespace
{
int recv_service_or_fault (void *handle_,
                           zlink_routing_id_t *source_rid_out_,
                           zlink_msg_t **parts_out_,
                           size_t *part_count_out_,
                           zlink_send_flags_t flags_)
{
    const int service_rc = zlink_service_recv_internal (
      handle_, source_rid_out_, parts_out_, part_count_out_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}

int recv_subscribe_service_or_fault (void *subject_,
                                     zlink_routing_id_t *source_rid_out_,
                                     zlink_msg_t **parts_out_,
                                     size_t *part_count_out_,
                                     char *topic_id_out_,
                                     size_t *topic_id_len_out_,
                                     zlink_send_flags_t flags_)
{
    const int service_rc = zlink_service_subscribe_recv_internal (
      subject_, source_rid_out_, parts_out_, part_count_out_, topic_id_out_,
      topic_id_len_out_, flags_);
    if (service_rc == 0 || errno != EFAULT)
        return service_rc;

    errno = EFAULT;
    return -1;
}

} // namespace

int zlink_xpub_recv (void *s_,
                     zlink_routing_id_t *source_rid_out_,
                     int *subscribed_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_,
                     zlink_send_flags_t flags_)
{
    return zlink_socket_xpub_recv_internal (s_, source_rid_out_,
                                            subscribed_out_, topic_id_out_,
                                            topic_id_len_, flags_);
}

int zlink_msg_recv (zlink_msg_t *msg_, void *s_, zlink_send_flags_t flags_)
{
    if (!s_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    zlink::socket_base_t *socket = try_as_socket (s_);
    if (!socket) {
        errno = ENOTSUP;
        return -1;
    }

    return zlink::recv_msg_socket (socket, socket->socket_type (), msg_, flags_);
}

int zlink_msg_recv_rid (zlink_msg_t *msg_,
                        void *s_,
                        zlink_routing_id_t *source_rid_out_,
                        zlink_send_flags_t flags_)
{
    if (!s_ || !msg_) {
        errno = EFAULT;
        return -1;
    }

    zlink::socket_base_t *socket = try_as_socket (s_);
    if (!socket) {
        errno = ENOTSUP;
        return -1;
    }

    return zlink::recv_msg_routed_socket (socket, msg_, source_rid_out_,
                                          flags_);
}

int zlink_recv (void *s_,
                zlink_routing_id_t *source_rid_out_,
                zlink_msg_t **parts_out_,
                size_t *part_count_out_,
                zlink_send_flags_t flags_)
{
    if (!s_) {
        errno = EFAULT;
        return -1;
    }
    const int socket_rc = zlink_socket_recv_internal (
      s_, source_rid_out_, parts_out_, part_count_out_, flags_);
    if (socket_rc == 0 || errno != EFAULT)
        return socket_rc;

    return recv_service_or_fault (s_, source_rid_out_, parts_out_,
                                  part_count_out_, flags_);
}

int zlink_subscribe (void *subject_,
                     zlink_routing_id_t *source_rid_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     char *topic_id_out_,
                     size_t *topic_id_len_out_,
                     zlink_send_flags_t flags_)
{
    if (!subject_) {
        errno = EFAULT;
        return -1;
    }

    const int socket_rc = zlink_socket_subscribe_recv_internal (
      subject_, source_rid_out_, parts_out_, part_count_out_, topic_id_out_,
      topic_id_len_out_, flags_);
    if (socket_rc == 0 || errno != EFAULT)
        return socket_rc;

    return recv_subscribe_service_or_fault (
      subject_, source_rid_out_, parts_out_, part_count_out_, topic_id_out_,
      topic_id_len_out_, flags_);
}

int zlink_subscription_event (void *subject_,
                              zlink_routing_id_t *source_rid_out_,
                              int *subscribed_out_,
                              char *topic_id_out_,
                              size_t *topic_id_len_out_,
                              zlink_send_flags_t flags_)
{
    return zlink_xpub_recv (subject_, source_rid_out_, subscribed_out_,
                            topic_id_out_, topic_id_len_out_, flags_);
}
