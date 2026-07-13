/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <chrono>
#include <map>
#include <string>

namespace zlink::framework
{

struct location_options_t
{
    std::chrono::milliseconds heartbeat_interval{5000};
    std::chrono::milliseconds owner_lease_ttl{15000};
    std::chrono::milliseconds polling_interval{1000};
    int list_page_size = 1000;
    std::chrono::milliseconds store_failure_grace{30000};
    /* Spot mesh name to route mesh channel name, used when the two differ.
     * An unmapped mesh name is used as the route channel name as-is. */
    std::map<std::string, std::string> spot_router_channels;
};

} // namespace zlink::framework
