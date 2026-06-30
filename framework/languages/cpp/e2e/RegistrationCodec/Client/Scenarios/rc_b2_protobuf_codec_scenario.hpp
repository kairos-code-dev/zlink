/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_protobuf_codec_scenario (zlink::framework::channel_client_t &channels,
                                         const std::string &http_endpoint)
{
    auto request =
      channels.request (api_channel, protobuf_roundtrip_req_t{.value = "b2"})
        .timeout (std::chrono::milliseconds (2000))
        .async<protobuf_roundtrip_res_t> ();
    ensure (request.result ().has_value (), "RC-B2 request failed");
    ensure (request.result ().value ().value == "protobuf:b2", "RC-B2 reply mismatch");
    ensure (request.result ().value ().content_type == "application/x-protobuf",
            "RC-B2 content type mismatch");

    auto send =
      channels.send (api_channel, protobuf_codec_msg_t{.value = "send-b2"})
        .timeout (std::chrono::milliseconds (2000))
        .async ();
    ensure (send.result ().has_value (), "RC-B2 send failed");
    ensure (evidence_contains (http_endpoint, "RC-B2-send", "application/x-protobuf:send-b2"),
            "RC-B2 send evidence mismatch");
    std::cout << "scenario RC-B2 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
