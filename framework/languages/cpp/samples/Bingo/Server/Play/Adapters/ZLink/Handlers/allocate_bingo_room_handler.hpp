/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Configuration/sample_topology.hpp"
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
    using dependency_types =
      zlink::framework::dependency_list_t<bingo_room_allocator_t, sample_topology_t>;
    static constexpr const char *topic_name = "AllocateBingoRoom";

    allocate_bingo_room_handler_t (bingo_room_allocator_t &rooms, sample_topology_t &topology) :
        _rooms (rooms), _topology (topology)
    {
    }

    allocate_bingo_room_res_t handle (const allocate_bingo_room_req_t &request)
    {
        if (request.actor_id.empty ()) {
            throw std::runtime_error ("actor id must not be empty");
        }
        const auto owner_node_rid = _topology.selected_play_node_rid ();
        const auto reservation =
          _rooms.allocate (request.mode, request.actor_id, owner_node_rid);
        return {reservation.room_id, reservation.owner_play_node_rid};
    }

  private:
    bingo_room_allocator_t &_rooms;
    sample_topology_t &_topology;
};

} // namespace zlink::samples::bingo
