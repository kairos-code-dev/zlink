/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/protocol/service_wire_codec.hpp"

#include <array>
#include <limits>

namespace zlink::framework::runtime::protocol
{
namespace
{

constexpr std::size_t liveness_size = 13;
constexpr std::size_t prefix_size = 5;
constexpr std::uint8_t application_payload_version = 1;

void append_u32 (std::vector<std::uint8_t> &bytes, std::uint32_t value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 24u) & 0xffu));
    bytes.push_back (static_cast<std::uint8_t> ((value >> 16u) & 0xffu));
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8u) & 0xffu));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xffu));
}

void append_u16 (std::vector<std::uint8_t> &bytes, std::uint16_t value)
{
    bytes.push_back (static_cast<std::uint8_t> ((value >> 8u) & 0xffu));
    bytes.push_back (static_cast<std::uint8_t> (value & 0xffu));
}

void append_u64 (std::vector<std::uint8_t> &bytes, std::uint64_t value)
{
    for (std::size_t index = 0; index < 8; ++index) {
        bytes.push_back (static_cast<std::uint8_t> (
          (value >> ((7 - index) * 8)) & 0xffu));
    }
}

std::uint16_t read_u16 (std::span<const std::uint8_t> bytes,
                        std::size_t &offset)
{
    if (bytes.size () - offset < 2) {
        throw service_wire_error_t ("truncated u16 field");
    }
    const auto value = static_cast<std::uint16_t> (
      (static_cast<std::uint16_t> (bytes[offset]) << 8u)
      | bytes[offset + 1]);
    offset += 2;
    return value;
}

std::uint32_t read_u32 (std::span<const std::uint8_t> bytes, std::size_t &offset)
{
    if (bytes.size () - offset < 4) {
        throw service_wire_error_t ("truncated u32 field");
    }
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value = (value << 8u) | bytes[offset++];
    }
    return value;
}

std::uint64_t read_u64 (std::span<const std::uint8_t> bytes,
                        std::size_t &offset)
{
    if (bytes.size () - offset < 8) {
        throw service_wire_error_t ("truncated u64 field");
    }
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8u) | bytes[offset++];
    }
    return value;
}

bool valid_utf8 (std::span<const std::uint8_t> bytes)
{
    for (std::size_t index = 0; index < bytes.size ();) {
        const auto first = bytes[index];
        std::size_t continuation = 0;
        std::uint32_t codepoint = 0;
        if (first <= 0x7f) {
            if (first == 0) {
                return false;
            }
            ++index;
            continue;
        }
        if ((first & 0xe0u) == 0xc0u) {
            continuation = 1;
            codepoint = first & 0x1fu;
        } else if ((first & 0xf0u) == 0xe0u) {
            continuation = 2;
            codepoint = first & 0x0fu;
        } else if ((first & 0xf8u) == 0xf0u) {
            continuation = 3;
            codepoint = first & 0x07u;
        } else {
            return false;
        }
        if (bytes.size () - index - 1 < continuation) {
            return false;
        }
        for (std::size_t part = 0; part < continuation; ++part) {
            const auto next = bytes[index + part + 1];
            if ((next & 0xc0u) != 0x80u) {
                return false;
            }
            codepoint = (codepoint << 6u) | (next & 0x3fu);
        }
        if ((continuation == 1 && codepoint < 0x80)
            || (continuation == 2 && codepoint < 0x800)
            || (continuation == 3 && codepoint < 0x10000)
            || codepoint > 0x10ffff
            || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            return false;
        }
        index += continuation + 1;
    }
    return true;
}

void append_text8 (std::vector<std::uint8_t> &bytes,
                   const std::string &value,
                   const char *field)
{
    if (value.empty () || value.size () > std::numeric_limits<std::uint8_t>::max ()
        || !valid_utf8 (std::span<const std::uint8_t> (
          reinterpret_cast<const std::uint8_t *> (value.data ()), value.size ()))) {
        throw service_wire_error_t (std::string (field)
                                    + " must be nonempty bounded UTF-8 without NUL");
    }
    bytes.push_back (static_cast<std::uint8_t> (value.size ()));
    bytes.insert (bytes.end (), value.begin (), value.end ());
}

std::string read_text8 (std::span<const std::uint8_t> bytes,
                        std::size_t &offset,
                        const char *field)
{
    if (offset >= bytes.size ()) {
        throw service_wire_error_t (std::string ("truncated ") + field);
    }
    const auto length = bytes[offset++];
    if (length == 0 || bytes.size () - offset < length) {
        throw service_wire_error_t (std::string ("invalid ") + field);
    }
    const auto value_bytes = bytes.subspan (offset, length);
    if (!valid_utf8 (value_bytes)) {
        throw service_wire_error_t (std::string ("invalid UTF-8 in ") + field);
    }
    offset += length;
    return std::string (
      reinterpret_cast<const char *> (value_bytes.data ()), value_bytes.size ());
}

void append_bytes8 (std::vector<std::uint8_t> &bytes,
                    const std::vector<std::uint8_t> &value,
                    const char *field)
{
    if (value.empty ()
        || value.size () > std::numeric_limits<std::uint8_t>::max ()) {
        throw service_wire_error_t (
          std::string (field) + " must contain 1..255 bytes");
    }
    bytes.push_back (static_cast<std::uint8_t> (value.size ()));
    bytes.insert (bytes.end (), value.begin (), value.end ());
}

std::vector<std::uint8_t> read_bytes8 (
  std::span<const std::uint8_t> bytes,
  std::size_t &offset,
  const char *field)
{
    if (offset >= bytes.size ()) {
        throw service_wire_error_t (std::string ("truncated ") + field);
    }
    const auto length = bytes[offset++];
    if (length == 0 || bytes.size () - offset < length) {
        throw service_wire_error_t (std::string ("invalid ") + field);
    }
    std::vector<std::uint8_t> value (
      bytes.begin () + static_cast<std::ptrdiff_t> (offset),
      bytes.begin () + static_cast<std::ptrdiff_t> (offset + length));
    offset += length;
    return value;
}

void append_text16 (std::vector<std::uint8_t> &bytes,
                    const std::string &value,
                    const char *field)
{
    if (value.empty () || value.size () > 4096
        || !valid_utf8 (std::span<const std::uint8_t> (
          reinterpret_cast<const std::uint8_t *> (value.data ()), value.size ()))) {
        throw service_wire_error_t (std::string (field)
                                    + " must be nonempty bounded UTF-8 without NUL");
    }
    append_u16 (bytes, static_cast<std::uint16_t> (value.size ()));
    bytes.insert (bytes.end (), value.begin (), value.end ());
}

