/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_attribute_registration_scenario ()
{
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto reply = post_empty<echo_attr_res_t> (http_endpoint, "/registration/attribute");
    ensure (reply.value == "attr:a2", "RC-A2 reply mismatch");
    ensure (reply.packet_name == "EchoAttr", "RC-A2 packet name mismatch");
    ensure (reply.content_type == "application/json", "RC-A2 content type mismatch");
    wait_evidence_contains (http_endpoint, "RC-A2-request", "EchoAttr:a2",
                            std::chrono::seconds (10));
    wait_evidence_contains (http_endpoint, "RC-A2-send", "EchoAttrMsg:send-a2",
                            std::chrono::seconds (10));
    std::cout << "scenario RC-A2 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
