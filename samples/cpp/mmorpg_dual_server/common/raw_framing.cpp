#include "common/raw_framing.hpp"
#include <cstring>
#include <arpa/inet.h>

namespace sample {

std::vector<uint8_t> frame_encode(const std::string& payload) {
    std::vector<uint8_t> buf(4 + payload.size());
    uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::memcpy(buf.data(), &len, 4);
    std::memcpy(buf.data() + 4, payload.data(), payload.size());
    return buf;
}

size_t frame_decode(const uint8_t* data, size_t len, std::vector<std::string>& out) {
    size_t consumed = 0;
    while (len - consumed >= 4) {
        uint32_t payload_len;
        std::memcpy(&payload_len, data + consumed, 4);
        payload_len = ntohl(payload_len);
        if (payload_len > kMaxPayloadSize) break;
        if (len - consumed < 4 + payload_len) break;
        out.emplace_back(reinterpret_cast<const char*>(data + consumed + 4), payload_len);
        consumed += 4 + payload_len;
    }
    return consumed;
}

} // namespace sample