std::string read_text16 (std::span<const std::uint8_t> bytes,
                         std::size_t &offset,
                         const char *field)
{
    const auto length = read_u16 (bytes, offset);
    if (length == 0 || length > 4096 || bytes.size () - offset < length) {
        throw service_wire_error_t (std::string ("invalid ") + field);
    }
    const auto value_bytes = bytes.subspan (offset, length);
    if (!valid_utf8 (value_bytes)) {
        throw service_wire_error_t (std::string ("invalid UTF-8 in ") + field);
    }
    offset += length;
    return std::string (
      reinterpret_cast<const char *> (value_bytes.data ()), value_bytes.size ());
}

void append_tlv (std::vector<std::uint8_t> &extension,
                 std::uint8_t id,
                 const std::vector<std::uint8_t> &value)
{
    extension.push_back (id);
    append_u32 (extension, static_cast<std::uint32_t> (value.size ()));
    extension.insert (extension.end (), value.begin (), value.end ());
}

std::uint8_t runtime_state_wire (mesh::service_node_state_t state)
{
    switch (state) {
        case mesh::service_node_state_t::preparing:
            return 0;
        case mesh::service_node_state_t::serving:
            return 1;
        case mesh::service_node_state_t::draining:
            return 2;
        case mesh::service_node_state_t::stopped:
            return 3;
        case mesh::service_node_state_t::error:
            return 4;
        default:
            throw service_wire_error_t (
              "retiring is a host state and cannot be encoded as a service descriptor");
    }
}

mesh::service_node_state_t runtime_state_from_wire (std::uint8_t value)
{
    switch (value) {
        case 0:
            return mesh::service_node_state_t::preparing;
        case 1:
            return mesh::service_node_state_t::serving;
        case 2:
            return mesh::service_node_state_t::draining;
        case 3:
            return mesh::service_node_state_t::stopped;
        case 4:
            return mesh::service_node_state_t::error;
        default:
            throw service_wire_error_t ("invalid runtime state");
    }
}

std::uint8_t object_role_wire (mesh::service_object_role_t role)
{
    return static_cast<std::uint8_t> (role);
}

mesh::service_object_role_t object_role_from_wire (std::uint8_t value)
{
    if (value > 2) {
        throw service_wire_error_t ("invalid object role");
    }
    return static_cast<mesh::service_object_role_t> (value);
}

void validate_admission_kind (command kind)
{
    if (kind != command::hello && kind != command::admit
        && kind != command::update) {
        throw service_wire_error_t (
          "command is not a RouteMesh admission record");
    }
}

void validate_kind (command kind)
{
    if (kind != command::livenessProbe && kind != command::livenessAck) {
        throw service_wire_error_t ("command is not a liveness record");
    }
}

} // namespace

std::vector<std::uint8_t> encode_node_send_header ()
{
    return {magic[0], magic[1], wire_major,
            static_cast<std::uint8_t> (command::nodeSend), 0};
}

std::vector<std::uint8_t>
encode_node_request_header (std::uint64_t correlation)
{
    if (correlation == 0) {
        throw service_wire_error_t ("request correlation must be nonzero");
    }
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major,
      static_cast<std::uint8_t> (command::nodeRequest), 0};
    append_u64 (result, correlation);
    return result;
}

std::uint64_t
decode_node_request_header (std::span<const std::uint8_t> bytes)
{
    const auto header = decode_header (bytes);
    if (header.kind != command::nodeRequest || header.flags != 0
        || bytes.size () != prefix_size + 8) {
        throw service_wire_error_t ("invalid nodeRequest header");
    }
    std::size_t offset = prefix_size;
    const auto correlation = read_u64 (bytes, offset);
    if (correlation == 0) {
        throw service_wire_error_t ("request correlation must be nonzero");
    }
    return correlation;
}

std::vector<std::uint8_t> encode_channel_request_header (
  std::uint64_t correlation,
  const std::string &channel_name)
{
    if (correlation == 0) {
        throw service_wire_error_t ("request correlation must be nonzero");
    }
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major,
      static_cast<std::uint8_t> (command::channelRequest), 0};
    append_u64 (result, correlation);
    append_text8 (result, channel_name, "channel name");
    return result;
}

channel_request_header_t
decode_channel_request_header (std::span<const std::uint8_t> bytes)
{
    const auto header = decode_header (bytes);
    if (header.kind != command::channelRequest || header.flags != 0) {
        throw service_wire_error_t ("invalid channelRequest header");
    }
    std::size_t offset = prefix_size;
    const auto correlation = read_u64 (bytes, offset);
    if (correlation == 0) {
        throw service_wire_error_t ("request correlation must be nonzero");
    }
    auto channel_name = read_text8 (bytes, offset, "channel name");
    if (offset != bytes.size ()) {
        throw service_wire_error_t ("channelRequest header has trailing bytes");
    }
    return {correlation, std::move (channel_name)};
}

std::vector<std::uint8_t>
encode_channel_send_header (const std::string &channel_name)
{
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major,
      static_cast<std::uint8_t> (command::channelSend), 0};
    append_text8 (result, channel_name, "channel name");
    return result;
}

std::vector<std::uint8_t> encode_spot_message_header (
  command kind,
  const std::string &source_spot_id,
  const spot_route_fence_t &target,
  std::optional<std::uint64_t> correlation)
{
    if (kind != command::spotSend && kind != command::spotRequest) {
        throw service_wire_error_t ("command is not a Spot message");
    }
    if ((kind == command::spotRequest) != correlation.has_value ()
        || (correlation && *correlation == 0)
        || target.object_generation == 0
        || target.target_node_generation == 0
        || target.authority_owner_generation == 0) {
        throw service_wire_error_t ("invalid Spot route fence");
    }
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major, static_cast<std::uint8_t> (kind), 0};
    if (correlation) {
        append_u64 (result, *correlation);
    }
    append_text8 (result, source_spot_id, "source SpotId");
    append_text8 (result, target.spot_id, "target SpotId");
    append_u64 (result, target.object_generation);
    append_bytes8 (
      result, target.target_node_routing_id, "target node RID");
    append_u64 (result, target.target_node_generation);
    append_u64 (result, target.authority_owner_generation);
    return result;
}

