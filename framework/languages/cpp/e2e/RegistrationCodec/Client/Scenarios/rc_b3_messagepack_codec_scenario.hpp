/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_messagepack_codec_scenario (zlink::framework::channel_client_t &channels,
                                            const std::string &http_endpoint)
{
    auto request =
      channels.request (api_channel, messagepack_roundtrip_t{.value = "b3"})
        .timeout (std::chrono::milliseconds (2000))
        .async<messagepack_roundtrip_reply_t> ();
    ensure (request.result ().has_value (), "RC-B3 request failed");
    ensure (request.result ().value ().value == "messagepack:b3", "RC-B3 reply mismatch");
    ensure (request.result ().value ().content_type == "application/x-msgpack",
            "RC-B3 content type mismatch");

    auto send =
      channels.send (api_channel, messagepack_codec_send_t{.value = "send-b3"})
        .timeout (std::chrono::milliseconds (2000))
        .async ();
    ensure (send.result ().has_value (), "RC-B3 send failed");
    ensure (evidence_contains (http_endpoint, "RC-B3-send", "application/x-msgpack:send-b3"),
            "RC-B3 send evidence mismatch");
    std::cout << "scenario RC-B3 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
