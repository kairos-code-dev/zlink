/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "utils/routing_id.hpp"

namespace
{
using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::resolve_spot_node_routing_id;

const size_t packed_spot_routed_control_part_count = 1;

bool assign_routing_id_value_local (const char *data_, size_t size_, zlink_routing_id_t *out_rid_)
{
    if (!out_rid_)
        return false;
    memset (out_rid_, 0, sizeof (*out_rid_));
    if (!data_ || size_ == 0)
        return true;
    if (size_ > sizeof (out_rid_->data))
        return false;
    memcpy (out_rid_->data, data_, size_);
    out_rid_->size = static_cast<uint8_t> (size_);
    return true;
}

bool parse_packed_spot_routed_envelope (zlink_msg_t *parts_,
                                        size_t part_count_,
                                        parsed_spot_envelope_t *out_)
{
    const size_t packed_header_prefix_size = 20;
    zlink::msg_t *first_frame = reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (!first_frame->check ())
        return false;

    const unsigned char *data = static_cast<const unsigned char *> (zlink_msg_data (&parts_[0]));
    const size_t size = zlink_msg_size (&parts_[0]);
    if (size < packed_header_prefix_size || data[0] != zlink::spot_routed_protocol::protocol_id
        || data[1] != zlink::spot_routed_protocol::packed_frame_version
        || !zlink::spot_routed_protocol::is_endpoint_class (data[2])
        || !zlink::spot_routed_protocol::is_endpoint_class (data[3])) {
        return false;
    }

    const uint32_t source_node_size = zlink::request_reply::decode_u32_be (data + 4);
    const uint32_t source_endpoint_size = zlink::request_reply::decode_u32_be (data + 8);
    const uint32_t destination_node_size = zlink::request_reply::decode_u32_be (data + 12);
    const uint32_t destination_endpoint_size = zlink::request_reply::decode_u32_be (data + 16);
    const size_t total_header_size =
      packed_header_prefix_size + static_cast<size_t> (source_node_size)
      + static_cast<size_t> (source_endpoint_size) + static_cast<size_t> (destination_node_size)
      + static_cast<size_t> (destination_endpoint_size);
    if (size < total_header_size)
        return false;

    const char *cursor = reinterpret_cast<const char *> (data) + packed_header_prefix_size;
    out_->source_class = data[2];
    out_->destination_class = data[3];
    out_->source_node_rid.assign (cursor, source_node_size);
    if (!assign_routing_id_value_local (cursor, source_node_size, &out_->source_node_rid_value)) {
        return false;
    }
    cursor += source_node_size;
    out_->source_endpoint_rid.assign (cursor, source_endpoint_size);
    if (!assign_routing_id_value_local (cursor, source_endpoint_size,
                                        &out_->source_endpoint_rid_value)) {
        return false;
    }
    cursor += source_endpoint_size;
    out_->destination_node_rid.assign (cursor, destination_node_size);
    if (!assign_routing_id_value_local (cursor, destination_node_size,
                                        &out_->destination_node_rid_value)) {
        return false;
    }
    cursor += destination_node_size;
    out_->destination_endpoint_rid.assign (cursor, destination_endpoint_size);
    if (!assign_routing_id_value_local (cursor, destination_endpoint_size,
                                        &out_->destination_endpoint_rid_value)) {
        return false;
    }
    out_->payload_parts = parts_ + 1;
    out_->payload_part_count = part_count_ - 1;
    return true;
}

bool peek_packed_destination_node_rid (zlink_msg_t *parts_,
                                       size_t part_count_,
                                       zlink_routing_id_t *out_)
{
    if (!out_)
        return false;
    memset (out_, 0, sizeof (*out_));
    if (!parts_ || part_count_ == 0)
        return false;

    const size_t packed_header_prefix_size = 20;
    zlink::msg_t *first_frame = reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (!first_frame->check ())
        return false;

    const unsigned char *data = static_cast<const unsigned char *> (zlink_msg_data (&parts_[0]));
    const size_t size = zlink_msg_size (&parts_[0]);
    if (size < packed_header_prefix_size || data[0] != zlink::spot_routed_protocol::protocol_id
        || data[1] != zlink::spot_routed_protocol::packed_frame_version
        || !zlink::spot_routed_protocol::is_endpoint_class (data[2])
        || !zlink::spot_routed_protocol::is_endpoint_class (data[3])) {
        return false;
    }

    const uint32_t source_node_size = zlink::request_reply::decode_u32_be (data + 4);
    const uint32_t source_endpoint_size = zlink::request_reply::decode_u32_be (data + 8);
    const uint32_t destination_node_size = zlink::request_reply::decode_u32_be (data + 12);
    const uint32_t destination_endpoint_size = zlink::request_reply::decode_u32_be (data + 16);
    const size_t destination_offset = packed_header_prefix_size
                                      + static_cast<size_t> (source_node_size)
                                      + static_cast<size_t> (source_endpoint_size);
    const size_t total_header_size = destination_offset
                                     + static_cast<size_t> (destination_node_size)
                                     + static_cast<size_t> (destination_endpoint_size);
    if (size < total_header_size)
        return false;
    if (destination_node_size == 0 || destination_node_size > sizeof (out_->data))
        return false;

    memcpy (out_->data, data + destination_offset, destination_node_size);
    out_->size = static_cast<uint8_t> (destination_node_size);
    return true;
}

template <typename InitPrefixFn>
int build_packed_spot_message_with_payload (zlink_msg_t *parts_,
                                            size_t part_count_,
                                            zlink_msg_t *out_,
                                            size_t out_count_,
                                            size_t expected_out_count_,
                                            size_t payload_offset_,
                                            InitPrefixFn init_prefix_)
{
    if (!parts_ || part_count_ == 0 || !out_ || out_count_ != expected_out_count_) {
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < out_count_; ++i)
        zlink_msg_init (&out_[i]);

    if (init_prefix_ (out_) != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_, out_count_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&out_[payload_offset_ + i], &parts_[i]) != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_, out_count_);
            zlink::request_reply::consume_send_frames_from (parts_, i, part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}
}

