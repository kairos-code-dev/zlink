/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/scenario_state.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <thread>

namespace zlink::framework::e2e::registry_messaging::provider
{

class profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using request_type = profile_request_t;
    using reply_type = profile_reply_t;

    explicit profile_request_handler_t (scenario_state_t &state) : _state (state) {}

    profile_reply_t handle (const profile_request_t &request)
    {
        if (request.value == "slow") {
            std::this_thread::sleep_for (std::chrono::seconds (1));
        } else if (request.value == "very-slow") {
            std::this_thread::sleep_for (std::chrono::seconds (10));
        }
        _state.record ("ProfileRequest", request.value);
        return {.value = "profile:" + request.value,
                .provider_rid = _state.provider_rid,
                .instance_id = _state.instance_id};
    }

  private:
    scenario_state_t &_state;
};

class profile_command_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using message_type = profile_command_t;

    explicit profile_command_handler_t (scenario_state_t &state) : _state (state) {}

    void handle (const profile_command_t &command)
    {
        _state.record ("ProfileCommand", command.command_id);
    }

  private:
    scenario_state_t &_state;
};

class route_ping_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using request_type = route_ping_t;
    using reply_type = route_pong_t;

    explicit route_ping_handler_t (scenario_state_t &state) : _state (state) {}

    route_pong_t handle (const route_ping_t &request,
                         const zlink::framework::route_handler_context_t &context)
    {
        _state.record ("ScenarioRoutePing", request.value);
        return {.value = "route:" + request.value,
                .target_rid = _state.provider_rid,
                .source_rid = context.source_node_rid.to_string ()};
    }

  private:
    scenario_state_t &_state;
};

} // namespace zlink::framework::e2e::registry_messaging::provider
