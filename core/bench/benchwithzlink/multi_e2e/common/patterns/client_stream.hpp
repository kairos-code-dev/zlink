#ifndef BENCH_WITH_ZLINK_MULTI_E2E_CLIENT_STREAM_PATTERN_HPP
#define BENCH_WITH_ZLINK_MULTI_E2E_CLIENT_STREAM_PATTERN_HPP

#include "stream_codec.hpp"

#include <zlink.h>

#include <cerrno>
#include <string>
#include <thread>

namespace bench_with_zlink_multi_e2e_pattern {

using namespace bench_with_zlink_multi_e2e;

inline bool expect_stream_connect_id(void *socket, std::string &routing_id)
{
    const int timeout_ms = static_cast<int>(
      parse_long_env("BENCH_MULTI_CONNECT_READY_TIMEOUT_MS", 5000, 1));
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        zlink_msg_t id_msg;
        zlink_msg_t payload_msg;
        zlink_msg_init(&id_msg);
        zlink_msg_init(&payload_msg);

        const int id_rc = zlink_msg_recv(&id_msg, socket, ZLINK_DONTWAIT);
        if (id_rc < 0) {
            zlink_msg_close(&id_msg);
            zlink_msg_close(&payload_msg);
            if (zlink_errno() == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false;
        }

        const int payload_rc = zlink_msg_recv(&payload_msg, socket, 0);
        if (payload_rc < 0) {
            zlink_msg_close(&id_msg);
            zlink_msg_close(&payload_msg);
            return false;
        }

        const char *id_data = static_cast<const char *>(zlink_msg_data(&id_msg));
        const size_t id_size = zlink_msg_size(&id_msg);
        if (id_data && id_size > 0) {
            routing_id.assign(id_data, id_size);
            zlink_msg_close(&id_msg);
            zlink_msg_close(&payload_msg);
            return true;
        }

        zlink_msg_close(&id_msg);
        zlink_msg_close(&payload_msg);
    }

    return false;
}

inline bool send_stream_rtt_message(void *socket,
                                    const std::string &stream_routing_id,
                                    const std::vector<unsigned char> &payload)
{
    if (stream_routing_id.empty())
        return false;

    std::vector<unsigned char> framed(4 + payload.size(), 0);
    store_u32_be(framed.data(), static_cast<uint32_t>(payload.size()));
    std::memcpy(framed.data() + 4, payload.data(), payload.size());

    if (zlink_send(socket, stream_routing_id.data(), stream_routing_id.size(),
                   ZLINK_SNDMORE | ZLINK_DONTWAIT)
        < 0) {
        return false;
    }
    return zlink_send(socket, framed.data(), framed.size(), ZLINK_DONTWAIT) >= 0;
}

inline bool recv_stream_body(void *socket,
                             stream_buffer_t &stash,
                             std::vector<char> &body)
{
    zlink_msg_t id_msg;
    zlink_msg_t payload_msg;
    zlink_msg_init(&id_msg);
    zlink_msg_init(&payload_msg);

    const int id_rc = zlink_msg_recv(&id_msg, socket, ZLINK_DONTWAIT);
    if (id_rc < 0) {
        zlink_msg_close(&id_msg);
        zlink_msg_close(&payload_msg);
        return false;
    }

    const int payload_rc = zlink_msg_recv(&payload_msg, socket, ZLINK_DONTWAIT);
    if (payload_rc < 0) {
        zlink_msg_close(&id_msg);
        zlink_msg_close(&payload_msg);
        return false;
    }

    const char *payload_data = static_cast<const char *>(zlink_msg_data(&payload_msg));
    const size_t payload_size = zlink_msg_size(&payload_msg);
    if (payload_data && payload_size > 0
        && !stream_is_event_payload(payload_data, payload_size)) {
        stash.append(payload_data, payload_size);
    }

    zlink_msg_close(&id_msg);
    zlink_msg_close(&payload_msg);

    return read_one_stream_frame(stash, body);
}

inline bool recv_stream_rtt_message(void *socket,
                                    stream_buffer_t &stream_stash,
                                    uint64_t &wire_send_ts)
{
    std::vector<char> body;
    if (!recv_stream_body(socket, stream_stash, body))
        return false;
    if (body.size() < 16)
        return false;
    wire_send_ts =
      load_u64_be(reinterpret_cast<const unsigned char *>(body.data()));
    return true;
}

} // namespace bench_with_zlink_multi_e2e_pattern

#endif
