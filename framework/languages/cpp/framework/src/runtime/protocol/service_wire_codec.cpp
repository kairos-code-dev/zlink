/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/protocol/service_wire_codec.hpp"

#include <array>

namespace zlink::framework::runtime::protocol
{
namespace
{

constexpr std::size_t liveness_size = 13;

void validate_kind (command kind)
{
    if (kind != command::livenessProbe && kind != command::livenessAck) {
        throw service_wire_error_t ("command is not a liveness record");
    }
}

} // namespace

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
    if (bytes[0] != magic[0] || bytes[1] != magic[1]) {
        throw service_wire_error_t ("invalid service wire magic");
    }
    if (bytes[2] != wire_major) {
        throw service_wire_error_t ("unsupported service wire major");
    }
    const auto kind = static_cast<command> (bytes[3]);
    validate_kind (kind);
    if (bytes[4] != 0) {
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
