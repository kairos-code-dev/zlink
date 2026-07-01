/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_json_codec_scenario (zlink::framework::channel_client_t &channels,
                                     const std::string &http_endpoint)
{
    auto request =
      channels.request (api_channel, json_roundtrip_req_t{.value = "b1"})
        .timeout (std::chrono::milliseconds (2000))
        .async<json_roundtrip_res_t> ();
    ensure (request.result ().has_value (), "RC-B1 request failed");
    ensure (request.result ().value ().value == "json:b1", "RC-B1 reply mismatch");
    ensure (request.result ().value ().content_type == "application/json",
            "RC-B1 content type mismatch");

    channels.send (api_channel, json_codec_msg_t{.value = "send-b1"})
      .timeout (std::chrono::milliseconds (2000))
      .submit ();
    ensure (evidence_contains (http_endpoint, "RC-B1-send", "application/json:send-b1"),
            "RC-B1 send evidence mismatch");
    std::cout << "scenario RC-B1 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
