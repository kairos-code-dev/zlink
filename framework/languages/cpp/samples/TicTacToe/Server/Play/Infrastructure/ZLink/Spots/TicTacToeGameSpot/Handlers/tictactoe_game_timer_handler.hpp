/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../tictactoe_game_spot.hpp"

namespace zlink::samples::tictactoe
{

inline task_t<void> tictactoe_game_spot_t::handle_game_tick (const timer_tick_t &)
{
    if (!tick ()) {
        co_return;
    }
    const auto &state = snapshot ();
    const auto notify = game_state_notify_t{state.room_id, state.next_turn, state};
    publisher.publish_game_state (notify);
    for (auto &[_, actor] : actors) {
        actor->context.bound_session ().send (notify).submit ();
    }
}

inline task_t<void>
tictactoe_game_timer_handler_t::handle (tictactoe_game_spot_t &spot,
                                        const timer_tick_t &tick) const
{
    co_await spot.handle_game_tick (tick);
}

} // namespace zlink::samples::tictactoe
