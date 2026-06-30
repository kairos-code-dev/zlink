/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_di_lifecycle_scenario (zlink::framework::channel_client_t &channels)
{
    auto first =
      channels.request (api_channel, scoped_lifecycle_req_t{.value = "first"})
        .timeout (std::chrono::milliseconds (2000))
        .async<scoped_lifecycle_res_t> ();
    ensure (first.result ().has_value (), "RC-A4 first request failed");

    auto second =
      channels.request (api_channel, scoped_lifecycle_req_t{.value = "second"})
        .timeout (std::chrono::milliseconds (2000))
        .async<scoped_lifecycle_res_t> ();
    ensure (second.result ().has_value (), "RC-A4 second request failed");

    const auto first_reply = first.result ().value ();
    const auto second_reply = second.result ().value ();
    ensure (first_reply.scoped_id != second_reply.scoped_id,
            "RC-A4 scoped dependency was reused");
    ensure (first_reply.singleton_id == second_reply.singleton_id,
            "RC-A4 singleton dependency was recreated");
    ensure (second_reply.destroyed_before >= first_reply.destroyed_before + 1,
            "RC-A4 scoped dependency was not destroyed after request");

    auto stats =
      channels.request (api_channel, scoped_lifecycle_stats_req_t{.value = "stats"})
        .timeout (std::chrono::milliseconds (2000))
        .async<scoped_lifecycle_stats_res_t> ();
    ensure (stats.result ().has_value (), "RC-A4 stats request failed");
    ensure (stats.result ().value ().destroyed_count >= 2,
            "RC-A4 scoped dependency destruction count mismatch");
    std::cout << "scenario RC-A4 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