spot_message_header_t decode_spot_message_header (
  std::span<const std::uint8_t> bytes,
  command expected_kind)
{
    const auto header = decode_header (bytes);
    if ((expected_kind != command::spotSend
         && expected_kind != command::spotRequest)
        || header.kind != expected_kind || header.flags != 0) {
        throw service_wire_error_t ("invalid Spot message header");
    }
    std::size_t offset = prefix_size;
    std::optional<std::uint64_t> correlation;
    if (expected_kind == command::spotRequest) {
        correlation = read_u64 (bytes, offset);
        if (*correlation == 0) {
            throw service_wire_error_t (
              "Spot request correlation must be nonzero");
        }
    }
    spot_message_header_t result;
    result.correlation = correlation;
    result.source_spot_id =
      read_text8 (bytes, offset, "source SpotId");
    result.target.spot_id =
      read_text8 (bytes, offset, "target SpotId");
    result.target.object_generation = read_u64 (bytes, offset);
    result.target.target_node_routing_id =
      read_bytes8 (bytes, offset, "target node RID");
    result.target.target_node_generation = read_u64 (bytes, offset);
    result.target.authority_owner_generation = read_u64 (bytes, offset);
    if (result.target.object_generation == 0
        || result.target.target_node_generation == 0
        || result.target.authority_owner_generation == 0
        || offset != bytes.size ()) {
        throw service_wire_error_t (
          "invalid or trailing Spot route fence");
    }
    return result;
}

std::vector<std::uint8_t> encode_actor_message_header (
  command kind,
  const std::optional<std::pair<std::string, std::uint64_t>> &source_actor,
  const actor_route_fence_t &target,
  std::optional<std::uint64_t> correlation)
{
    if (kind != command::actorSend && kind != command::actorRequest) {
        throw service_wire_error_t ("command is not an Actor message");
    }
    if ((kind == command::actorRequest) != correlation.has_value ()
        || (correlation && *correlation == 0)
        || target.object_generation == 0
        || target.target_node_generation == 0
        || target.authority_owner_generation == 0) {
        throw service_wire_error_t ("invalid Actor route fence");
    }
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major, static_cast<std::uint8_t> (kind), 0};
    if (correlation) {
        append_u64 (result, *correlation);
    }
    if (source_actor) {
        append_text8 (result, source_actor->first, "source Actor ID");
        if (source_actor->second == 0) {
            throw service_wire_error_t (
              "source Actor generation must be nonzero");
        }
        append_u64 (result, source_actor->second);
    } else {
        result.push_back (0);
    }
    append_text8 (result, target.actor_id, "target Actor ID");
    append_u64 (result, target.object_generation);
    append_bytes8 (
      result, target.target_node_routing_id, "target node RID");
    append_u64 (result, target.target_node_generation);
    append_u64 (result, target.authority_owner_generation);
    return result;
}

actor_message_header_t decode_actor_message_header (
  std::span<const std::uint8_t> bytes,
  command expected_kind)
{
    const auto header = decode_header (bytes);
    if ((expected_kind != command::actorSend
         && expected_kind != command::actorRequest)
        || header.kind != expected_kind || header.flags != 0) {
        throw service_wire_error_t ("invalid Actor message header");
    }
    std::size_t offset = prefix_size;
    std::optional<std::uint64_t> correlation;
    if (expected_kind == command::actorRequest) {
        correlation = read_u64 (bytes, offset);
        if (*correlation == 0) {
            throw service_wire_error_t (
              "Actor request correlation must be nonzero");
        }
    }
    actor_message_header_t result;
    result.correlation = correlation;
    if (offset >= bytes.size ()) {
        throw service_wire_error_t ("truncated source Actor");
    }
    if (bytes[offset] == 0) {
        ++offset;
    } else {
        auto actor_id = read_text8 (bytes, offset, "source Actor ID");
        const auto generation = read_u64 (bytes, offset);
        if (generation == 0) {
            throw service_wire_error_t (
              "source Actor generation must be nonzero");
        }
        result.source_actor =
          std::pair{std::move (actor_id), generation};
    }
    result.target.actor_id =
      read_text8 (bytes, offset, "target Actor ID");
    result.target.object_generation = read_u64 (bytes, offset);
    result.target.target_node_routing_id =
      read_bytes8 (bytes, offset, "target node RID");
    result.target.target_node_generation = read_u64 (bytes, offset);
    result.target.authority_owner_generation = read_u64 (bytes, offset);
    if (result.target.object_generation == 0
        || result.target.target_node_generation == 0
        || result.target.authority_owner_generation == 0
        || offset != bytes.size ()) {
        throw service_wire_error_t (
          "invalid or trailing Actor route fence");
    }
    return result;
}

std::vector<std::uint8_t> encode_user_spot_create_header (
  const user_spot_create_header_t &record)
{
    if (record.correlation == 0
        || (record.operation.high == 0 && record.operation.low == 0)
        || record.source_node_generation == 0
        || record.reservation.object_generation == 0
        || record.reservation.authority_owner_generation == 0
        || record.reservation.target_node_generation == 0
        || record.reservation.target_owner_lease_generation == 0
        || record.reservation.pending_capacity_delta == 0
        || record.deadline_unix_ms == 0) {
        throw service_wire_error_t (
          "user Spot create contains a zero required fence");
    }
    std::vector<std::uint8_t> bytes{
      magic[0], magic[1], wire_major,
      static_cast<std::uint8_t> (command::userSpotCreate), 0};
    append_u64 (bytes, record.correlation);
    append_u64 (bytes, record.operation.high);
    append_u64 (bytes, record.operation.low);
    append_bytes8 (
      bytes, record.source_node_routing_id, "source node RID");
    append_u64 (bytes, record.source_node_generation);
    append_text8 (bytes, record.spot_id, "spot ID");
    append_text8 (bytes, record.stable_type, "stable type");
    append_text8 (
      bytes, record.reservation.reservation_id, "reservation ID");
    append_text16 (
      bytes, record.reservation.expected_store_version,
      "expected StoreVersion");
    append_u64 (bytes, record.reservation.object_generation);
    append_u64 (
      bytes, record.reservation.authority_owner_generation);
    append_bytes8 (
      bytes, record.reservation.target_node_routing_id,
      "target node RID");
    append_u64 (bytes, record.reservation.target_node_generation);
    append_text8 (
      bytes, record.reservation.target_owner_id, "target owner ID");
    append_u64 (
      bytes, record.reservation.target_owner_lease_generation);
    append_u32 (bytes, record.reservation.pending_capacity_delta);
    append_u64 (bytes, record.deadline_unix_ms);
    return bytes;
}

