/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_codec_mismatch_scenario (zlink::framework::channel_client_t &channels)
{
    auto request =
      channels.request (api_channel, protobuf_roundtrip_req_t{.value = "json-only"})
        .timeout (std::chrono::milliseconds (2000))
        .async<protobuf_roundtrip_res_t> ();
    ensure (request.result ().has_value (), "RC-B5 Protobuf-to-JSON-only request failed");
    ensure (request.result ().value ().value == "protobuf:json-only",
            "RC-B5 fallback reply mismatch");
    ensure (request.result ().value ().content_type == "application/x-protobuf",
            "RC-B5 inbound content type mismatch");

    auto json =
      channels.request (api_channel, json_roundtrip_req_t{.value = "after-mismatch"})
        .timeout (std::chrono::milliseconds (2000))
        .async<json_roundtrip_res_t> ();
    ensure (json.result ().has_value (), "RC-B5 recovery JSON request failed");
    ensure (json.result ().value ().value == "json:after-mismatch",
            "RC-B5 recovery reply mismatch");
    std::cout << "scenario RC-B5 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
