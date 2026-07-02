/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_codec_mismatch_scenario ()
{
    const auto status =
      post_empty<operation_status_t> (env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT"), "/codec/mismatch");
    ensure (status.status == "passed", "RC-B5 mismatch endpoint failed");
    std::cout << "scenario RC-B5 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