bool zlink::spot_reqrep_internal::parse_spot_routed_envelope (zlink_msg_t *parts_,
                                                              size_t part_count_,
                                                              parsed_spot_envelope_t *out_)
{
    if (!parts_ || !out_ || part_count_ == 0)
        return false;

    memset (&out_->source_node_rid_value, 0, sizeof (out_->source_node_rid_value));
    memset (&out_->source_endpoint_rid_value, 0, sizeof (out_->source_endpoint_rid_value));
    memset (&out_->destination_node_rid_value, 0, sizeof (out_->destination_node_rid_value));
    memset (&out_->destination_endpoint_rid_value, 0,
            sizeof (out_->destination_endpoint_rid_value));

    return parse_packed_spot_routed_envelope (parts_, part_count_, out_);
}

bool zlink::spot_reqrep_internal::peek_spot_routed_destination_node_rid (zlink_msg_t *parts_,
                                                                         size_t part_count_,
                                                                         zlink_routing_id_t *out_)
{
    return peek_packed_destination_node_rid (parts_, part_count_, out_);
}

int zlink::spot_reqrep_internal::init_packed_spot_routed_header (
  zlink_msg_t *msg_,
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

    const size_t packed_header_prefix_size = 20;
    const size_t total_size = packed_header_prefix_size + source_node_rid_.size ()
                              + source_endpoint_rid_.size () + destination_node_rid_.size ()
                              + destination_endpoint_rid_.size ();
    if (zlink_msg_init_size (msg_, total_size) != 0)
        return -1;

    unsigned char *data = static_cast<unsigned char *> (zlink_msg_data (msg_));
    data[0] = zlink::spot_routed_protocol::protocol_id;
    data[1] = zlink::spot_routed_protocol::packed_frame_version;
    data[2] = source_class_;
    data[3] = destination_class_;
    zlink::request_reply::encode_u32_be (static_cast<uint32_t> (source_node_rid_.size ()),
                                         data + 4);
    zlink::request_reply::encode_u32_be (static_cast<uint32_t> (source_endpoint_rid_.size ()),
                                         data + 8);
    zlink::request_reply::encode_u32_be (static_cast<uint32_t> (destination_node_rid_.size ()),
                                         data + 12);
    zlink::request_reply::encode_u32_be (static_cast<uint32_t> (destination_endpoint_rid_.size ()),
                                         data + 16);

    unsigned char *cursor = data + packed_header_prefix_size;
    if (!source_node_rid_.empty ()) {
        memcpy (cursor, source_node_rid_.data (), source_node_rid_.size ());
        cursor += source_node_rid_.size ();
    }
    if (!source_endpoint_rid_.empty ()) {
        memcpy (cursor, source_endpoint_rid_.data (), source_endpoint_rid_.size ());
        cursor += source_endpoint_rid_.size ();
    }
    if (!destination_node_rid_.empty ()) {
        memcpy (cursor, destination_node_rid_.data (), destination_node_rid_.size ());
        cursor += destination_node_rid_.size ();
    }
    if (!destination_endpoint_rid_.empty ()) {
        memcpy (cursor, destination_endpoint_rid_.data (), destination_endpoint_rid_.size ());
    }

    return 0;
}

