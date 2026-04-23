/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "services/spot/spot_data_plane_internal.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;

using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::pending_reply_t;
using zlink::spot_reqrep_internal::pending_spot_key_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;

enum : uint8_t
{
    zmp_spot_routed_protocol_id = 0x02,
    zmp_protocol_version = 0x01,
    zmp_packed_protocol_version = 0x02,
    zmp_spot_class = 0x01,
    zmp_router_class = 0x02
};

const size_t packed_spot_routed_control_part_count = 1;

int init_packed_spot_routed_header (zlink_msg_t *msg_,
                                    uint8_t source_class_,
                                    const std::string &source_node_rid_,
                                    const std::string &source_endpoint_rid_,
                                    uint8_t destination_class_,
                                    const std::string &destination_node_rid_,
                                    const std::string &destination_endpoint_rid_)
{
    if (!msg_) {
        errno = EFAULT;
        return -1;
    }

    const size_t total_size =
      20 + source_node_rid_.size () + source_endpoint_rid_.size ()
      + destination_node_rid_.size () + destination_endpoint_rid_.size ();
    if (zlink_msg_init_size (msg_, total_size) != 0)
        return -1;

    unsigned char *data = static_cast<unsigned char *> (zlink_msg_data (msg_));
    data[0] = zmp_spot_routed_protocol_id;
    data[1] = zmp_packed_protocol_version;
    data[2] = source_class_;
    data[3] = destination_class_;
    zlink::request_reply::encode_u32_be (
      static_cast<uint32_t> (source_node_rid_.size ()), data + 4);
    zlink::request_reply::encode_u32_be (
      static_cast<uint32_t> (source_endpoint_rid_.size ()), data + 8);
    zlink::request_reply::encode_u32_be (
      static_cast<uint32_t> (destination_node_rid_.size ()), data + 12);
    zlink::request_reply::encode_u32_be (
      static_cast<uint32_t> (destination_endpoint_rid_.size ()), data + 16);

    unsigned char *cursor = data + 20;
    if (!source_node_rid_.empty ()) {
        memcpy (cursor, source_node_rid_.data (), source_node_rid_.size ());
        cursor += source_node_rid_.size ();
    }
    if (!source_endpoint_rid_.empty ()) {
        memcpy (
          cursor, source_endpoint_rid_.data (), source_endpoint_rid_.size ());
        cursor += source_endpoint_rid_.size ();
    }
    if (!destination_node_rid_.empty ()) {
        memcpy (
          cursor, destination_node_rid_.data (), destination_node_rid_.size ());
        cursor += destination_node_rid_.size ();
    }
    if (!destination_endpoint_rid_.empty ()) {
        memcpy (cursor,
                destination_endpoint_rid_.data (),
                destination_endpoint_rid_.size ());
    }

    return 0;
}

std::string routing_id_key_local (const zlink_routing_id_t *peer_rid_)
{
    if (!peer_rid_ || peer_rid_->size == 0
        || peer_rid_->size > sizeof (peer_rid_->data))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
}

void routing_id_from_string_local (const std::string &value_,
                                   zlink_routing_id_t *out_)
{
    if (!out_)
        return;

    memset (out_, 0, sizeof (*out_));
    if (value_.empty ())
        return;

    const size_t size =
      value_.size () > sizeof (out_->data) ? sizeof (out_->data) : value_.size ();
    memcpy (out_->data, value_.data (), size);
    out_->size = static_cast<uint8_t> (size);
}

int dispatch_spot_message_local (spot_request_reply_state_t *state_,
                                 const zlink_routing_id_t *source_rid_,
                                 const zlink_routing_id_t *spot_rid_,
                                 uint64_t request_seq_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_)
{
    zlink_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->request_handler;
        handler_userdata = state_->request_handler_userdata;
    }

    if (handler) {
        handler (source_rid_, spot_rid_, request_seq_, parts_, part_count_,
                 handler_userdata);
        return 0;
    }

    if (zlink::spot_reqrep_internal::queue_spot_message (
          state_, source_rid_, spot_rid_, request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }
    return 0;
}

