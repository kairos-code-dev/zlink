/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/discovery_registry_ha_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <thread>

namespace zlink::framework::e2e::discovery_registry_ha::consumer
{

class profile_request_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit profile_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        auto call = _channels.request (api_channel, request)
                      .timeout (std::chrono::milliseconds (3000))
                      .async<profile_res_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return reply.value ();
        }
        throw std::runtime_error (reply.error () ? reply.error ()->what ()
                                                 : "profile request failed");
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

class profile_request_wait_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit profile_request_wait_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (10);
        std::string last_error = "profile request failed";
        while (std::chrono::steady_clock::now () < deadline) {
            auto call = _channels.request (api_channel, request)
                          .timeout (std::chrono::milliseconds (3000))
                          .async<profile_res_t> ();
            const auto &reply = call.result ();
            if (reply) {
                return reply.value ();
            }
            if (reply.error ()) {
                last_error = reply.error ()->what ();
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        throw std::runtime_error ("timed out waiting for profile request routing: " + last_error);
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

} // namespace zlink::framework::e2e::discovery_registry_ha::consumer
