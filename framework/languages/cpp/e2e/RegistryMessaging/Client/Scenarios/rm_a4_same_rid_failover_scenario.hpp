/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>
#include <thread>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_a4_same_rid_failover_scenario (const client_options_t &options)
{
    const auto provider_a_url = options.http_a_endpoint;
    const auto replacement_provider_url =
      options.http_a2_endpoint.empty () ? provider_a_url : options.http_a2_endpoint;
    const auto consumer_url = options.store_consumer_url;
    const auto replacement_api_endpoint = options.api_a2_endpoint;
    auto first = post_json<profile_req_t, profile_res_t> (
      consumer_url, "/profile/request", profile_req_t{.value = "failover-before"});
    ensure (first.provider_rid == "api-a" && first.instance_id == "api-a-v1",
            "RM-A4 initial provider mismatch");
    const auto v1_evidence = fetch_evidence (provider_a_url);
    ensure (evidence_value_prefix_count (v1_evidence, "ProfileReq", "failover-before") == 1,
            "RM-A4 initial provider evidence mismatch");

    touch_file (options.ready_file);
    wait_for_file (options.continue_file, options.control_wait);

    auto location_client = zlink::http_client::client_t::create ()
                             .base_url (consumer_url)
                             .timeout (std::chrono::milliseconds (1000))
                             .build ();
    bool replacement_row_ready = false;
    const auto row_deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
    do {
        const auto rows = location_client.get ("/locations/peers").fetch<nlohmann::json> ();
        int matching_rows = 0;
        bool replacement_endpoint_found = false;
        for (const auto &row : rows) {
            if (row.value ("mesh_name", "") == api_channel
                && row.value ("role", "") == "router"
                && row.value ("node_rid", "") == "api-a") {
                ++matching_rows;
                replacement_endpoint_found =
                  replacement_endpoint_found
                  || row.value ("endpoint", "") == replacement_api_endpoint;
            }
        }
        replacement_row_ready = matching_rows == 1 && replacement_endpoint_found;
        if (!replacement_row_ready) {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
    } while (!replacement_row_ready && std::chrono::steady_clock::now () < row_deadline);
    ensure (replacement_row_ready,
            "RM-A4 peer location did not converge to the replacement endpoint");

    for (int index = 0; index < 20; ++index) {
        auto reply = post_json<profile_req_t, profile_res_t> (
          consumer_url, "/profile/request",
          profile_req_t{.value = "failover-after-" + std::to_string (index)});
        ensure (reply.provider_rid == "api-a" && reply.instance_id == "api-a-v2",
                "RM-A4 did not switch to replacement provider");
    }
    const auto v2_evidence = fetch_evidence (replacement_provider_url);
    ensure (evidence_value_prefix_count (v2_evidence, "ProfileReq", "failover-after-") == 20,
            "RM-A4 replacement provider evidence count mismatch");
    std::cout << "scenario RM-A4 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