int dispatch_router_spot_message_local (
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
    if (reqrep::dispatch_router_message (
          router_state.get (), source_node_rid_, source_spot_rid_,
          request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }

    return 0;
}

bool parse_combined_local_message (
  std::vector<zlink_msg_t> *combined_,
  parsed_spot_envelope_t *spot_envelope_out_,
  zlink::request_reply::parsed_envelope_t *request_reply_envelope_out_)
{
    if (!combined_ || !spot_envelope_out_ || !request_reply_envelope_out_) {
        errno = EFAULT;
        return false;
    }

    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (
          &(*combined_)[0], combined_->size (), spot_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    if (!zlink::request_reply::parse_envelope (
          spot_envelope_out_->payload_parts, spot_envelope_out_->payload_part_count,
          request_reply_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    return true;
}

int deliver_reply_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (spot_envelope_.destination_node_rid,
                                   spot_envelope_.destination_endpoint_rid);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_spot_key_t key;
    key.source_class = spot_envelope_.source_class;
    key.source_rid = spot_envelope_.source_class == zmp_router_class
                       ? spot_envelope_.source_endpoint_rid
                       : spot_envelope_.source_node_rid;
    key.source_spot_rid = spot_envelope_.source_class == zmp_spot_class
                            ? spot_envelope_.source_endpoint_rid
                            : std::string ();
    key.request_seq = rr_envelope_.request_seq;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::unordered_map<pending_spot_key_t,
                           pending_reply_t,
                           zlink::spot_reqrep_internal::pending_spot_key_hash_t>::iterator it =
          state->pending_replies.find (key);
        if (it != state->pending_replies.end ()) {
            pending = it->second;
            state->pending_sequences.erase (key.request_seq);
            state->pending_replies.erase (it);
            found = true;
        }
    }

    zlink::request_timeout::cancel (pending.timeout_task);

    if (!found)
        return 0;

    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts,
          rr_envelope_.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        (void) zlink::spot_reqrep_internal::queue_spot_reply_completion (
          state, pending.handler, pending.userdata, EPROTO, NULL, 0);
        return -1;
    }

    return zlink::spot_reqrep_internal::queue_spot_reply_completion (
      state, pending.handler, pending.userdata, callback_errno, callback_parts,
      callback_part_count);
}

int deliver_reply_to_router (
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
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::unordered_map<uint64_t, pending_reply_t>::iterator it =
          state->pending_replies.find (rr_envelope_.request_seq);
        if (it != state->pending_replies.end ()) {
            pending = it->second;
            state->pending_sequences.erase (rr_envelope_.request_seq);
            state->pending_replies.erase (it);
            found = true;
        }
    }

    zlink::request_timeout::cancel (pending.timeout_task);

    if (!found)
        return 0;

    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts,
          rr_envelope_.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        (void) zlink::spot_reqrep_internal::queue_router_reply_completion (
          state, pending.handler, pending.userdata, EPROTO, NULL, 0);
        return -1;
    }

    return zlink::spot_reqrep_internal::queue_router_reply_completion (
      state, pending.handler, pending.userdata, callback_errno, callback_parts,
      callback_part_count);
}

