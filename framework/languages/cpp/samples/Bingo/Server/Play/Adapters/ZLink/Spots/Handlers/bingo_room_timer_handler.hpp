/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Domain/Bingo/bingo_room_game.hpp"

namespace zlink::samples::bingo
{

struct bingo_room_timer_handler_t
{
    number_drawn_notify_t handle (bingo_room_game_t &room, int number) const
    {
        (void) number;
        auto drawn = room.draw_next ();
        if (!drawn) {
            return {room.snapshot ().room_id, room.snapshot ().draw_seq, 0, room.snapshot ()};
        }
        return *drawn;
    }
};

} // namespace zlink::samples::bingo
