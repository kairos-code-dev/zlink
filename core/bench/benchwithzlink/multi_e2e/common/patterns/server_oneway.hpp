#ifndef BENCH_WITH_ZLINK_MULTI_E2E_SERVER_ONEWAY_PATTERN_HPP
#define BENCH_WITH_ZLINK_MULTI_E2E_SERVER_ONEWAY_PATTERN_HPP

#include "../e2e_common.hpp"

#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <vector>

namespace bench_with_zlink_multi_e2e_pattern {

using namespace bench_with_zlink_multi_e2e;

inline void run_pub_server(void *server,
                           size_t msg_size,
                           const std::atomic<bool> &stop_flag)
{
    std::vector<unsigned char> payload(std::max<size_t>(16, msg_size), 0xA5);
    while (!stop_flag.load(std::memory_order_acquire)) {
        const uint64_t now = now_ns();
        store_u64_be(payload.data(), now);
        const int rc = zlink_send(server, payload.data(), payload.size(), ZLINK_DONTWAIT);
        if (rc < 0 && zlink_errno() == EAGAIN)
            continue;
    }
}

} // namespace bench_with_zlink_multi_e2e_pattern

#endif
