/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework.hpp>

#include <cstdint>
#include <string>
#include <utility>

namespace zlink::framework::e2e::yield_dispatch::server::play
{

struct yield_actor_t
{
    explicit yield_actor_t (std::string actor_id) : actor_id (std::move (actor_id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (zlink::framework::actor_context_t value)
    {
        context = std::move (value);
    }

    std::string actor_id;
    zlink::framework::actor_ref_t actor_ref;
    zlink::framework::actor_context_t context;
};

struct yield_actor_factory_t
{
    yield_actor_t create (std::string actor_id) const
    {
        return yield_actor_t (std::move (actor_id));
    }
};

class yield_probe_spot_t;

struct yield_timer_handler_t
{
    zlink::framework::task_t<void>
    handle (yield_probe_spot_t &spot, const zlink::framework::timer_tick_t &tick) const;
};

struct yield_timer_state_t
{
    std::string request_id;
    std::string timer_name;
    std::string mode;
    int delay_ms = 0;
    std::uint64_t tick_count = 0;
    bool active = true;
    zlink::framework::timer_t timer;
};

struct yield_timer_tick_state_t
{
    std::string request_id;
    std::string timer_name;
    std::string mode;
    int delay_ms = 0;
    std::uint64_t tick_number = 0;
};

} // namespace zlink::framework::e2e::yield_dispatch::server::play
