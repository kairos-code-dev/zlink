/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/scenario_state.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::registration_codec::server
{

class auto_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using request_type = echo_auto_req_t;
    using reply_type = echo_auto_res_t;

    explicit auto_request_handler_t (scenario_state_t &state) : _state (state) {}

    echo_auto_res_t handle (const echo_auto_req_t &request)
    {
        _state.record ("RC-A1-request", request.value);
        return {.value = "auto:" + request.value};
    }

  private:
    scenario_state_t &_state;
};

class auto_send_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using message_type = echo_auto_msg_t;

    explicit auto_send_handler_t (scenario_state_t &state) : _state (state) {}

    void handle (const echo_auto_msg_t &message)
    {
        _state.record ("RC-A1-send", message.value);
    }

  private:
    scenario_state_t &_state;
};

class manual_route_handler_t
{
  public:
    echo_manual_res_t handle (const echo_manual_req_t &request,
                                const zlink::framework::route_handler_context_t &context)
    {
        return {.value = "manual:" + request.value,
                .packet_name = context.packet_name,
                .content_type = context.content_type};
    }
};

} // namespace zlink::framework::e2e::registration_codec::server
