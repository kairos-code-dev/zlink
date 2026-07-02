/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_auto_registration_scenario ()
{
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto reply = post_empty<echo_auto_res_t> (http_endpoint, "/registration/auto");
    ensure (reply.value == "auto:a1", "RC-A1 reply mismatch");
    wait_evidence_contains (http_endpoint, "RC-A1-send", "send-a1", std::chrono::seconds (10));
    std::cout << "scenario RC-A1 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