user_spot_create_header_t decode_user_spot_create_header (
  std::span<const std::uint8_t> bytes)
{
    const auto header = decode_header (bytes);
    if (header.kind != command::userSpotCreate || header.flags != 0) {
        throw service_wire_error_t (
          "record is not a User Spot create command");
    }
    std::size_t offset = prefix_size;
    user_spot_create_header_t record;
    record.correlation = read_u64 (bytes, offset);
    record.operation.high = read_u64 (bytes, offset);
    record.operation.low = read_u64 (bytes, offset);
    record.source_node_routing_id =
      read_bytes8 (bytes, offset, "source node RID");
    record.source_node_generation = read_u64 (bytes, offset);
    record.spot_id = read_text8 (bytes, offset, "spot ID");
    record.stable_type = read_text8 (bytes, offset, "stable type");
    record.reservation.reservation_id =
      read_text8 (bytes, offset, "reservation ID");
    record.reservation.expected_store_version =
      read_text16 (bytes, offset, "expected StoreVersion");
    record.reservation.object_generation = read_u64 (bytes, offset);
    record.reservation.authority_owner_generation =
      read_u64 (bytes, offset);
    record.reservation.target_node_routing_id =
      read_bytes8 (bytes, offset, "target node RID");
    record.reservation.target_node_generation =
      read_u64 (bytes, offset);
    record.reservation.target_owner_id =
      read_text8 (bytes, offset, "target owner ID");
    record.reservation.target_owner_lease_generation =
      read_u64 (bytes, offset);
    record.reservation.pending_capacity_delta =
      read_u32 (bytes, offset);
    record.deadline_unix_ms = read_u64 (bytes, offset);
    if (offset != bytes.size ()) {
        throw service_wire_error_t (
          "User Spot create command has trailing bytes");
    }
    if (record.correlation == 0
        || (record.operation.high == 0 && record.operation.low == 0)
        || record.source_node_generation == 0
        || record.reservation.object_generation == 0
        || record.reservation.authority_owner_generation == 0
        || record.reservation.target_node_generation == 0
        || record.reservation.target_owner_lease_generation == 0
        || record.reservation.pending_capacity_delta == 0
        || record.deadline_unix_ms == 0) {
        throw service_wire_error_t (
          "user Spot create contains a zero required fence");
    }
    return record;
}

std::vector<std::uint8_t> encode_user_spot_close_header (
  const user_spot_close_header_t &record)
{
    if (record.correlation == 0
        || (record.operation.high == 0 && record.operation.low == 0)
        || record.source_node_generation == 0
        || record.target.object_generation == 0
        || record.target.target_node_generation == 0
        || record.target.authority_owner_generation == 0
        || record.deadline_unix_ms == 0) {
        throw service_wire_error_t (
          "user Spot close contains a zero required fence");
    }
    std::vector<std::uint8_t> bytes{
      magic[0], magic[1], wire_major,
      static_cast<std::uint8_t> (command::userSpotClose), 0};
    append_u64 (bytes, record.correlation);
    append_u64 (bytes, record.operation.high);
    append_u64 (bytes, record.operation.low);
    append_bytes8 (
      bytes, record.source_node_routing_id, "source node RID");
    append_u64 (bytes, record.source_node_generation);
    std::vector<std::uint8_t> fence;
    append_text8 (fence, record.target.spot_id, "spot ID");
    append_u64 (fence, record.target.object_generation);
    append_bytes8 (
      fence, record.target.target_node_routing_id, "target node RID");
    append_u64 (fence, record.target.target_node_generation);
    append_u64 (fence, record.target.authority_owner_generation);
    append_text16 (
      fence, record.target.expected_store_version,
      "expected StoreVersion");
    if (fence.size () > std::numeric_limits<std::uint16_t>::max ()) {
        throw service_wire_error_t ("User Spot close fence is too large");
    }
    bytes.push_back (1);
    append_u16 (bytes, static_cast<std::uint16_t> (fence.size ()));
    bytes.insert (bytes.end (), fence.begin (), fence.end ());
    append_u64 (bytes, record.deadline_unix_ms);
    return bytes;
}

user_spot_close_header_t decode_user_spot_close_header (
  std::span<const std::uint8_t> bytes)
{
    const auto header = decode_header (bytes);
    if (header.kind != command::userSpotClose || header.flags != 0) {
        throw service_wire_error_t (
          "record is not a User Spot close command");
    }
    std::size_t offset = prefix_size;
    user_spot_close_header_t record;
    record.correlation = read_u64 (bytes, offset);
    record.operation.high = read_u64 (bytes, offset);
    record.operation.low = read_u64 (bytes, offset);
    record.source_node_routing_id =
      read_bytes8 (bytes, offset, "source node RID");
    record.source_node_generation = read_u64 (bytes, offset);
    if (offset >= bytes.size () || bytes[offset++] != 1) {
        throw service_wire_error_t (
          "unsupported User Spot close fence version");
    }
    const auto fence_size = read_u16 (bytes, offset);
    if (bytes.size () - offset < fence_size) {
        throw service_wire_error_t (
          "truncated User Spot close fence");
    }
    const auto fence_end = offset + fence_size;
    const auto fence_bytes = bytes.first (fence_end);
    record.target.spot_id =
      read_text8 (fence_bytes, offset, "spot ID");
    record.target.object_generation = read_u64 (fence_bytes, offset);
    record.target.target_node_routing_id =
      read_bytes8 (fence_bytes, offset, "target node RID");
    record.target.target_node_generation =
      read_u64 (fence_bytes, offset);
    record.target.authority_owner_generation =
      read_u64 (fence_bytes, offset);
    record.target.expected_store_version =
      read_text16 (fence_bytes, offset, "expected StoreVersion");
    if (offset != fence_end) {
        throw service_wire_error_t (
          "User Spot close fence has trailing bytes");
    }
    record.deadline_unix_ms = read_u64 (bytes, offset);
    if (offset != bytes.size ()) {
        throw service_wire_error_t (
          "User Spot close command has trailing bytes");
    }
    if (record.correlation == 0
        || (record.operation.high == 0 && record.operation.low == 0)
        || record.source_node_generation == 0
        || record.target.object_generation == 0
        || record.target.target_node_generation == 0
        || record.target.authority_owner_generation == 0
        || record.deadline_unix_ms == 0) {
        throw service_wire_error_t (
          "user Spot close contains a zero required fence");
    }
    return record;
}

