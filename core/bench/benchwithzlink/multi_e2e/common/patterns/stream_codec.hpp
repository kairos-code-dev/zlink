#ifndef BENCH_WITH_ZLINK_MULTI_E2E_STREAM_CODEC_HPP
#define BENCH_WITH_ZLINK_MULTI_E2E_STREAM_CODEC_HPP

#include "../e2e_common.hpp"

#include <cstring>
#include <vector>

namespace bench_with_zlink_multi_e2e_pattern {

using namespace bench_with_zlink_multi_e2e;

static const size_t k_max_stream_frame_size = 16u * 1024u * 1024u;

struct stream_buffer_t {
    std::vector<char> data;
    size_t offset;

    stream_buffer_t() : data(), offset(0) {}

    void append(const char *src, size_t len)
    {
        if (len == 0)
            return;
        data.insert(data.end(), src, src + len);
    }

    size_t available() const { return data.size() - offset; }

    void compact()
    {
        if (offset == 0)
            return;
        if (offset >= data.size()) {
            data.clear();
            offset = 0;
            return;
        }
        if (offset > 4096) {
            data.erase(data.begin(), data.begin() + static_cast<long>(offset));
            offset = 0;
        }
    }
};

inline bool stream_is_event_payload(const char *data, size_t len)
{
    if (len != 1 || !data)
        return false;
    const unsigned char v = static_cast<unsigned char>(data[0]);
    return v == 0x00u || v == 0x01u;
}

inline bool read_one_stream_frame(stream_buffer_t &stash, std::vector<char> &body)
{
    if (stash.available() < 4)
        return false;

    const unsigned char *prefix =
      reinterpret_cast<const unsigned char *>(&stash.data[stash.offset]);
    const uint32_t frame_len = load_u32_be(prefix);
    if (frame_len > k_max_stream_frame_size) {
        stash.data.clear();
        stash.offset = 0;
        return false;
    }
    const size_t required = 4u + static_cast<size_t>(frame_len);
    if (stash.available() < required)
        return false;

    body.assign(frame_len, 0);
    if (frame_len > 0)
        std::memcpy(body.data(), &stash.data[stash.offset + 4], frame_len);

    stash.offset += required;
    stash.compact();
    return true;
}

} // namespace bench_with_zlink_multi_e2e_pattern

#endif
