/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/scenario_state.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::registration_codec::server
{

class filter_order_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<filter_order_state_t>;
    using request_type = filter_order_req_t;
    using reply_type = filter_order_res_t;

    explicit filter_order_handler_t (filter_order_state_t &state) : _state (state) {}

    filter_order_res_t handle (const filter_order_req_t &request)
    {
        _state.add ("handler");
        return {.value = request.value, .order = _state.snapshot ()};
    }

  private:
    filter_order_state_t &_state;
};

class first_filter_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<filter_order_state_t, scenario_state_t>;

    first_filter_t (filter_order_state_t &state, scenario_state_t &scenario_state) :
        _state (state), _scenario_state (scenario_state)
    {
    }

    zlink::framework::task_t<zlink::message_t>
    invoke (const zlink::framework::handler_invocation_context_t &context,
            zlink::framework::handler_next_t next)
    {
        if (context.descriptor.packet_name != filter_order_req_t::packet_name) {
            co_return co_await next ();
        }

        _state.reset ();
        _state.add ("first-before");
        auto reply = co_await next ();
        _state.add ("first-after");
        const auto order = _state.snapshot ();
        _scenario_state.record ("RC-A5", nlohmann::json (order).dump ());
        co_return reply;
    }

  private:
    filter_order_state_t &_state;
    scenario_state_t &_scenario_state;
};

class second_filter_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<filter_order_state_t>;

    explicit second_filter_t (filter_order_state_t &state) : _state (state) {}

    zlink::framework::task_t<zlink::message_t>
    invoke (const zlink::framework::handler_invocation_context_t &context,
            zlink::framework::handler_next_t next)
    {
        if (context.descriptor.packet_name != filter_order_req_t::packet_name) {
            co_return co_await next ();
        }

        _state.add ("second-before");
        auto reply = co_await next ();
        _state.add ("second-after");
        co_return reply;
    }

  private:
    filter_order_state_t &_state;
};

} // namespace zlink::framework::e2e::registration_codec::server
