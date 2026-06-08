/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Domain/Bingo/bingo_room_game.hpp"

#include <map>
#include <stdexcept>
#include <string>

namespace zlink::samples::bingo
{

class bingo_room_allocator_t
{
  public:
    std::string allocate (const std::string &mode)
    {
        for (const auto &[room_id, room] : _rooms) {
            if (room.snapshot ().status == "waiting" && room.snapshot ().players.size () < 2) {
                return room_id;
            }
        }

        const std::string room_id = mode + "-room-" + std::to_string (_next++);
        _rooms.emplace (room_id, bingo_room_game_t (room_id));
        return room_id;
    }

    bingo_room_game_t &get (const std::string &room_id)
    {
        auto found = _rooms.find (room_id);
        if (found == _rooms.end ()) {
            throw std::runtime_error ("unknown bingo room");
        }
        return found->second;
    }

  private:
    int _next = 1;
    std::map<std::string, bingo_room_game_t> _rooms;
};

} // namespace zlink::samples::bingo
