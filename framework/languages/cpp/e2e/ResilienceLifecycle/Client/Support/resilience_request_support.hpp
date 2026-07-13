/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "client_support.hpp"

#include <chrono>
#include <string>

namespace zlink::framework::e2e::resilience_lifecycle::client
{

inline profile_res_t request_profile (const std::string &value,
                                      const std::string &marker,
                                      std::chrono::milliseconds timeout =
                                        std::chrono::milliseconds (3000))
{
    return post_consumer_profile (value, marker, "/profile/request", timeout);
}

inline profile_res_t request_profile (const std::string &value,
                                      std::chrono::milliseconds timeout =
                                        std::chrono::milliseconds (3000))
{
    return request_profile (value, value, timeout);
}

} // namespace zlink::framework::e2e::resilience_lifecycle::client