int synthesize_local_error_reply (const parsed_spot_envelope_t &request_envelope_,
                                  uint64_t request_seq_,
                                  int errnum_)
{
    zlink_msg_t errno_part;
    zlink_msg_init (&errno_part);

    unsigned char errbuf[4];
    zlink::request_reply::encode_u32_be (static_cast<uint32_t> (errnum_),
                                         errbuf);
    if (zlink_msg_init_size (&errno_part, sizeof (errbuf)) != 0)
        return -1;
    memcpy (zlink_msg_data (&errno_part), errbuf, sizeof (errbuf));

    std::vector<zlink_msg_t> combined;
    if (zlink::spot_reqrep_internal::build_spot_request_reply_message (
          request_envelope_.destination_class,
          request_envelope_.destination_node_rid,
          request_envelope_.destination_endpoint_rid,
          request_envelope_.source_class, request_envelope_.source_node_rid,
          request_envelope_.source_endpoint_rid,
          zlink::request_reply::error_reply_type,
          request_seq_, &errno_part, 1, &combined)
        != 0) {
        const int saved_errno = errno;
        zlink::request_reply::consume_send_frame (&errno_part);
        errno = saved_errno;
        return -1;
    }

    const int rc = zlink::spot_reqrep_internal::dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int dispatch_spot_request_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (spot_envelope_.destination_node_rid,
                                   spot_envelope_.destination_endpoint_rid);
    if (!state)
        return synthesize_local_error_reply (spot_envelope_,
                                             rr_envelope_.request_seq,
                                             ENOENT);

    const int rc = dispatch_spot_message_local (
      state.get (),
      spot_envelope_.source_class == zmp_router_class
        ? &spot_envelope_.source_endpoint_rid_value
        : &spot_envelope_.source_node_rid_value,
      spot_envelope_.source_class == zmp_spot_class
        ? &spot_envelope_.source_endpoint_rid_value
        : NULL,
      rr_envelope_.request_seq,
      rr_envelope_.payload_parts, rr_envelope_.payload_part_count);
    return rc;
}

int dispatch_spot_request_to_router (
  const std::string &router_rid_,
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state)
        return synthesize_local_error_reply (spot_envelope_,
                                             rr_envelope_.request_seq,
                                             ENOENT);

    return dispatch_router_spot_message_local (
      state.get (),
      &spot_envelope_.source_node_rid_value,
      &spot_envelope_.source_endpoint_rid_value,
      rr_envelope_.request_seq, rr_envelope_.payload_parts,
      rr_envelope_.payload_part_count);
}

int dispatch_local_direct_to_spot (uint8_t source_class_,
                                   const std::string &source_node_rid_,
                                   const std::string &source_endpoint_rid_,
                                   const zlink_routing_id_t *source_node_rid_value_,
                                   const zlink_routing_id_t *source_endpoint_rid_value_,
                                   const std::string &dest_node_rid_,
                                   const std::string &dest_spot_rid_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (dest_node_rid_, dest_spot_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = 0;
        return 0;
    }

    zlink_routing_id_t source_rid_fallback;
    zlink_routing_id_t spot_rid_fallback;
    const zlink_routing_id_t *source_rid =
      source_class_ == zmp_router_class ? source_endpoint_rid_value_
                                        : source_node_rid_value_;
    const zlink_routing_id_t *spot_rid =
      source_class_ == zmp_spot_class ? source_endpoint_rid_value_ : NULL;
    if (!source_rid) {
        routing_id_from_string_local (
          source_class_ == zmp_router_class ? source_endpoint_rid_
                                            : source_node_rid_,
          &source_rid_fallback);
        source_rid = &source_rid_fallback;
    }
    if (source_class_ == zmp_spot_class && !spot_rid) {
        routing_id_from_string_local (source_endpoint_rid_, &spot_rid_fallback);
        spot_rid = &spot_rid_fallback;
    }

    if (dispatch_spot_message_local (state.get (), source_rid, spot_rid, 0,
                                     parts_, part_count_)
        != 0)
        return -1;
    errno = 0;
    return 0;
}

int dispatch_local_direct_to_router (const std::string &router_rid_,
                                     const std::string &source_node_rid_,
                                     const std::string &source_spot_rid_,
                                     const zlink_routing_id_t *source_node_rid_value_,
                                     const zlink_routing_id_t *source_spot_rid_value_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = ENOENT;
        return -1;
    }

    zlink_routing_id_t source_node_rid_fallback;
    zlink_routing_id_t source_spot_rid_fallback;
    const zlink_routing_id_t *source_node_rid = source_node_rid_value_;
    const zlink_routing_id_t *source_spot_rid = source_spot_rid_value_;
    if (!source_node_rid) {
        routing_id_from_string_local (source_node_rid_, &source_node_rid_fallback);
        source_node_rid = &source_node_rid_fallback;
    }
    if (!source_spot_rid) {
        routing_id_from_string_local (source_spot_rid_, &source_spot_rid_fallback);
        source_spot_rid = &source_spot_rid_fallback;
    }
    return dispatch_router_spot_message_local (
      state.get (), source_node_rid, source_spot_rid, 0, parts_, part_count_);
}
}

