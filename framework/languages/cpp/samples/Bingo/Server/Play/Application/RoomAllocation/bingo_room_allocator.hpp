/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "bingo_match_queue.hpp"

#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace zlink::samples::bingo
{

class bingo_room_allocator_t
{
  public:
    explicit bingo_room_allocator_t (bingo_match_queue_t &match_queue) : _match_queue (match_queue)
    {
    }

    bingo_match_reservation_t allocate (const std::string &mode,
                                        const std::string &actor_id,
                                        const std::string &preferred_owner_node_rid)
    {
        const auto room_id = new_room_id ();
        auto reservation =
          _match_queue.reserve (mode, actor_id, preferred_owner_node_rid, room_id, 2);
        reservation.created_local_room =
          reservation.room_id == room_id
          && reservation.owner_play_node_rid == preferred_owner_node_rid;
        return reservation;
    }

  private:
    static std::string new_room_id ()
    {
        static thread_local std::mt19937_64 random{std::random_device{} ()};
        std::ostringstream room_id;
        room_id << "bingo-room-" << std::hex << std::setfill ('0') << std::setw (16) << random ()
                << std::setw (16) << random ();
        return room_id.str ();
    }

    bingo_match_queue_t &_match_queue;
};

} // namespace zlink::samples::bingo
