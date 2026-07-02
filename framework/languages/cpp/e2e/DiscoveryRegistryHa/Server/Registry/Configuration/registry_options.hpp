/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace zlink::framework::e2e::discovery_registry_ha::registry
{

inline std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

struct registry_options_t
{
    std::string rid;
    std::string pub_endpoint;
    std::string router_endpoint;
    std::string http_endpoint;
    std::string log_dir;
    std::vector<std::string> peer_pub_endpoints;
};

inline registry_options_t read_registry_options ()
{
    registry_options_t options{.rid = env_or ("ZLINK_CPP_DRHA_RID", "reg-1"),
                               .pub_endpoint = env_or ("ZLINK_CPP_DRHA_REGISTRY_PUB"),
                               .router_endpoint = env_or ("ZLINK_CPP_DRHA_REGISTRY_ROUTER"),
                               .http_endpoint = env_or ("ZLINK_CPP_DRHA_HTTP_ENDPOINT"),
                               .log_dir = env_or ("ZLINK_CPP_DRHA_LOG_DIR", "logs")};
    for (int index = 1;; ++index) {
        auto peer = env_or (("ZLINK_CPP_DRHA_REGISTRY_PEER_" + std::to_string (index)).c_str ());
        if (peer.empty ()) {
            break;
        }
        options.peer_pub_endpoints.push_back (std::move (peer));
    }
    return options;
}

} // namespace zlink::framework::e2e::discovery_registry_ha::registry