int zlink::spot_reqrep_internal::recv_combined_router_message (
  zlink::socket_base_t *socket_,
  std::vector<zlink_msg_t> *out_)
{
    if (!socket_ || !out_) {
        errno = EFAULT;
        return -1;
    }

    out_->clear ();

    zlink_msg_t first;
    while (true) {
        zlink_msg_init (&first);
        if (zlink::recv_msg_socket (socket_, ZLINK_CORE_SOCKET_ROUTER, &first,
                                    ZLINK_DONTWAIT)
            != 0) {
            if (errno == EAGAIN && socket_->socket_has_attached_pipes ()) {
                const int wait_rc = zlink::wait_socket_events_internal (
                  socket_, ZLINK_POLLIN, 1);
                if (wait_rc > 0
                    && zlink::recv_msg_socket (
                         socket_, ZLINK_CORE_SOCKET_ROUTER, &first,
                         ZLINK_DONTWAIT)
                         == 0) {
                    // Received a real frame after a short scheduler gap.
                } else {
                    if (wait_rc <= 0 && errno == 0)
                        errno = EAGAIN;
                    zlink_msg_close (&first);
                    return -1;
                }
            } else {
            zlink_msg_close (&first);
            return -1;
            }
        }

        const bool routing_id_has_more =
          zlink::internal_pair_queue::frame_has_more (first);
        zlink_msg_close (&first);

        if (!routing_id_has_more)
            continue;

        zlink_msg_t payload_first;
        zlink_msg_init (&payload_first);
        if (zlink::internal_pair_queue::recv_followup_with_retry (
              socket_, &payload_first, ZLINK_DONTWAIT)
            != 0) {
            const int saved_errno = errno;
            zlink_msg_close (&payload_first);
            errno = saved_errno;
            return -1;
        }

        out_->push_back (payload_first);
        break;
    }

    while (out_->empty ()
             || zlink::internal_pair_queue::frame_has_more (out_->back ())) {
        zlink_msg_t next;
        zlink_msg_init (&next);
        if (zlink::internal_pair_queue::recv_followup_with_retry (
              socket_, &next, ZLINK_DONTWAIT)
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            out_->clear ();
            errno = saved_errno;
            return -1;
        }
        out_->push_back (next);
    }

    return 0;
}

int zlink::spot_reqrep_internal::build_spot_request_reply_message (
  uint8_t source_class_,
  const std::string &source_node_rid_,
  const std::string &source_endpoint_rid_,
  uint8_t destination_class_,
  const std::string &destination_node_rid_,
  const std::string &destination_endpoint_rid_,
  uint8_t message_type_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_,
  std::vector<zlink_msg_t> *out_)
{
    if (!parts_ || part_count_ == 0 || request_seq_ == 0 || !out_) {
        errno = EINVAL;
        return -1;
    }

    const size_t total_part_count =
      packed_spot_routed_control_part_count
      + zlink::request_reply::control_part_count + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;
    unsigned char rr_protocol_id = zlink::request_reply::protocol_id;
    unsigned char rr_version = zmp_protocol_version;
    unsigned char rr_type = message_type_;
    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);

    if (init_packed_spot_routed_header (&(*out_)[0],
                                        source_class,
                                        source_node_rid_,
                                        source_endpoint_rid_,
                                        destination_class,
                                        destination_node_rid_,
                                        destination_endpoint_rid_)
          != 0
        || zlink::request_reply::init_control_part (&(*out_)[1],
                                                    &rr_protocol_id, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[2],
                                                    &rr_version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[3], &rr_type, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[4], seq_buf,
                                                    sizeof (seq_buf))
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[packed_spot_routed_control_part_count
                                     + zlink::request_reply::control_part_count
                                     + i],
                            &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}

