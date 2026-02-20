#ifndef BENCH_WITH_ZLINK_MULTI_E2E_SERVER_STREAM_PATTERN_HPP
#define BENCH_WITH_ZLINK_MULTI_E2E_SERVER_STREAM_PATTERN_HPP

#include "stream_codec.hpp"

#include <zlink.h>

#include <map>
#include <string>
#include <vector>

namespace bench_with_zlink_multi_e2e_pattern {

inline bool send_stream_reply(void *server,
                              const std::string &routing_id,
                              const std::vector<char> &body)
{
    if (routing_id.empty())
        return false;

    std::vector<unsigned char> framed(4 + body.size(), 0);
    store_u32_be(framed.data(), static_cast<uint32_t>(body.size()));
    if (!body.empty())
        std::memcpy(framed.data() + 4, body.data(), body.size());

    if (zlink_send(server, routing_id.data(), routing_id.size(), ZLINK_SNDMORE) < 0)
        return false;
    return zlink_send(server, framed.data(), framed.size(), 0) >= 0;
}

inline bool handle_stream_once(void *server,
                               std::map<std::string, stream_buffer_t> &stashes)
{
    zlink_msg_t id_msg;
    zlink_msg_t payload_msg;
    zlink_msg_init(&id_msg);
    zlink_msg_init(&payload_msg);

    const int id_rc = zlink_msg_recv(&id_msg, server, ZLINK_DONTWAIT);
    if (id_rc < 0) {
        zlink_msg_close(&id_msg);
        zlink_msg_close(&payload_msg);
        return false;
    }

    const int payload_rc = zlink_msg_recv(&payload_msg, server, 0);
    if (payload_rc < 0) {
        zlink_msg_close(&id_msg);
        zlink_msg_close(&payload_msg);
        return false;
    }

    const char *id_data = static_cast<const char *>(zlink_msg_data(&id_msg));
    const size_t id_size = zlink_msg_size(&id_msg);
    const char *payload = static_cast<const char *>(zlink_msg_data(&payload_msg));
    const size_t payload_size = zlink_msg_size(&payload_msg);

    bool handled = false;
    if (id_data && id_size > 0 && payload && payload_size > 0
        && !stream_is_event_payload(payload, payload_size)) {
        std::string routing_id(id_data, id_size);
        stream_buffer_t &stash = stashes[routing_id];
        stash.append(payload, payload_size);

        std::vector<char> body;
        while (read_one_stream_frame(stash, body)) {
            (void) send_stream_reply(server, routing_id, body);
            handled = true;
        }
    }

    zlink_msg_close(&id_msg);
    zlink_msg_close(&payload_msg);
    return handled;
}

} // namespace bench_with_zlink_multi_e2e_pattern

#endif
