/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"

#include "api/recv_result_internal.hpp"
#include "api/socket_message_api_internal.hpp"
#include "core/recv_internal.hpp"
#include "sockets/socket_base.hpp"

namespace zlink
{
namespace
{
template <typename T, typename SocketSelector>
static bool wait_for_service_socket_event_local (const T &items_,
                                                 short events_,
                                                 zlink_recv_flags_t flags_,
                                                 size_t *ready_index_out_,
                                                 SocketSelector socket_selector_)
{
    if (ready_index_out_)
        *ready_index_out_ = 0;
    if (items_.empty ()) {
        errno = (flags_ & ZLINK_DONTWAIT) != 0 ? EAGAIN : ENOTCONN;
        return false;
    }

    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    for (size_t attempt = 0; attempt < items_.size (); ++attempt) {
        const int timeout_ms =
          dontwait || attempt + 1 < items_.size () ? 0 : 25;
        if (zlink::wait_socket_events_internal (
              socket_selector_ (items_[attempt]), events_, timeout_ms)
            > 0) {
            if (ready_index_out_)
                *ready_index_out_ = attempt;
            return true;
        }
    }
    errno = EAGAIN;
    return false;
}

}

int spot_node_t::service_subscribe_recv (zlink_routing_id_t *source_rid_out_,
                                         zlink_msg_t **parts_out_,
                                         size_t *part_count_out_,
                                         char *service_name_out_,
                                         size_t *service_name_len_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_)
{
    if (!parts_out_ || !part_count_out_ || !topic_id_len_out_
        || !service_name_len_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<const service_attachment_state_t::service_sub_cache_t>
          subs;
        {
            scoped_lock_t lock (_sync);
            subs = _service_attachment_state.sub_cache;
        }
        size_t ready_index = 0;
        if (!wait_for_service_socket_event_local (
              *subs, ZLINK_POLLIN, flags_, &ready_index,
              [] (const service_attachment_state_t::service_sub_cache_entry_t
                    &entry_) -> socket_base_t * { return entry_.socket; })) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }

        size_t service_len = *service_name_len_out_;
        zlink_recv_result_t rc =
          recv_result_internal::from_rc (zlink_socket_subscribe_recv_internal (
            (*subs)[ready_index].socket, source_rid_out_, parts_out_,
            part_count_out_, topic_id_out_, topic_id_len_out_,
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
        if (!service_name_out_) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            return 0;
        }
        if (service_len < (*subs)[ready_index].service_name.size ()) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            errno = EMSGSIZE;
            return -1;
        }
        if (!(*subs)[ready_index].service_name.empty ())
            memcpy (service_name_out_,
                    (*subs)[ready_index].service_name.data (),
                    (*subs)[ready_index].service_name.size ());
        *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
        return 0;
    }
}

int spot_node_t::service_subscription_event_recv (
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    if (!subscribed_out_ || !topic_id_len_out_ || !service_name_len_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<const service_attachment_state_t::service_sub_cache_t>
          subs;
        {
            scoped_lock_t lock (_sync);
            subs = _service_attachment_state.sub_cache;
        }
        size_t ready_index = 0;
        if (!wait_for_service_socket_event_local (
              *subs, ZLINK_POLLIN, flags_, &ready_index,
              [] (const service_attachment_state_t::service_sub_cache_entry_t
                    &entry_) -> socket_base_t * { return entry_.socket; })) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }

        size_t service_len = *service_name_len_out_;
        zlink_recv_result_t rc =
          recv_result_internal::from_rc (zlink_socket_xpub_recv_internal (
            (*subs)[ready_index].socket, source_rid_out_, subscribed_out_,
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
        if (!service_name_out_) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            return 0;
        }
        if (service_len < (*subs)[ready_index].service_name.size ()) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            errno = EMSGSIZE;
            return -1;
        }
        if (!(*subs)[ready_index].service_name.empty ())
            memcpy (service_name_out_,
                    (*subs)[ready_index].service_name.data (),
                    (*subs)[ready_index].service_name.size ());
        *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
        return 0;
    }
}

}