int zlink::spot_reqrep_internal::build_spot_routed_message (
  uint8_t source_class_,
  const std::string &source_node_rid_,
  const std::string &source_endpoint_rid_,
  uint8_t destination_class_,
  const std::string &destination_node_rid_,
  const std::string &destination_endpoint_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  std::vector<zlink_msg_t> *out_)
{
    if (!parts_ || part_count_ == 0 || !out_) {
        errno = EINVAL;
        return -1;
    }

    const size_t total_part_count =
      packed_spot_routed_control_part_count + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;

    if (init_packed_spot_routed_header (&(*out_)[0],
                                        source_class,
                                        source_node_rid_,
                                        source_endpoint_rid_,
                                        destination_class,
                                        destination_node_rid_,
                                        destination_endpoint_rid_)
        != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[packed_spot_routed_control_part_count + i],
                            &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}

int zlink::spot_reqrep_internal::dispatch_local_reply_impl (
  std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == zmp_spot_class)
        return deliver_reply_to_spot (spot_envelope, rr_envelope);

    return deliver_reply_to_router (spot_envelope.destination_endpoint_rid,
                                    rr_envelope);
}

int zlink::spot_reqrep_internal::dispatch_local_request_impl (
  const std::string &router_rid_,
  std::vector<zlink_msg_t> *combined_)
{
    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == zmp_spot_class)
        return dispatch_spot_request_to_spot (spot_envelope, rr_envelope);

    return dispatch_spot_request_to_router (router_rid_, spot_envelope,
                                            rr_envelope);
}

int zlink::spot_reqrep_internal::dispatch_local_built_message (
  uint8_t source_class_,
  const std::string &source_node_rid_,
  const std::string &source_endpoint_rid_,
  uint8_t destination_class_,
  const std::string &destination_node_rid_,
  const std::string &destination_endpoint_rid_,
  uint8_t message_type_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          source_class_, source_node_rid_, source_endpoint_rid_,
          destination_class_, destination_node_rid_, destination_endpoint_rid_,
          message_type_, request_seq_, parts_, part_count_, &combined)
        != 0)
        return -1;

    const int rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int zlink::spot_reqrep_internal::process_parsed_route_combined_for_local_delivery_impl (
  std::vector<zlink_msg_t> *combined_,
  const parsed_spot_envelope_t &spot_envelope_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (zlink::request_reply::parse_envelope (spot_envelope_.payload_parts,
                                              spot_envelope_.payload_part_count,
                                              &rr_envelope)) {
        if (rr_envelope.message_type == zlink::request_reply::request_type) {
            if (spot_envelope_.destination_class == zmp_spot_class)
                return dispatch_spot_request_to_spot (spot_envelope_, rr_envelope);
            return dispatch_spot_request_to_router (
              spot_envelope_.destination_endpoint_rid, spot_envelope_,
              rr_envelope);
        }

        if (spot_envelope_.destination_class == zmp_spot_class)
            return deliver_reply_to_spot (spot_envelope_, rr_envelope);
        return deliver_reply_to_router (spot_envelope_.destination_endpoint_rid,
                                        rr_envelope);
    }

    if (spot_envelope_.destination_class == zmp_spot_class) {
        return dispatch_local_direct_to_spot (
          spot_envelope_.source_class, spot_envelope_.source_node_rid,
          spot_envelope_.source_endpoint_rid,
          &spot_envelope_.source_node_rid_value,
          &spot_envelope_.source_endpoint_rid_value,
          spot_envelope_.destination_node_rid,
          spot_envelope_.destination_endpoint_rid, spot_envelope_.payload_parts,
          spot_envelope_.payload_part_count);
    }

    return dispatch_local_direct_to_router (
      spot_envelope_.destination_endpoint_rid,
      spot_envelope_.source_node_rid,
      spot_envelope_.source_endpoint_rid,
      &spot_envelope_.source_node_rid_value,
      &spot_envelope_.source_endpoint_rid_value,
      spot_envelope_.payload_parts,
      spot_envelope_.payload_part_count);
}

int zlink::spot_reqrep_internal::process_route_combined_for_local_delivery_impl (
  std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (
          &(*combined_)[0], combined_->size (), &spot_envelope)) {
        errno = EPROTO;
        return -1;
    }

    return process_parsed_route_combined_for_local_delivery (combined_,
                                                             spot_envelope);
}
