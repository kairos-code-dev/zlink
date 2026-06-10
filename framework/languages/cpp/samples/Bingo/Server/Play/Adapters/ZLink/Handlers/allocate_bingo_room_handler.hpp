/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Application/RoomAllocation/bingo_room_allocator.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>

namespace zlink::samples::bingo
{

class allocate_bingo_room_handler_t
{
  public:
    using request_type = allocate_bingo_room_req_t;
    using reply_type = allocate_bingo_room_res_t;
    using dependency_types = zlink::framework::dependency_list_t<bingo_room_allocator_t>;
    static constexpr const char *topic_name = "AllocateBingoRoom";

    explicit allocate_bingo_room_handler_t (bingo_room_allocator_t &rooms) : _rooms (rooms) {}

    allocate_bingo_room_res_t handle (const allocate_bingo_room_req_t &request)
    {
        if (request.actor_id.empty ()) {
            throw std::runtime_error ("actor id must not be empty");
        }
        return {_rooms.allocate (request.mode)};
    }

  private:
    bingo_room_allocator_t &_rooms;
};

} // namespace zlink::samples::bingo
