#ifndef BENCH_WITH_ZLINK_MULTI_E2E_CLIENT_ROUTER_PATTERN_HPP
#define BENCH_WITH_ZLINK_MULTI_E2E_CLIENT_ROUTER_PATTERN_HPP

#include "../e2e_common.hpp"

#include <zlink.h>

#include <cstring>
#include <vector>

namespace bench_with_zlink_multi_e2e_pattern {

using namespace bench_with_zlink_multi_e2e;

inline bool send_router_rtt_message(void *socket,
                                    const char *server_routing_id,
                                    const std::vector<unsigned char> &payload)
{
    if (zlink_send(socket, server_routing_id, std::strlen(server_routing_id),
                   ZLINK_SNDMORE | ZLINK_DONTWAIT)
        < 0) {
        return false;
    }
    return zlink_send(socket, payload.data(), payload.size(), ZLINK_DONTWAIT) >= 0;
}

inline bool recv_router_rtt_message(void *socket, uint64_t &wire_send_ts)
{
    std::vector<unsigned char> payload(1024 * 1024, 0);
    std::vector<char> id(512, 0);
    const int id_len = zlink_recv(socket, id.data(), id.size(), ZLINK_DONTWAIT);
    if (id_len < 0)
        return false;
    const int rc = zlink_recv(socket, payload.data(), payload.size(), ZLINK_DONTWAIT);
    if (rc < 16)
        return false;
    wire_send_ts = load_u64_be(payload.data());
    return true;
}

inline bool recv_router_poll_rtt_message(void *socket, uint64_t &wire_send_ts)
{
    zlink_pollitem_t item[] = {{socket, 0, ZLINK_POLLIN, 0}};
    const int prc = zlink_poll(item, 1, 0);
    if (prc <= 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return false;
    return recv_router_rtt_message(socket, wire_send_ts);
}

} // namespace bench_with_zlink_multi_e2e_pattern

#endif
