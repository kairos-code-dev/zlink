#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace sample {

// Encode: prepend 4-byte big-endian length prefix
std::vector<uint8_t> frame_encode(const std::string& payload);

// Decode: extract messages from a byte buffer
// Returns number of bytes consumed, appends complete messages to `out`
size_t frame_decode(const uint8_t* data, size_t len, std::vector<std::string>& out);

static constexpr size_t kMaxPayloadSize = 65536;

} // namespace sample
