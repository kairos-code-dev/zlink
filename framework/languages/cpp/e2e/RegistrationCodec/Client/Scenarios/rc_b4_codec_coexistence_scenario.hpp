/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_codec_coexistence_scenario ()
{
    const auto reply = post_empty<codec_coexistence_scenario_res_t> (
      env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"), "/codec/coexistence");
    ensure (reply.json.value == "json:coexist-json", "RC-B4 JSON reply mismatch");
    ensure (reply.json.content_type == "application/json", "RC-B4 JSON content type mismatch");
    ensure (reply.custom.value == "custom:coexist-custom", "RC-B4 custom reply mismatch");
    ensure (reply.protobuf.value == "protobuf:coexist-protobuf",
            "RC-B4 Protobuf reply mismatch");
    ensure (reply.protobuf.content_type == "application/x-protobuf",
            "RC-B4 Protobuf content type mismatch");
    ensure (reply.messagepack.value == "messagepack:coexist-msgpack",
            "RC-B4 MessagePack reply mismatch");
    ensure (reply.messagepack.content_type == "application/x-msgpack",
            "RC-B4 MessagePack content type mismatch");
    std::cout << "scenario RC-B4 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
