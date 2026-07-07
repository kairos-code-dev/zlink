/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "runtime/services/spot/request_reply/spot_request_reply_local_dispatch_internal.hpp"

#include "api/socket/socket_request_reply_internal.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;
}

int zlink::spot_reqrep_internal::queue_local_spot_message (
  spot_request_reply_state_t *state_,
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (zlink::spot_reqrep_internal::queue_spot_message (state_, source_rid_, spot_rid_,
                                                         request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }
    return 0;
}

int zlink::spot_reqrep_internal::queue_local_router_message (
  router_spot_request_reply_state_t *state_,
  const zlink_routing_id_t *source_node_rid_,
  const zlink_routing_id_t *source_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!state_ || !state_->owner) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        errno = EFAULT;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (state_->owner);
    if (!handle.socket) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> router_state =
      reqrep::find_or_create_request_reply_state (handle);
    if (reqrep::dispatch_router_message (router_state.get (), source_node_rid_, source_spot_rid_,
                                         request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }

    return 0;
}
