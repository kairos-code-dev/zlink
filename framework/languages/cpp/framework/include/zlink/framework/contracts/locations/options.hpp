/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <chrono>

namespace zlink::framework
{

struct location_options_t
{
    std::chrono::milliseconds heartbeat_interval{5000};
    std::chrono::milliseconds owner_lease_ttl{15000};
    std::chrono::milliseconds polling_interval{1000};
    int list_page_size = 1000;
    std::chrono::milliseconds store_failure_grace{30000};
};

} // namespace zlink::framework
