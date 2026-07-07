/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/spot/core/service_spot_route_bridge_channel_reply_internal.hpp"

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/socket/socket_request_reply_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "sockets/common/socket_base.hpp"

namespace zlink
{
namespace spot_route_bridge_channel_reply_internal
{

namespace
{
struct pending_reply_t
{
    socket_base_t *router_socket;
    zlink_routing_id_t peer_rid;
    uint64_t request_seq;
    const void *owner;
};
}

void *new_pending_reply (socket_base_t *router_socket_,
                         const zlink_routing_id_t *peer_rid_,
                         uint64_t request_seq_,
                         const void *owner_)
{
    if (!router_socket_ || !peer_rid_ || request_seq_ == 0 || !owner_) {
        errno = EINVAL;
        return NULL;
    }
    pending_reply_t *pending = new (std::nothrow) pending_reply_t ();
    if (!pending) {
        errno = ENOMEM;
        return NULL;
    }
    pending->router_socket = router_socket_;
    pending->peer_rid = *peer_rid_;
    pending->request_seq = request_seq_;
    pending->owner = owner_;
    return pending;
}

void delete_pending_reply (void *pending_)
{
    delete static_cast<pending_reply_t *> (pending_);
}

uint64_t register_pending_reply (
  const std::shared_ptr<spot_reqrep_internal::router_spot_request_reply_state_t> &state_,
  socket_base_t *router_socket_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t channel_request_seq_,
  uint32_t timeout_ms_,
  const void *owner_)
{
    if (!state_) {
        errno = EFAULT;
        return 0;
    }

    uint64_t local_request_seq = 0;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        local_request_seq = zlink::request_reply_runtime::allocate_request_sequence (
          &state_->requests.next_request_seq, state_->requests.pending_sequences);
    }
    if (local_request_seq == 0)
        return 0;

    void *pending = new_pending_reply (router_socket_, peer_rid_, channel_request_seq_, owner_);
    if (!pending)
        return 0;

    zlink::spot_reqrep_internal::pending_spot_key_t ignored_key;
    ignored_key.source_class = 0;
    ignored_key.request_seq = local_request_seq;
    if (zlink::spot_reqrep_internal::register_router_spot_pending_request (
          state_, local_request_seq, ignored_key, timeout_ms_, &reply_to_channel_request, pending)
        != 0) {
        delete_pending_reply (pending);
        return 0;
    }
    return local_request_seq;
}

void erase_pending_reply (
  const std::shared_ptr<spot_reqrep_internal::router_spot_request_reply_state_t> &state_,
  uint64_t request_seq_)
{
    if (!state_ || request_seq_ == 0)
        return;

    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->requests.pending_sequences.erase (request_seq_);
    std::unordered_map<uint64_t, zlink::spot_reqrep_internal::pending_reply_t>::iterator it =
      state_->requests.pending_replies.find (request_seq_);
    if (it == state_->requests.pending_replies.end ())
        return;
    zlink::request_timeout::cancel (it->second.timeout_task);
    delete_pending_reply (it->second.userdata);
    state_->requests.pending_replies.erase (it);
}

size_t pending_reply_count (
  const std::shared_ptr<spot_reqrep_internal::router_spot_request_reply_state_t> &state_)
{
    if (!state_)
        return 0;
    std::lock_guard<std::mutex> lock (state_->mutex);
    return state_->requests.pending_replies.size ();
}

void cancel_pending_replies (
  const std::shared_ptr<spot_reqrep_internal::router_spot_request_reply_state_t> &state_,
  const void *owner_)
{
    if (!state_ || !owner_)
        return;

    std::vector<pending_reply_t *> pending;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        for (std::unordered_map<uint64_t, zlink::spot_reqrep_internal::pending_reply_t>::iterator
               it = state_->requests.pending_replies.begin ();
             it != state_->requests.pending_replies.end ();) {
            pending_reply_t *bridge_pending = static_cast<pending_reply_t *> (it->second.userdata);
            if (!bridge_pending || bridge_pending->owner != owner_) {
                ++it;
                continue;
            }
            zlink::request_timeout::cancel (it->second.timeout_task);
            pending.push_back (bridge_pending);
            state_->requests.pending_sequences.erase (it->first);
            it = state_->requests.pending_replies.erase (it);
        }
    }

    for (size_t i = 0; i < pending.size (); ++i)
        delete_pending_reply (pending[i]);
}

void send_channel_request_error (socket_base_t *router_socket_,
                                 const zlink_routing_id_t *peer_rid_,
                                 uint64_t channel_request_seq_,
                                 zlink_request_result_t result_)
{
    if (!router_socket_ || !peer_rid_ || channel_request_seq_ == 0)
        return;
    zlink_msg_t errno_part;
    zlink_msg_init (&errno_part);
    if (zlink::request_reply::init_error_reply_result_part (&errno_part, result_) != 0)
        return;
    (void) socket_reqrep_internal::send_request_reply_message (
      router_socket_, peer_rid_, &errno_part, 1, ZLINK_DONTWAIT,
      request_reply::error_reply_type, channel_request_seq_);
}

void reply_to_channel_request (zlink_request_result_t result_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               void *userdata_)
{
    pending_reply_t *pending = static_cast<pending_reply_t *> (userdata_);
    if (!pending) {
        zlink_multipart_close (parts_, part_count_);
        return;
    }

    if (result_ != ZLINK_REQUEST_OK) {
        zlink_multipart_close (parts_, part_count_);
        zlink_msg_t errno_part;
        zlink_msg_init (&errno_part);
        if (zlink::request_reply::init_error_reply_result_part (&errno_part, result_) == 0) {
            (void) socket_reqrep_internal::send_request_reply_message (
              pending->router_socket, &pending->peer_rid, &errno_part, 1, ZLINK_DONTWAIT,
              request_reply::error_reply_type, pending->request_seq);
        }
        delete_pending_reply (pending);
        return;
    }

    if (!parts_ || part_count_ == 0) {
        zlink_msg_t empty;
        zlink_msg_init (&empty);
        (void) zlink_msg_init_size (&empty, 0);
        (void) zlink_router_reply_part (pending->router_socket, &pending->peer_rid,
                                        pending->request_seq, &empty, ZLINK_PART_FINAL);
        delete_pending_reply (pending);
        return;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        const zlink_part_flag_t part_flag =
          i + 1 < part_count_ ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        if (zlink_router_reply_part (pending->router_socket, &pending->peer_rid,
                                     pending->request_seq, &parts_[i], part_flag)
            != ZLINK_SUBMIT_OK) {
            zlink_multipart_close (parts_ + i, part_count_ - i);
            break;
        }
    }
    delete_pending_reply (pending);
}

}
}
