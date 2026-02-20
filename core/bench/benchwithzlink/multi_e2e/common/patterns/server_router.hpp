#ifndef BENCH_WITH_ZLINK_MULTI_E2E_SERVER_ROUTER_PATTERN_HPP
#define BENCH_WITH_ZLINK_MULTI_E2E_SERVER_ROUTER_PATTERN_HPP

#include <zlink.h>

#include <vector>

namespace bench_with_zlink_multi_e2e_pattern {

inline bool handle_router_once(void *server)
{
    std::vector<char> id(512, 0);
    std::vector<char> payload(1024 * 1024, 0);

    const int id_len = zlink_recv(server, id.data(), id.size(), ZLINK_DONTWAIT);
    if (id_len < 0)
        return false;
    const int rc = zlink_recv(server, payload.data(), payload.size(), 0);
    if (rc < 0)
        return false;

    if (zlink_send(server, id.data(), id_len, ZLINK_SNDMORE) < 0)
        return false;
    return zlink_send(server, payload.data(), rc, 0) >= 0;
}

inline bool handle_router_poll_once(void *server)
{
    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
    const int prc = zlink_poll(item, 1, 0);
    if (prc <= 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return false;

    std::vector<char> id(512, 0);
    std::vector<char> payload(1024 * 1024, 0);

    const int id_len = zlink_recv(server, id.data(), id.size(), ZLINK_DONTWAIT);
    if (id_len < 0)
        return false;
    const int rc = zlink_recv(server, payload.data(), payload.size(), ZLINK_DONTWAIT);
    if (rc < 0)
        return false;
    if (zlink_send(server, id.data(), id_len, ZLINK_SNDMORE) < 0)
        return false;
    return zlink_send(server, payload.data(), rc, 0) >= 0;
}

inline bool handle_dealer_once(void *server)
{
    std::vector<char> payload(1024 * 1024, 0);
    const int rc = zlink_recv(server, payload.data(), payload.size(), ZLINK_DONTWAIT);
    if (rc < 0)
        return false;
    return zlink_send(server, payload.data(), rc, 0) >= 0;
}

} // namespace bench_with_zlink_multi_e2e_pattern

#endif
