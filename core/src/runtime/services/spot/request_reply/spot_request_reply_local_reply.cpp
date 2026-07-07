/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "runtime/services/spot/request_reply/spot_request_reply_local_dispatch_internal.hpp"

#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"

namespace
{
namespace routed_protocol = zlink::spot_routed_protocol;

using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::pending_reply_t;
using zlink::spot_reqrep_internal::pending_spot_key_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

int queue_decoded_spot_reply_completion (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  const pending_reply_t &pending_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts, rr_envelope_.payload_part_count,
          &callback_errno, &callback_parts, &callback_part_count)
        != 0) {
        (void) zlink::spot_reqrep_internal::queue_spot_reply_completion (
          state_, pending_.handler, pending_.userdata, EPROTO, NULL, 0);
        return -1;
    }

    return zlink::spot_reqrep_internal::queue_spot_reply_completion (
      state_, pending_.handler, pending_.userdata, callback_errno, callback_parts,
      callback_part_count);
}

int queue_decoded_router_reply_completion (
  const std::shared_ptr<router_spot_request_reply_state_t> &state_,
  const pending_reply_t &pending_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts, rr_envelope_.payload_part_count,
          &callback_errno, &callback_parts, &callback_part_count)
        != 0) {
        (void) zlink::spot_reqrep_internal::queue_router_reply_completion (
          state_, pending_.handler, pending_.userdata, EPROTO, NULL, 0);
        return -1;
    }

    return zlink::spot_reqrep_internal::queue_router_reply_completion (
      state_, pending_.handler, pending_.userdata, callback_errno, callback_parts,
      callback_part_count);
}

bool take_spot_pending_reply (const std::shared_ptr<spot_request_reply_state_t> &state_,
                              const pending_spot_key_t &key_,
                              pending_reply_t *pending_out_)
{
    if (!state_ || !pending_out_)
        return false;

    std::lock_guard<std::mutex> lock (state_->mutex);
    std::unordered_map<pending_spot_key_t, pending_reply_t,
                       zlink::spot_reqrep_internal::pending_spot_key_hash_t>::iterator it =
      state_->requests.pending_replies.find (key_);
    if (it == state_->requests.pending_replies.end ())
        return false;

    *pending_out_ = it->second;
    state_->requests.pending_sequences.erase (key_.request_seq);
    state_->requests.pending_replies.erase (it);
    return true;
}

bool take_router_pending_reply (const std::shared_ptr<router_spot_request_reply_state_t> &state_,
                                uint64_t request_seq_,
                                pending_reply_t *pending_out_)
{
    if (!state_ || !pending_out_)
        return false;

    std::lock_guard<std::mutex> lock (state_->mutex);
    std::unordered_map<uint64_t, pending_reply_t>::iterator it =
      state_->requests.pending_replies.find (request_seq_);
    if (it == state_->requests.pending_replies.end ())
        return false;

    *pending_out_ = it->second;
    state_->requests.pending_sequences.erase (request_seq_);
    state_->requests.pending_replies.erase (it);
    return true;
}
}

int zlink::spot_reqrep_internal::deliver_reply_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<spot_request_reply_state_t> state = find_spot_state_by_identity (
      spot_envelope_.destination_node_rid, spot_envelope_.destination_endpoint_rid);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_spot_key_t key;
    key.source_class = spot_envelope_.source_class;
    key.source_rid = spot_envelope_.source_class == routed_protocol::router_endpoint_class
                       ? spot_envelope_.source_endpoint_rid
                       : spot_envelope_.source_node_rid;
    key.source_spot_rid = spot_envelope_.source_class == routed_protocol::spot_endpoint_class
                            ? spot_envelope_.source_endpoint_rid
                            : std::string ();
    key.request_seq = rr_envelope_.request_seq;

    pending_reply_t pending;
    if (!take_spot_pending_reply (state, key, &pending))
        return 0;

    zlink::request_timeout::cancel (pending.timeout_task);
    return queue_decoded_spot_reply_completion (state, pending, rr_envelope_);
}

int zlink::spot_reqrep_internal::deliver_reply_to_router (
  const std::string &router_rid_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_reply_t pending;
    if (!take_router_pending_reply (state, rr_envelope_.request_seq, &pending))
        return 0;

    zlink::request_timeout::cancel (pending.timeout_task);
    return queue_decoded_router_reply_completion (state, pending, rr_envelope_);
}