service_wire_header_t decode_header (std::span<const std::uint8_t> bytes)
{
    if (bytes.size () < prefix_size) {
        throw service_wire_error_t ("truncated service wire prefix");
    }
    if (bytes[0] != magic[0] || bytes[1] != magic[1]) {
        throw service_wire_error_t ("invalid service wire magic");
    }
    if (bytes[2] != wire_major) {
        throw service_wire_error_t ("unsupported service wire major");
    }
    return service_wire_header_t{
      static_cast<command> (bytes[3]), bytes[4]};
}

std::string
decode_channel_send_header (std::span<const std::uint8_t> bytes)
{
    const auto header = decode_header (bytes);
    if (header.kind != command::channelSend || header.flags != 0) {
        throw service_wire_error_t ("frame is not a channelSend header");
    }
    std::size_t offset = prefix_size;
    auto channel_name = read_text8 (bytes, offset, "channel name");
    if (offset != bytes.size ()) {
        throw service_wire_error_t ("channelSend header has trailing bytes");
    }
    return channel_name;
}

std::vector<std::uint8_t>
encode_application_payload (const application_payload_t &payload)
{
    if (payload.payload.size ()
        > std::numeric_limits<std::uint32_t>::max ()) {
        throw service_wire_error_t ("application payload exceeds u32");
    }
    std::vector<std::uint8_t> body;
    append_text8 (body, payload.packet_name, "packet name");
    append_text8 (body, payload.content_type, "content type");
    append_u32 (body, static_cast<std::uint32_t> (payload.payload.size ()));
    body.insert (body.end (), payload.payload.begin (), payload.payload.end ());
    if (body.size () > std::numeric_limits<std::uint32_t>::max ()) {
        throw service_wire_error_t ("application payload envelope exceeds u32");
    }
    std::vector<std::uint8_t> result;
    result.reserve (5 + body.size ());
    result.push_back (application_payload_version);
    append_u32 (result, static_cast<std::uint32_t> (body.size ()));
    result.insert (result.end (), body.begin (), body.end ());
    return result;
}

application_payload_t
decode_application_payload (std::span<const std::uint8_t> bytes)
{
    if (bytes.size () < 5 || bytes[0] != application_payload_version) {
        throw service_wire_error_t ("invalid application payload version");
    }
    std::size_t offset = 1;
    const auto body_length = read_u32 (bytes, offset);
    if (body_length != bytes.size () - offset) {
        throw service_wire_error_t (
          "application payload body length does not match frame");
    }
    application_payload_t result;
    result.packet_name = read_text8 (bytes, offset, "packet name");
    result.content_type = read_text8 (bytes, offset, "content type");
    const auto payload_length = read_u32 (bytes, offset);
    if (payload_length != bytes.size () - offset) {
        throw service_wire_error_t (
          "application payload length does not match frame");
    }
    result.payload.assign (bytes.begin () + static_cast<std::ptrdiff_t> (offset),
                           bytes.end ());
    return result;
}

std::vector<std::uint8_t> encode_route_mesh_admission (
  command kind,
  const mesh::service_node_descriptor_t &descriptor)
{
    validate_admission_kind (kind);
    try {
        static_cast<void> (
          mesh::service_topology_registry_t (descriptor));
    }
    catch (const std::invalid_argument &error) {
        throw service_wire_error_t (error.what ());
    }

    std::vector<std::uint8_t> route;
    append_text8 (route, descriptor.mesh_name, "mesh name");
    append_text8 (route, descriptor.security_identity, "security identity");
    append_u32 (route, descriptor.effective_max_message_bytes);
    append_u64 (route, descriptor.lifecycle_generation);
    append_u64 (route, descriptor.descriptor_revision);
    append_text16 (route, descriptor.advertised_endpoint, "advertised endpoint");
    if (descriptor.channels.size ()
        > std::numeric_limits<std::uint16_t>::max ()) {
        throw service_wire_error_t ("channel vector exceeds u16");
    }
    append_u16 (route, static_cast<std::uint16_t> (descriptor.channels.size ()));
    for (const auto &channel : descriptor.channels) {
        append_text8 (route, channel.name, "channel name");
        append_u32 (
          route,
          static_cast<std::uint32_t> (channel.weight));
    }

    std::vector<std::uint8_t> extension;
    append_tlv (
      extension, 1,
      {runtime_state_wire (descriptor.state)});
    std::vector<std::uint8_t> application_version;
    append_u64 (
      application_version,
      static_cast<std::uint64_t> (descriptor.application_version));
    append_tlv (extension, 2, application_version);

    if (descriptor.protocol_capabilities.size ()
        > std::numeric_limits<std::uint16_t>::max ()) {
        throw service_wire_error_t ("protocol capability vector exceeds u16");
    }
    std::vector<std::uint8_t> capabilities;
    append_u16 (
      capabilities,
      static_cast<std::uint16_t> (descriptor.protocol_capabilities.size ()));
    for (const auto &capability : descriptor.protocol_capabilities) {
        append_text8 (capabilities, capability, "protocol capability");
    }
    append_tlv (extension, 6, capabilities);
    append_tlv (
      extension, 7,
      {object_role_wire (descriptor.object_role)});
    for (const auto &[id, value] :
         std::array<std::pair<std::uint8_t, std::uint32_t>, 5>{
           std::pair<std::uint8_t, std::uint32_t>{
             8, static_cast<std::uint32_t> (
                  descriptor.placement_weight)},
           {9, descriptor.active_capacity_limit},
           {10, descriptor.pending_capacity_limit},
           {11, descriptor.active_capacity_used},
           {12, descriptor.pending_capacity_used}}) {
        std::vector<std::uint8_t> encoded;
        append_u32 (encoded, value);
        append_tlv (extension, id, encoded);
    }
    append_u32 (route, static_cast<std::uint32_t> (extension.size ()));
    route.insert (route.end (), extension.begin (), extension.end ());

    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major, static_cast<std::uint8_t> (kind), 0,
      1};
    append_u32 (result, static_cast<std::uint32_t> (route.size ()));
    result.insert (result.end (), route.begin (), route.end ());
    return result;
}

