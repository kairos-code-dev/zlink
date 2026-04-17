/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/routing_id_internal.hpp"
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
    zmp_spot_class = 0x01,
    zmp_router_class = 0x02
};

const size_t spot_routed_control_part_count = 8;

std::string routing_id_key_local (const zlink_routing_id_t *peer_rid_)
{
    if (!peer_rid_ || peer_rid_->size == 0 || peer_rid_->size > 255)
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

    (void) zlink::routing_id_internal::assign_view (
      out_, reinterpret_cast<const uint8_t *> (value_.data ()),
      std::min (value_.size (), zlink::routing_id_internal::owned_capacity ()));
}

bool parse_spot_routed_envelope_local (zlink_msg_t *parts_,
                                       size_t part_count_,
                                       parsed_spot_envelope_t *out_)
{
    if (!parts_ || !out_ || part_count_ < spot_routed_control_part_count)
        return false;

    zlink::msg_t *protocol_id = reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (!protocol_id->check ()
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[0], zmp_spot_routed_protocol_id)
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[1], zmp_protocol_version)) {
        return false;
    }

    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                              zmp_router_class))
        return false;
    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                              zmp_router_class))
        return false;

    out_->source_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[2]))[0];
    out_->source_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[3])),
      zlink_msg_size (&parts_[3]));
    out_->source_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[4])),
      zlink_msg_size (&parts_[4]));
    out_->destination_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[5]))[0];
    out_->destination_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[6])),
      zlink_msg_size (&parts_[6]));
    out_->destination_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[7])),
      zlink_msg_size (&parts_[7]));
    out_->payload_parts = parts_ + spot_routed_control_part_count;
    out_->payload_part_count = part_count_ - spot_routed_control_part_count;
    return true;
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

    if (!parse_spot_routed_envelope_local (&(*combined_)[0], combined_->size (),
                                           spot_envelope_out_)) {
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
        std::map<pending_spot_key_t, pending_reply_t>::iterator it =
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
        return -1;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    return 0;
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
        std::map<uint64_t, pending_reply_t>::iterator it =
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
        return -1;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    return 0;
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

    zlink_routing_id_t source_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string_local (
      spot_envelope_.source_class == zmp_router_class
        ? spot_envelope_.source_endpoint_rid
        : spot_envelope_.source_node_rid,
      &source_rid);
    routing_id_from_string_local (
      spot_envelope_.source_class == zmp_spot_class
        ? spot_envelope_.source_endpoint_rid
        : std::string (),
      &source_spot_rid);

    return dispatch_spot_message_local (
      state.get (), &source_rid, &source_spot_rid, rr_envelope_.request_seq,
      rr_envelope_.payload_parts, rr_envelope_.payload_part_count);
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

    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string_local (spot_envelope_.source_node_rid, &source_node_rid);
    routing_id_from_string_local (spot_envelope_.source_endpoint_rid,
                                  &source_spot_rid);
    return dispatch_router_spot_message_local (
      state.get (), &source_node_rid, &source_spot_rid,
      rr_envelope_.request_seq, rr_envelope_.payload_parts,
      rr_envelope_.payload_part_count);
}

int dispatch_local_direct_to_spot (uint8_t source_class_,
                                   const std::string &source_node_rid_,
                                   const std::string &source_endpoint_rid_,
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

    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    routing_id_from_string_local (
      source_class_ == zmp_router_class ? source_endpoint_rid_ : source_node_rid_,
      &source_rid);
    routing_id_from_string_local (
      source_class_ == zmp_spot_class ? source_endpoint_rid_ : std::string (),
      &spot_rid);

    if (dispatch_spot_message_local (state.get (), &source_rid,
                                     spot_rid.size > 0 ? &spot_rid : NULL, 0,
                                     parts_, part_count_)
        != 0)
        return -1;
    errno = 0;
    return 0;
}

