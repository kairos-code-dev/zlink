/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../../Shared/Contracts/messages.hpp"

namespace zlink::samples::tictactoe
{

struct tictactoe_game_snapshot_t
{
    tictactoe_state_t state;

    bool is_playing () const noexcept { return state.status == "playing"; }
    bool is_finished () const noexcept { return state.status == "ended"; }
};

} // namespace zlink::samples::tictactoe
