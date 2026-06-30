/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_codec_coexistence_scenario (zlink::framework::channel_client_t &channels)
{
    auto json =
      channels.request (api_channel, json_roundtrip_req_t{.value = "coexist-json"})
        .timeout (std::chrono::milliseconds (2000))
        .async<json_roundtrip_res_t> ();
    ensure (json.result ().has_value (), "RC-B4 JSON request failed");
    ensure (json.result ().value ().value == "json:coexist-json",
            "RC-B4 JSON reply mismatch");
    ensure (json.result ().value ().content_type == "application/json",
            "RC-B4 JSON content type mismatch");

    auto custom =
      channels.request (api_channel, custom_roundtrip_req_t{.value = "coexist-custom"})
        .timeout (std::chrono::milliseconds (2000))
        .async<custom_roundtrip_res_t> ();
    ensure (custom.result ().has_value (), "RC-B4 custom request failed");
    ensure (custom.result ().value ().value == "custom:coexist-custom",
            "RC-B4 custom reply mismatch");

    auto protobuf =
      channels
        .request (api_channel, protobuf_roundtrip_req_t{.value = "coexist-protobuf"})
        .timeout (std::chrono::milliseconds (2000))
        .async<protobuf_roundtrip_res_t> ();
    ensure (protobuf.result ().has_value (), "RC-B4 Protobuf request failed");
    ensure (protobuf.result ().value ().value == "protobuf:coexist-protobuf",
            "RC-B4 Protobuf reply mismatch");
    ensure (protobuf.result ().value ().content_type == "application/x-protobuf",
            "RC-B4 Protobuf content type mismatch");

    auto messagepack =
      channels
        .request (api_channel, messagepack_roundtrip_req_t{.value = "coexist-msgpack"})
        .timeout (std::chrono::milliseconds (2000))
        .async<messagepack_roundtrip_res_t> ();
    ensure (messagepack.result ().has_value (), "RC-B4 MessagePack request failed");
    ensure (messagepack.result ().value ().value == "messagepack:coexist-msgpack",
            "RC-B4 MessagePack reply mismatch");
    ensure (messagepack.result ().value ().content_type == "application/x-msgpack",
            "RC-B4 MessagePack content type mismatch");
    std::cout << "scenario RC-B4 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
