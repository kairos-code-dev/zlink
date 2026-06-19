/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "bingo_match_queue.hpp"

#include <atomic>
#include <string>

namespace zlink::samples::bingo
{

class bingo_room_allocator_t
{
  public:
    explicit bingo_room_allocator_t (bingo_match_queue_t &match_queue) :
        _match_queue (match_queue)
    {
    }

    bingo_match_reservation_t allocate (const std::string &mode,
                                        const std::string &actor_id,
                                        const std::string &preferred_owner_node_rid)
    {
        const auto room_id = mode + "-room-" + std::to_string (++_next);
        return _match_queue.reserve (mode, actor_id, preferred_owner_node_rid, room_id, 2);
    }

  private:
    bingo_match_queue_t &_match_queue;
    std::atomic<unsigned long long> _next{0};
};

} // namespace zlink::samples::bingo