mesh::service_node_descriptor_t decode_route_mesh_admission (
  std::span<const std::uint8_t> bytes,
  command expected_kind,
  std::vector<std::uint8_t> source_routing_id)
{
    validate_admission_kind (expected_kind);
    const auto header = decode_header (bytes);
    if (header.kind != expected_kind || header.flags != 0) {
        throw service_wire_error_t ("unexpected admission command or flags");
    }
    std::size_t offset = prefix_size;
    if (offset >= bytes.size () || bytes[offset++] != 1) {
        throw service_wire_error_t ("admission is not RouteMesh topology");
    }
    const auto route_length = read_u32 (bytes, offset);
    if (route_length != bytes.size () - offset) {
        throw service_wire_error_t ("RouteMesh admission length mismatch");
    }

    mesh::service_node_descriptor_t result;
    result.node_routing_id = std::move (source_routing_id);
    result.mesh_name = read_text8 (bytes, offset, "mesh name");
    result.security_identity =
      read_text8 (bytes, offset, "security identity");
    result.effective_max_message_bytes = read_u32 (bytes, offset);
    result.lifecycle_generation = read_u64 (bytes, offset);
    result.descriptor_revision = read_u64 (bytes, offset);
    result.advertised_endpoint =
      read_text16 (bytes, offset, "advertised endpoint");
    const auto channel_count = read_u16 (bytes, offset);
    result.channels.reserve (channel_count);
    for (std::uint16_t index = 0; index < channel_count; ++index) {
        auto name =
          read_text8 (bytes, offset, "channel name");
        const auto weight = read_u32 (bytes, offset);
        if (weight > 10000)
            throw service_wire_error_t (
              "channel weight is outside 0..10000");
        result.channels.push_back (
          {std::move (name), static_cast<int> (weight)});
    }

    const auto extension_length = read_u32 (bytes, offset);
    if (extension_length != bytes.size () - offset) {
        throw service_wire_error_t ("descriptor extension length mismatch");
    }
    const auto extension_end = offset + extension_length;
    std::uint16_t required = 0;
    std::uint8_t previous_id = 0;
    while (offset < extension_end) {
        const auto id = bytes[offset++];
        const auto length = read_u32 (bytes, offset);
        if (id <= previous_id || length > extension_end - offset) {
            throw service_wire_error_t (
              "descriptor TLV order or length is invalid");
        }
        previous_id = id;
        const auto value = bytes.subspan (offset, length);
        offset += length;
        std::size_t value_offset = 0;
        switch (id) {
            case 1:
                if (value.size () != 1) {
                    throw service_wire_error_t ("runtime state TLV length");
                }
                result.state = runtime_state_from_wire (value[0]);
                required |= 1u << 0u;
                break;
            case 2:
                if (value.size () != 8) {
                    throw service_wire_error_t (
                      "application version TLV length");
                }
                result.application_version =
                  static_cast<std::int64_t> (read_u64 (value, value_offset));
                if (result.application_version < 0) {
                    throw service_wire_error_t ("negative application version");
                }
                required |= 1u << 1u;
                break;
            case 6: {
                const auto count = read_u16 (value, value_offset);
                result.protocol_capabilities.clear ();
                result.protocol_capabilities.reserve (count);
                for (std::uint16_t index = 0; index < count; ++index) {
                    result.protocol_capabilities.push_back (
                      read_text8 (value, value_offset, "protocol capability"));
                }
                if (value_offset != value.size ()) {
                    throw service_wire_error_t (
                      "protocol capability TLV trailing bytes");
                }
                required |= 1u << 2u;
                break;
            }
            case 7:
                if (value.size () != 1) {
                    throw service_wire_error_t ("object role TLV length");
                }
                result.object_role = object_role_from_wire (value[0]);
                required |= 1u << 3u;
                break;
            case 8:
                result.placement_weight = static_cast<int> (
                  read_u32 (value, value_offset));
                required |= 1u << 4u;
                break;
            case 9:
                result.active_capacity_limit =
                  read_u32 (value, value_offset);
                required |= 1u << 5u;
                break;
            case 10:
                result.pending_capacity_limit =
                  read_u32 (value, value_offset);
                required |= 1u << 6u;
                break;
            case 11:
                result.active_capacity_used =
                  read_u32 (value, value_offset);
                required |= 1u << 7u;
                break;
            case 12:
                result.pending_capacity_used =
                  read_u32 (value, value_offset);
                required |= 1u << 8u;
                break;
            default:
                break;
        }
        if (id >= 8 && id <= 12 && value_offset != value.size ()) {
            throw service_wire_error_t ("u32 descriptor TLV length");
        }
    }
    if (required != 0x1ffu) {
        throw service_wire_error_t (
          "descriptor extension omits a required field");
    }
    try {
        static_cast<void> (mesh::service_topology_registry_t (result));
    }
    catch (const std::invalid_argument &error) {
        throw service_wire_error_t (error.what ());
    }
    return result;
}

std::vector<std::uint8_t> encode_client_server_client_admission (
  command kind,
  const client_server_client_admission_t &admission)
{
    validate_admission_kind (kind);
    if (admission.effective_max_message_bytes == 0) {
        throw service_wire_error_t (
          "client effective max message bytes must be nonzero");
    }
    std::vector<std::uint8_t> body;
    append_text8 (body, admission.channel_name, "channel name");
    body.push_back (1);
    append_text8 (
      body, admission.security_identity, "security identity");
    append_u32 (body, admission.effective_max_message_bytes);
    std::vector<std::uint8_t> client_server{1};
    append_u16 (
      client_server, static_cast<std::uint16_t> (body.size ()));
    client_server.insert (
      client_server.end (), body.begin (), body.end ());
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major, static_cast<std::uint8_t> (kind), 0, 2};
    append_u32 (
      result, static_cast<std::uint32_t> (client_server.size ()));
    result.insert (
      result.end (), client_server.begin (), client_server.end ());
    return result;
}

