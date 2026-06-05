/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

namespace zlink::samples::bingo
{

struct bingo_room_snapshot_t
{
    bingo_room_state_t state;

    bool is_playing () const noexcept { return state.status == "playing"; }
    bool has_winner () const noexcept { return !state.winners.empty (); }
};

} // namespace zlink::samples::bingo
