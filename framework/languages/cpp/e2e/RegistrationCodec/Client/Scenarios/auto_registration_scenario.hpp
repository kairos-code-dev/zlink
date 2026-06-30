/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registration_codec::client
{

inline void run_auto_registration_scenario (zlink::framework::channel_client_t &channels)
{
    auto request =
      channels.request (api_channel, echo_auto_req_t{.value = "a1"})
        .timeout (std::chrono::milliseconds (2000))
        .async<echo_auto_res_t> ();
    ensure (request.result ().has_value (), "RC-A1 request failed");
    ensure (request.result ().value ().value == "auto:a1", "RC-A1 reply mismatch");

    auto send = channels.send (api_channel, echo_auto_msg_t{.value = "send-a1"})
                  .timeout (std::chrono::milliseconds (2000))
                  .async ();
    ensure (send.result ().has_value (), "RC-A1 send failed");
    std::cout << "scenario RC-A1 passed\n";
}

} // namespace zlink::framework::e2e::registration_codec::client