client_server_client_admission_t decode_client_server_client_admission (
  std::span<const std::uint8_t> bytes,
  command expected_kind)
{
    validate_admission_kind (expected_kind);
    const auto header = decode_header (bytes);
    if (header.kind != expected_kind || header.flags != 0) {
        throw service_wire_error_t ("unexpected client admission command");
    }
    std::size_t offset = prefix_size;
    if (bytes.size () - offset < 1 || bytes[offset++] != 2) {
        throw service_wire_error_t ("admission is not ClientServer");
    }
    const auto outer_length = read_u32 (bytes, offset);
    if (outer_length != bytes.size () - offset
        || bytes.size () - offset < 3 || bytes[offset++] != 1) {
        throw service_wire_error_t ("invalid ClientServer client envelope");
    }
    const auto body_length = read_u16 (bytes, offset);
    if (body_length != bytes.size () - offset) {
        throw service_wire_error_t ("ClientServer client length mismatch");
    }
    client_server_client_admission_t result;
    result.channel_name = read_text8 (bytes, offset, "channel name");
    if (offset >= bytes.size () || bytes[offset++] != 1) {
        throw service_wire_error_t ("invalid ClientServer direction");
    }
    result.security_identity =
      read_text8 (bytes, offset, "security identity");
    result.effective_max_message_bytes = read_u32 (bytes, offset);
    if (result.effective_max_message_bytes == 0 || offset != bytes.size ()) {
        throw service_wire_error_t (
          "invalid ClientServer client admission");
    }
    return result;
}

std::vector<std::uint8_t> encode_client_server_server_admission (
  command kind,
  const client_server_server_admission_t &admission)
{
    validate_admission_kind (kind);
    if (admission.lifecycle_generation == 0
        || admission.descriptor_revision == 0 || admission.weight > 10000
        || admission.effective_max_message_bytes == 0) {
        throw service_wire_error_t (
          "invalid ClientServer server descriptor");
    }
    std::vector<std::uint8_t> body;
    append_text8 (body, admission.channel_name, "channel name");
    body.push_back (1);
    append_bytes8 (
      body, admission.server_routing_id, "server routing id");
    append_u64 (body, admission.lifecycle_generation);
    append_u64 (body, admission.descriptor_revision);
    append_u32 (body, admission.weight);
    body.push_back (runtime_state_wire (admission.state));
    append_text8 (
      body, admission.security_identity, "security identity");
    append_u32 (body, admission.effective_max_message_bytes);
    append_text16 (
      body, admission.advertised_endpoint, "advertised endpoint");
    if (body.size () > std::numeric_limits<std::uint16_t>::max ()) {
        throw service_wire_error_t (
          "ClientServer server descriptor exceeds u16");
    }
    std::vector<std::uint8_t> client_server{2};
    append_u16 (
      client_server, static_cast<std::uint16_t> (body.size ()));
    client_server.insert (
      client_server.end (), body.begin (), body.end ());
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major, static_cast<std::uint8_t> (kind), 0, 2};
    append_u32 (
      result, static_cast<std::uint32_t> (client_server.size ()));
    result.insert (
      result.end (), client_server.begin (), client_server.end ());
    return result;
}

client_server_server_admission_t decode_client_server_server_admission (
  std::span<const std::uint8_t> bytes,
  command expected_kind)
{
    validate_admission_kind (expected_kind);
    const auto header = decode_header (bytes);
    if (header.kind != expected_kind || header.flags != 0) {
        throw service_wire_error_t ("unexpected server admission command");
    }
    std::size_t offset = prefix_size;
    if (bytes.size () - offset < 1 || bytes[offset++] != 2) {
        throw service_wire_error_t ("admission is not ClientServer");
    }
    const auto outer_length = read_u32 (bytes, offset);
    if (outer_length != bytes.size () - offset
        || bytes.size () - offset < 3 || bytes[offset++] != 2) {
        throw service_wire_error_t ("invalid ClientServer server envelope");
    }
    const auto body_length = read_u16 (bytes, offset);
    if (body_length != bytes.size () - offset) {
        throw service_wire_error_t ("ClientServer server length mismatch");
    }
    client_server_server_admission_t result;
    result.channel_name = read_text8 (bytes, offset, "channel name");
    if (offset >= bytes.size () || bytes[offset++] != 1) {
        throw service_wire_error_t ("invalid ClientServer direction");
    }
    result.server_routing_id =
      read_bytes8 (bytes, offset, "server routing id");
    result.lifecycle_generation = read_u64 (bytes, offset);
    result.descriptor_revision = read_u64 (bytes, offset);
    result.weight = read_u32 (bytes, offset);
    if (offset >= bytes.size ()) {
        throw service_wire_error_t ("truncated ClientServer state");
    }
    result.state = runtime_state_from_wire (bytes[offset++]);
    result.security_identity =
      read_text8 (bytes, offset, "security identity");
    result.effective_max_message_bytes = read_u32 (bytes, offset);
    result.advertised_endpoint =
      read_text16 (bytes, offset, "advertised endpoint");
    if (result.lifecycle_generation == 0
        || result.descriptor_revision == 0 || result.weight > 10000
        || result.effective_max_message_bytes == 0
        || offset != bytes.size ()) {
        throw service_wire_error_t (
          "invalid ClientServer server admission");
    }
    return result;
}

std::vector<std::uint8_t> encode_reject (std::uint32_t reason)
{
    if (reason < 1 || reason > 12) {
        throw service_wire_error_t ("invalid reject reason");
    }
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major,
      static_cast<std::uint8_t> (command::reject), 0};
    append_u32 (result, reason);
    return result;
}

std::uint32_t decode_reject (std::span<const std::uint8_t> bytes)
{
    const auto header = decode_header (bytes);
    if (header.kind != command::reject || header.flags != 0
        || bytes.size () != prefix_size + 4) {
        throw service_wire_error_t ("invalid reject record");
    }
    std::size_t offset = prefix_size;
    const auto reason = read_u32 (bytes, offset);
    if (reason < 1 || reason > 12) {
        throw service_wire_error_t ("invalid reject reason");
    }
    return reason;
}

