/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include <zlink.h>

#include <memory>
#include <stddef.h>
#include <stdint.h>

namespace zlink
{
class socket_base_t;
namespace spot_reqrep_internal
{
struct router_spot_request_reply_state_t;
}

namespace spot_route_bridge_channel_reply_internal
{

uint64_t register_pending_reply (
  const std::shared_ptr<spot_reqrep_internal::router_spot_request_reply_state_t> &state_,
  socket_base_t *router_socket_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t channel_request_seq_,
  uint32_t timeout_ms_,
  const void *owner_);
void erase_pending_reply (
  const std::shared_ptr<spot_reqrep_internal::router_spot_request_reply_state_t> &state_,
  uint64_t request_seq_);
size_t pending_reply_count (
  const std::shared_ptr<spot_reqrep_internal::router_spot_request_reply_state_t> &state_);
void cancel_pending_replies (
  const std::shared_ptr<spot_reqrep_internal::router_spot_request_reply_state_t> &state_,
  const void *owner_);
void reply_to_channel_request (zlink_request_result_t result_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               void *userdata_);
//  Completes a channel request immediately with an error result, without a
//  registered pending reply. Used when the request can be judged on arrival
//  (e.g. the addressed spot does not live on this node).
void send_channel_request_error (socket_base_t *router_socket_,
                                 const zlink_routing_id_t *peer_rid_,
                                 uint64_t channel_request_seq_,
                                 zlink_request_result_t result_);

}
}
