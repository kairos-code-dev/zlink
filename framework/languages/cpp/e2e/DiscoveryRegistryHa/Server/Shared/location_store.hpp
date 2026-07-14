/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>
#include <zlink/locations/redis.hpp>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

namespace zlink::framework::e2e::store_failure::server
{

inline int env_int (const char *name, int fallback)
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return std::stoi (value);
    }
    return fallback;
}

inline void add_redis_location_store (zlink::framework::zlink_framework_options_t &framework,
                                      const std::string &redis_endpoint,
                                      const std::string &redis_key_prefix)
{
    if (redis_endpoint.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_REDIS_ENDPOINT is required");
    }
    if (redis_key_prefix.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX is required");
    }
    auto redis_store = std::make_shared<zlink::framework::locations::redis::redis_location_store_t> (
      zlink::framework::locations::redis::redis_location_options_t{
        .connection_string = redis_endpoint, .key_prefix = redis_key_prefix});
    framework.add_location_store (std::move (redis_store));
    auto &locations = framework.configure_locations ();
    locations.heartbeat_interval =
      std::chrono::milliseconds (env_int ("ZLINK_CPP_SF_LOCATION_HEARTBEAT_MS", 1000));
    locations.owner_lease_ttl =
      std::chrono::milliseconds (env_int ("ZLINK_CPP_SF_LOCATION_LEASE_TTL_MS", 3000));
    locations.polling_interval =
      std::chrono::milliseconds (env_int ("ZLINK_CPP_SF_LOCATION_POLLING_MS", 500));
    locations.store_failure_grace =
      std::chrono::milliseconds (env_int ("ZLINK_CPP_SF_LOCATION_GRACE_MS", 6000));
}

} // namespace zlink::framework::e2e::store_failure::server
