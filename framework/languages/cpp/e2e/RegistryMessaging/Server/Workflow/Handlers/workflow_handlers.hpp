/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/scenario_state.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::registry_messaging::workflow
{

class workflow_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using request_type = profile_request_t;
    using reply_type = profile_reply_t;

    explicit workflow_request_handler_t (scenario_state_t &state) : _state (state) {}

    profile_reply_t handle (const profile_request_t &request)
    {
        _state.record ("WorkflowRequest", request.value);
        return {.value = "profile:" + request.value,
                .provider_rid = _state.provider_rid,
                .instance_id = _state.instance_id};
    }

  private:
    scenario_state_t &_state;
};

} // namespace zlink::framework::e2e::registry_messaging::workflow
