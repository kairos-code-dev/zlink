/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/stores.hpp>

#include <optional>
#include <string_view>
#include <vector>

namespace zlink::framework::runtime
{

struct pending_creation_projection_t
{
    object_creation_intent_t intent;
};

inline std::optional<std::vector<std::byte>>
decode_inline_creation_content (std::string_view reference)
{
    constexpr std::string_view prefix = "inline-v1:";
    if (!reference.starts_with (prefix))
        return std::nullopt;
    reference.remove_prefix (prefix.size ());
    const auto separator = reference.find (':');
    if (separator != 8 || separator + 1 >= reference.size ())
        return std::nullopt;
    std::uint32_t expected_crc = 0;
    for (const auto value : reference.substr (0, separator)) {
        expected_crc <<= 4u;
        if (value >= '0' && value <= '9')
            expected_crc |= static_cast<std::uint32_t> (value - '0');
        else if (value >= 'a' && value <= 'f')
            expected_crc |= static_cast<std::uint32_t> (value - 'a' + 10);
        else
            return std::nullopt;
    }
    const auto encoded = reference.substr (separator + 1);
    if (encoded.size () % 4u == 1u)
        return std::nullopt;
    std::vector<std::byte> payload;
    payload.reserve ((encoded.size () * 3u) / 4u + 2u);
    std::uint32_t accumulator = 0;
    unsigned bits = 0;
    for (const auto value : encoded) {
        std::uint32_t decoded = 0;
        if (value >= 'A' && value <= 'Z')
            decoded = static_cast<std::uint32_t> (value - 'A');
        else if (value >= 'a' && value <= 'z')
            decoded = static_cast<std::uint32_t> (value - 'a' + 26);
        else if (value >= '0' && value <= '9')
            decoded = static_cast<std::uint32_t> (value - '0' + 52);
        else if (value == '-')
            decoded = 62;
        else if (value == '_')
            decoded = 63;
        else
            return std::nullopt;
        accumulator = (accumulator << 6u) | decoded;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            payload.push_back (static_cast<std::byte> (
              (accumulator >> bits) & 0xffu));
        }
    }
    if (bits != 0
        && (accumulator & ((std::uint32_t{1} << bits) - 1u)) != 0)
        return std::nullopt;
    std::uint32_t actual_crc = 0xffffffffu;
    for (const auto value : payload) {
        actual_crc ^= std::to_integer<std::uint8_t> (value);
        for (int bit = 0; bit < 8; ++bit)
            actual_crc = (actual_crc >> 1u)
              ^ (0x82f63b78u
                 & (0u - (actual_crc & 1u)));
    }
    actual_crc = ~actual_crc;
    if (actual_crc != expected_crc)
        return std::nullopt;
    return payload;
}

// Internal capability used by an operation target to obtain the immutable
// creation content that belongs to an exact reservation fence. Implementations
// return the exact reservation intent. The operation target decodes and checks
// the application content reference before invoking application lifecycle code.
class pending_creation_projection_provider_t
{
  public:
    virtual ~pending_creation_projection_provider_t () = default;

    virtual std::optional<pending_creation_projection_t>
    read_verified_pending_creation (
      object_creation_key_t key,
      const object_reservation_fence_t &fence) = 0;
};

} // namespace zlink::framework::runtime
