/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/scenario_state.hpp"

#include <zlink/framework.hpp>

#include <cstdint>

namespace zlink::framework::e2e::registry_messaging::provider
{

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

class server_weight_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      zlink::framework::channel_runtime_options_t>;

    explicit server_weight_handler_t (zlink::framework::channel_runtime_options_t &options) :
        _options (options)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &request)
    {
        const auto found = request.query_values.find ("weight");
        if (found == request.query_values.end ()) {
            zlink::framework::http_response_t response;
            response.status = 400;
            response.body = R"({"error":"weight is required"})";
            return response;
        }
        const auto weight = static_cast<std::uint32_t> (std::stoul (found->second));
        _options.client_server_channel (api_channel)
          .configure_server_socket ()
          .peer_weight (zlink::peer_weight_t::value (weight));
        zlink::framework::http_response_t response;
        response.body = nlohmann::json{{"weight", weight}}.dump ();
        return response;
    }

  private:
    zlink::framework::channel_runtime_options_t &_options;
};

class observer_fault_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit observer_fault_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        _state.set_fault_mode ("observer-throws");
        zlink::framework::http_response_t response;
        response.body = R"({"mode":"observer-throws"})";
        return response;
    }

  private:
    scenario_state_t &_state;
};

class clear_fault_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit clear_fault_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        _state.set_fault_mode ("none");
        zlink::framework::http_response_t response;
        response.body = R"({"mode":"none"})";
        return response;
    }

  private:
    scenario_state_t &_state;
};

} // namespace zlink::framework::e2e::registry_messaging::provider
