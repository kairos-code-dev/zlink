/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "player_actor.hpp"

namespace zlink::samples::tictactoe
{

class player_actor_transfer_adapter_t final
    : public actor_transfer_adapter_t<player_actor_t>
{
  public:
    task_t<message_t> transfer_out (const player_actor_t &actor) override
    {
        return task_t<message_t> (
          result_t<message_t>::success (message_t::from (actor)));
    }

    task_t<player_actor_t> transfer_in (std::string actor_id, message_t state) override
    {
        auto actor = state.decode<player_actor_t> ();
        actor.actor_id = std::move (actor_id);
        return task_t<player_actor_t> (result_t<player_actor_t>::success (std::move (actor)));
    }
};

} // namespace zlink::samples::tictactoe
