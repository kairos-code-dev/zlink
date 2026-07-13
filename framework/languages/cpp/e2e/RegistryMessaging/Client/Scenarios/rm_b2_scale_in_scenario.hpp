/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <set>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_b2_scale_in_scenario ()
{
    const auto provider_a_url = env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT");
    auto location_client = zlink::http_client::client_t::create ()
                             .base_url (provider_a_url)
                             .timeout (std::chrono::milliseconds (1000))
                             .build ();
    std::set<std::string> before;
    for (int index = 0; index < 80 && before.size () < 2; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          provider_a_url, "/profile/request",
          profile_req_t{.value = "scale-in-before-" + std::to_string (index)});
        before.insert (reply.provider_rid);
    }
    ensure (before.contains ("api-a") && before.contains ("api-b"),
            "RM-B2 did not start with both providers");

    touch_file (env_or ("ZLINK_CPP_E2E_READY_FILE"));
    wait_for_file (env_or ("ZLINK_CPP_E2E_CONTINUE_FILE"));

    bool api_b_removed = false;
    for (int attempt = 0; attempt < 150 && !api_b_removed; ++attempt) {
        const auto peers = location_client.get ("/locations/peers").fetch<nlohmann::json> ();
        api_b_removed = true;
        for (const auto &entry : peers) {
            if (entry.value ("mesh_name", "") == api_channel
                && entry.value ("role", "") == "router"
                && entry.value ("node_rid", "") == "api-b") {
                api_b_removed = false;
                break;
            }
        }
        if (!api_b_removed) {
            std::this_thread::sleep_for (std::chrono::milliseconds (200));
        }
    }
    ensure (api_b_removed, "RM-B2 timed out waiting for api-b location row removal");

    int settled = 0;
    for (int attempt = 0; attempt < 40 && settled < 3; ++attempt) {
        try {
            auto reply = post_json<profile_req_t, profile_res_t> (
              provider_a_url, "/profile/request",
              profile_req_t{.value = "scale-in-settle-" + std::to_string (attempt)},
              std::chrono::milliseconds (1500));
            settled = reply.provider_rid == "api-a" ? settled + 1 : 0;
        }
        catch (...) {
            settled = 0;
            std::this_thread::sleep_for (std::chrono::milliseconds (200));
        }
    }
    ensure (settled >= 3, "RM-B2 traffic did not settle on the surviving provider");

    for (int index = 0; index < 20; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          provider_a_url, "/profile/request",
          profile_req_t{.value = "scale-in-after-" + std::to_string (index)},
          std::chrono::seconds (5));
        ensure (reply.provider_rid == "api-a",
                "RM-B2 routed to removed provider after scale-in");
    }
    std::cout << "scenario RM-B2 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
