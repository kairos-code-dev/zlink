/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_a1_discovery_request_scenario (zlink::framework::channel_client_t &channels)
{
    std::set<std::string> providers;
    for (int index = 0; index < 80 && providers.size () < 2; ++index) {
        auto task = channels.request (api_channel,
                                      profile_request_t{.value = "auto-" + std::to_string (index)})
                      .timeout (std::chrono::milliseconds (2000))
                      .async<profile_reply_t> ();
        const auto &result = task.result ();
        ensure (result.has_value (),
                "RM-A1 request failed: "
                  + std::string (result.error () ? result.error ()->what () : "unknown"));
        ensure (result.value ().value.rfind ("profile:auto-", 0) == 0,
                "RM-A1 reply payload mismatch");
        providers.insert (result.value ().provider_rid);
    }
    ensure (providers.contains ("api-a") && providers.contains ("api-b"),
            "RM-A1 did not reach both providers");

    auto registry_client = zlink::http_client::client_t::create ()
                             .base_url (env_or ("ZLINK_CPP_E2E_HTTP_REGISTRY_ENDPOINT"))
                             .json ()
                             .timeout (std::chrono::milliseconds (1000))
                             .build ();
    int ready_providers = 0;
    for (int attempt = 0; attempt < 40 && ready_providers < 2; ++attempt) {
        ready_providers = 0;
        const auto topology = registry_client.get ("/registry/topology").fetch<nlohmann::json> ();
        for (const auto &entry : topology) {
            if (entry.value ("name", "") == api_channel && entry.value ("kind", "") == "channel"
                && entry.value ("role", "") == "server"
                && entry.value ("state", "") == "active") {
                ++ready_providers;
            }
        }
        if (ready_providers < 2) {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
    }
    ensure (ready_providers >= 2, "RM-A1 expected two ready api providers in registry topology");
    std::cout << "scenario RM-A1 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