size_t
zlink::spot_reqrep_internal::spot_request_reply_message_part_count (size_t payload_part_count_)
{
    return packed_spot_routed_control_part_count + zlink::request_reply::control_part_count
           + payload_part_count_;
}

size_t zlink::spot_reqrep_internal::spot_routed_message_part_count (size_t payload_part_count_)
{
    return packed_spot_routed_control_part_count + payload_part_count_;
}

int zlink::spot_reqrep_internal::build_spot_request_reply_message_into (
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
  zlink_msg_t *out_,
  size_t out_count_)
{
    if (request_seq_ == 0) {
        errno = EINVAL;
        return -1;
    }

    const size_t expected_out_count = spot_request_reply_message_part_count (part_count_);
    const size_t payload_offset =
      packed_spot_routed_control_part_count + zlink::request_reply::control_part_count;
    return build_packed_spot_message_with_payload (
      parts_, part_count_, out_, out_count_, expected_out_count, payload_offset,
      [&] (zlink_msg_t *out_parts_) {
          if (init_packed_spot_routed_header (
                &out_parts_[0], source_class_, source_node_rid_, source_endpoint_rid_,
                destination_class_, destination_node_rid_, destination_endpoint_rid_)
              != 0) {
              return -1;
          }
          return zlink::request_reply::init_envelope_control_parts (
            out_parts_ + packed_spot_routed_control_part_count, message_type_, request_seq_);
      });
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

    const size_t total_part_count = spot_request_reply_message_part_count (part_count_);
    out_->resize (total_part_count);
    return build_spot_request_reply_message_into (
      source_class_, source_node_rid_, source_endpoint_rid_, destination_class_,
      destination_node_rid_, destination_endpoint_rid_, message_type_, request_seq_, parts_,
      part_count_, &(*out_)[0], total_part_count);
}

int zlink::spot_reqrep_internal::build_spot_routed_message_into (
  uint8_t source_class_,
  const std::string &source_node_rid_,
  const std::string &source_endpoint_rid_,
  uint8_t destination_class_,
  const std::string &destination_node_rid_,
  const std::string &destination_endpoint_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_msg_t *out_,
  size_t out_count_)
{
    const size_t expected_out_count = spot_routed_message_part_count (part_count_);
    return build_packed_spot_message_with_payload (
      parts_, part_count_, out_, out_count_, expected_out_count,
      packed_spot_routed_control_part_count,
      [&] (zlink_msg_t *out_parts_) {
          return init_packed_spot_routed_header (
            &out_parts_[0], source_class_, source_node_rid_, source_endpoint_rid_,
            destination_class_, destination_node_rid_, destination_endpoint_rid_);
      });
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

    const size_t total_part_count = spot_routed_message_part_count (part_count_);
    out_->resize (total_part_count);
    return build_spot_routed_message_into (source_class_, source_node_rid_, source_endpoint_rid_,
                                           destination_class_, destination_node_rid_,
                                           destination_endpoint_rid_, parts_, part_count_,
                                           &(*out_)[0], total_part_count);
}

bool zlink::spot_reqrep_internal::resolve_spot_node_routing_id (spot_node_t *node_,
                                                                std::string *out_)
{
    if (!node_ || !out_) {
        errno = EFAULT;
        return false;
    }

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node_->node_routing_id (&node_rid) != 0 || node_rid.size == 0)
        return false;

    *out_ = zlink::routing_id_key (node_rid);
    return true;
}

bool zlink::spot_reqrep_internal::should_process_spot_routed_locally (
  spot_node_t *node_, const parsed_spot_envelope_t &envelope_)
{
    if (!node_)
        return false;

    if (envelope_.destination_class == zlink::spot_routed_protocol::router_endpoint_class) {
        return static_cast<bool> (find_router_state_by_rid (envelope_.destination_endpoint_rid));
    }

    zlink_routing_id_t local_node_rid;
    memset (&local_node_rid, 0, sizeof (local_node_rid));
    return node_->node_routing_id (&local_node_rid) == 0
           && local_node_rid.size == envelope_.destination_node_rid_value.size
           && local_node_rid.size > 0
           && memcmp (local_node_rid.data, envelope_.destination_node_rid_value.data,
                      local_node_rid.size)
                == 0;
}
