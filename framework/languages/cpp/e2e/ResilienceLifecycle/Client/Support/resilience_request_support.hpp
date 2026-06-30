/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "client_support.hpp"

#include <chrono>
#include <string>

namespace zlink::framework::e2e::registry_messaging::client
{

inline profile_res_t request_profile (zlink::framework::channel_client_t &channels,
                                        const std::string &channel,
                                        const std::string &value,
                                        std::chrono::milliseconds timeout =
                                          std::chrono::milliseconds (2000))
{
    auto task = channels.request (channel, profile_req_t{.value = value})
                  .timeout (timeout)
                  .async<profile_res_t> ();
    ensure (task.result ().has_value (), "profile request failed: " + value);
    return task.result ().value ();
}

} // namespace zlink::framework::e2e::registry_messaging::client
