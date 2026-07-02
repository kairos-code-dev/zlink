/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/scenario_state.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace zlink::framework::e2e::registry_messaging::workflow
{

inline workflow_res_t request_workflow_with_retry (zlink::framework::channel_client_t &channels,
                                                   const workflow_req_t &request)
{
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (30);
    std::string last_error = "workflow request failed";
    while (std::chrono::steady_clock::now () < deadline) {
        auto call = channels.request (workflow_channel, request)
                      .timeout (std::chrono::seconds (5))
                      .async<workflow_res_t> ();
        const auto &reply = call.result ();
        if (reply) {
            return reply.value ();
        }
        if (reply.error ()) {
            last_error = reply.error ()->what ();
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    throw std::runtime_error ("timed out waiting for workflow request channel route: "
                              + last_error);
}

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit evidence_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_state.snapshot ()).dump ();
        return response;
    }

  private:
    scenario_state_t &_state;
};

class http_workflow_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::channel_client_t>;
    using request_type = workflow_req_t;
    using reply_type = workflow_res_t;

    explicit http_workflow_request_handler_t (zlink::framework::channel_client_t &channels) :
        _channels (channels)
    {
    }

    workflow_res_t handle (const workflow_req_t &request)
    {
        return request_workflow_with_retry (_channels, request);
    }

  private:
    zlink::framework::channel_client_t &_channels;
};

} // namespace zlink::framework::e2e::registry_messaging::workflow
