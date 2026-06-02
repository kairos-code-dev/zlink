/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../BingoRoomSpots/bingo_room.hpp"

#include <map>
#include <stdexcept>
#include <string>

namespace zlink::samples::bingo
{

class bingo_room_directory_t
{
public:
  std::string allocate (const std::string &mode)
  {
    const std::string room_id = mode + "-room-" + std::to_string (_next++);
    _rooms.emplace (room_id, bingo_room_t (room_id));
    return room_id;
  }

  bingo_room_t &get (const std::string &room_id)
  {
    auto found = _rooms.find (room_id);
    if (found == _rooms.end ()) {
      throw std::runtime_error ("unknown bingo room");
    }
    return found->second;
  }

private:
  int _next = 1;
  std::map<std::string, bingo_room_t> _rooms;
};

} // namespace zlink::samples::bingo
