/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../../Configuration/sample_topology.hpp"
#include "../../../../Configuration/sample_names.hpp"
#include "../../../Application/RoomAllocation/bingo_room_allocator.hpp"

#include <zlink/framework.hpp>

#include <stdexcept>

namespace zlink::samples::bingo
{

using namespace framework;

class allocate_bingo_room_handler_t
{
  public:
    using request_type = allocate_bingo_room_req_t;
    using reply_type = allocate_bingo_room_res_t;
    using dependency_types =
      dependency_list_t<bingo_room_allocator_t, sample_topology_t, spot_node_manager_t>;
    static constexpr const char *topic_name = "AllocateBingoRoom";

    allocate_bingo_room_handler_t (bingo_room_allocator_t &rooms,
                                   sample_topology_t &topology,
                                   spot_node_manager_t &spots) :
        _rooms (rooms), _topology (topology), _spots (spots)
    {
    }

    allocate_bingo_room_res_t handle (const allocate_bingo_room_req_t &request)
    {
        if (request.actor_id.empty ()) {
            throw std::runtime_error ("actor id must not be empty");
        }
        const auto owner_node_rid = request.preferred_owner_node_rid.empty ()
                                      ? _topology.selected_play_node_rid ()
                                      : request.preferred_owner_node_rid;
        const auto reservation = _rooms.allocate (request.mode, request.actor_id, owner_node_rid);
        if (reservation.created_local_room) {
            const auto spot_rid = spot_rid_t::from_string (reservation.room_id);
            const auto payload = bingo_room_settings_payload_t{
              "Bingo Room " + reservation.room_id, request.mode, 2, 15, "Game", ""};
            try {
                _spots.get_or_create_spot (sample_names_t::room_spot, spot_rid, payload);
            }
            catch (const std::exception &error) {
                throw std::runtime_error ("failed to materialize bingo room '"
                                          + reservation.room_id + "' on node '"
                                          + owner_node_rid + "': " + error.what ());
            }
        }
        return {reservation.room_id, reservation.owner_play_node_rid};
    }

  private:
    bingo_room_allocator_t &_rooms;
    sample_topology_t &_topology;
    spot_node_manager_t &_spots;
};

} // namespace zlink::samples::bingo
