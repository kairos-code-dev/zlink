#ifndef BENCH_WITH_ZLINK_MULTI_E2E_CLIENT_ONEWAY_PATTERN_HPP
#define BENCH_WITH_ZLINK_MULTI_E2E_CLIENT_ONEWAY_PATTERN_HPP

#include "../e2e_common.hpp"

#include <zlink.h>

#include <vector>

namespace bench_with_zlink_multi_e2e_pattern {

using namespace bench_with_zlink_multi_e2e;

inline bool recv_pubsub_message(void *socket, uint64_t &wire_send_ts)
{
    std::vector<unsigned char> payload(1024 * 1024, 0);
    const int rc = zlink_recv(socket, payload.data(), payload.size(), ZLINK_DONTWAIT);
    if (rc < 8)
        return false;
    wire_send_ts = load_u64_be(payload.data());
    return true;
}

} // namespace bench_with_zlink_multi_e2e_pattern

#endif