int dispatch_local_direct_to_router (const std::string &router_rid_,
                                     const std::string &source_node_rid_,
                                     const std::string &source_spot_rid_,
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

    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string_local (source_node_rid_, &source_node_rid);
    routing_id_from_string_local (source_spot_rid_, &source_spot_rid);
    return dispatch_router_spot_message_local (
      state.get (), &source_node_rid, &source_spot_rid, 0, parts_, part_count_);
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

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));

    zlink_msg_t first;
    zlink_msg_init (&first);
    if (zlink::recv_msg_routed_socket (socket_, &first, &source_rid,
                                       ZLINK_DONTWAIT)
        != 0) {
        zlink_msg_close (&first);
        return -1;
    }

    out_->push_back (first);
    while (zlink::internal_pair_queue::frame_has_more (out_->back ())) {
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
      spot_routed_control_part_count + zlink::request_reply::control_part_count
      + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char spot_protocol_id = zmp_spot_routed_protocol_id;
    unsigned char version = zmp_protocol_version;
    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;
    unsigned char rr_protocol_id = zlink::request_reply::protocol_id;
    unsigned char rr_type = message_type_;
    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);

    if (zlink::request_reply::init_control_part (&(*out_)[0], &spot_protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&(*out_)[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[2], &source_class, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[3],
                                                    source_node_rid_.data (),
                                                    source_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[4],
                                                    source_endpoint_rid_.data (),
                                                    source_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[5],
                                                    &destination_class, 1)
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[6], destination_node_rid_.data (),
             destination_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[7], destination_endpoint_rid_.data (),
             destination_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[8],
                                                    &rr_protocol_id, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[9], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[10], &rr_type, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[11], seq_buf,
                                                    sizeof (seq_buf))
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[spot_routed_control_part_count
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

    const size_t total_part_count = spot_routed_control_part_count + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char spot_protocol_id = zmp_spot_routed_protocol_id;
    unsigned char version = zmp_protocol_version;
    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;

    if (zlink::request_reply::init_control_part (&(*out_)[0], &spot_protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&(*out_)[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[2], &source_class, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[3],
                                                    source_node_rid_.data (),
                                                    source_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[4],
                                                    source_endpoint_rid_.data (),
                                                    source_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[5],
                                                    &destination_class, 1)
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[6], destination_node_rid_.data (),
             destination_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[7], destination_endpoint_rid_.data (),
             destination_endpoint_rid_.size ())
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[spot_routed_control_part_count + i], &parts_[i])
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

int zlink::spot_reqrep_internal::dispatch_local_reply (
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

int zlink::spot_reqrep_internal::dispatch_local_request (
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

int zlink::spot_reqrep_internal::process_route_combined_for_local_delivery (
  std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    if (!parse_spot_routed_envelope_local (&(*combined_)[0], combined_->size (),
                                           &spot_envelope)) {
        errno = EPROTO;
        return -1;
    }

    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (zlink::request_reply::parse_envelope (spot_envelope.payload_parts,
                                              spot_envelope.payload_part_count,
                                              &rr_envelope)) {
        if (rr_envelope.message_type == zlink::request_reply::request_type) {
            if (spot_envelope.destination_class == zmp_spot_class)
                return dispatch_spot_request_to_spot (spot_envelope, rr_envelope);
            return dispatch_spot_request_to_router (
              spot_envelope.destination_endpoint_rid, spot_envelope, rr_envelope);
        }

        if (spot_envelope.destination_class == zmp_spot_class)
            return deliver_reply_to_spot (spot_envelope, rr_envelope);
        return deliver_reply_to_router (spot_envelope.destination_endpoint_rid,
                                        rr_envelope);
    }

    if (spot_envelope.destination_class == zmp_spot_class) {
        return dispatch_local_direct_to_spot (
          spot_envelope.source_class, spot_envelope.source_node_rid,
          spot_envelope.source_endpoint_rid, spot_envelope.destination_node_rid,
          spot_envelope.destination_endpoint_rid, spot_envelope.payload_parts,
          spot_envelope.payload_part_count);
    }

    return dispatch_local_direct_to_router (
      spot_envelope.destination_endpoint_rid, spot_envelope.source_node_rid,
      spot_envelope.source_endpoint_rid, spot_envelope.payload_parts,
      spot_envelope.payload_part_count);
}