std::vector<std::uint8_t> encode_reply_header (
  std::uint64_t correlation,
  std::uint32_t terminal_result,
  std::uint32_t failure_code)
{
    if (correlation == 0) {
        throw service_wire_error_t ("reply correlation must be nonzero");
    }
    const auto typed_failure =
      terminal_result == 102 || (terminal_result >= 104
                                 && terminal_result <= 107);
    const auto valid_failure =
      failure_code <= 22
      || (failure_code >= 33 && failure_code <= 35);
    if ((terminal_result != 0
         && (terminal_result < 101 || terminal_result > 113))
        || !valid_failure || (terminal_result == 0 && failure_code != 0)
        || (typed_failure && failure_code == 0)
        || (!typed_failure && failure_code != 0)) {
        throw service_wire_error_t ("invalid reply terminal fields");
    }
    std::vector<std::uint8_t> result{
      magic[0], magic[1], wire_major,
      static_cast<std::uint8_t> (command::reply), 0};
    append_u64 (result, correlation);
    append_u32 (result, terminal_result);
    append_u32 (result, failure_code);
    return result;
}

reply_header_t decode_reply_header (std::span<const std::uint8_t> bytes)
{
    const auto header = decode_header (bytes);
    if (header.kind != command::reply || header.flags != 0
        || bytes.size () != prefix_size + 16) {
        throw service_wire_error_t ("invalid reply header");
    }
    std::size_t offset = prefix_size;
    const auto correlation = read_u64 (bytes, offset);
    const auto terminal = read_u32 (bytes, offset);
    const auto failure = read_u32 (bytes, offset);
    const auto typed_failure =
      terminal == 102 || (terminal >= 104 && terminal <= 107);
    const auto valid_failure =
      failure <= 22 || (failure >= 33 && failure <= 35);
    if (correlation == 0
        || (terminal != 0 && (terminal < 101 || terminal > 113))
        || !valid_failure || (terminal == 0 && failure != 0)
        || (typed_failure && failure == 0)
        || (!typed_failure && failure != 0)) {
        throw service_wire_error_t ("invalid reply terminal fields");
    }
    return {correlation, terminal, failure};
}

std::vector<std::uint8_t> encode_user_spot_create_reply (
  std::uint64_t correlation,
  std::uint32_t terminal_result,
  std::uint32_t failure_code,
  user_spot_create_result_t result,
  const std::string &spot_id,
  std::uint64_t object_generation)
{
    auto bytes =
      encode_reply_header (correlation, terminal_result, failure_code);
    if (terminal_result != 0) {
        return bytes;
    }
    const auto result_value = static_cast<std::uint8_t> (result);
    if (result_value < 1 || result_value > 3
        || object_generation == 0) {
        throw service_wire_error_t (
          "invalid User Spot create success reply");
    }
    bytes.push_back (result_value);
    append_text8 (bytes, spot_id, "spot ID");
    append_u64 (bytes, object_generation);
    return bytes;
}

user_spot_create_reply_t decode_user_spot_create_reply (
  std::span<const std::uint8_t> bytes)
{
    if (bytes.size () < prefix_size + 16) {
        throw service_wire_error_t (
          "truncated User Spot create reply");
    }
    const auto header =
      decode_reply_header (bytes.first (prefix_size + 16));
    user_spot_create_reply_t reply;
    reply.header = header;
    if (header.terminal_result != 0) {
        if (bytes.size () != prefix_size + 16) {
            throw service_wire_error_t (
              "failed User Spot create reply has a tail");
        }
        return reply;
    }
    std::size_t offset = prefix_size + 16;
    if (offset >= bytes.size () || bytes[offset] < 1
        || bytes[offset] > 3) {
        throw service_wire_error_t (
          "invalid User Spot create result");
    }
    reply.result =
      static_cast<user_spot_create_result_t> (bytes[offset++]);
    reply.spot_id =
      read_text8 (bytes, offset, "spot ID");
    reply.object_generation = read_u64 (bytes, offset);
    if (reply.object_generation == 0 || offset != bytes.size ()) {
        throw service_wire_error_t (
          "invalid User Spot create success reply");
    }
    return reply;
}

std::vector<std::uint8_t> encode_user_spot_close_reply (
  std::uint64_t correlation,
  std::uint32_t terminal_result,
  std::uint32_t failure_code,
  bool closed)
{
    auto bytes =
      encode_reply_header (correlation, terminal_result, failure_code);
    if (terminal_result == 0) {
        bytes.push_back (closed ? 1 : 0);
    }
    return bytes;
}

user_spot_close_reply_t decode_user_spot_close_reply (
  std::span<const std::uint8_t> bytes)
{
    if (bytes.size () < prefix_size + 16) {
        throw service_wire_error_t (
          "truncated User Spot close reply");
    }
    const auto header =
      decode_reply_header (bytes.first (prefix_size + 16));
    user_spot_close_reply_t reply;
    reply.header = header;
    if (header.terminal_result != 0) {
        if (bytes.size () != prefix_size + 16) {
            throw service_wire_error_t (
              "failed User Spot close reply has a tail");
        }
        return reply;
    }
    if (bytes.size () != prefix_size + 17
        || (bytes.back () != 0 && bytes.back () != 1)) {
        throw service_wire_error_t (
          "invalid User Spot close success reply");
    }
    reply.closed = bytes.back () == 1;
    return reply;
}

std::vector<std::uint8_t> encode_liveness (command kind, std::uint64_t probe_id)
{
    validate_kind (kind);
    if (probe_id == 0) {
        throw service_wire_error_t ("liveness probe id must be nonzero");
    }
    std::vector<std::uint8_t> bytes (liveness_size);
    bytes[0] = magic[0];
    bytes[1] = magic[1];
    bytes[2] = wire_major;
    bytes[3] = static_cast<std::uint8_t> (kind);
    bytes[4] = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[5 + index] = static_cast<std::uint8_t> (
          (probe_id >> ((7 - index) * 8)) & 0xffu);
    }
    return bytes;
}

liveness_record_t decode_liveness (std::span<const std::uint8_t> bytes)
{
    if (bytes.size () < liveness_size) {
        throw service_wire_error_t ("truncated liveness record");
    }
    if (bytes.size () > liveness_size) {
        throw service_wire_error_t ("liveness record has trailing bytes");
    }
    const auto header = decode_header (bytes);
    const auto kind = header.kind;
    validate_kind (kind);
    if (header.flags != 0) {
        throw service_wire_error_t ("liveness record uses a forbidden flag");
    }
    std::uint64_t probe_id = 0;
    for (std::size_t index = 5; index < bytes.size (); ++index) {
        probe_id = (probe_id << 8u) | bytes[index];
    }
    if (probe_id == 0) {
        throw service_wire_error_t ("liveness probe id must be nonzero");
    }
    return liveness_record_t{kind, probe_id};
}

} // namespace zlink::framework::runtime::protocol
