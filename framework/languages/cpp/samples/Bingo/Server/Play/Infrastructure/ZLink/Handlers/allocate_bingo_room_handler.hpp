/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

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
      dependency_list_t<bingo_room_allocator_t, spot_manager_t>;
    static constexpr const char *topic_name = "AllocateBingoRoom";

    allocate_bingo_room_handler_t (bingo_room_allocator_t &rooms,
                                   spot_manager_t &spots) :
        _rooms (rooms), _spots (spots)
    {
    }

    task_t<allocate_bingo_room_res_t> handle (const allocate_bingo_room_req_t &request,
                                              const message_context_t &)
    {
        if (request.actor_id.empty ()) {
            throw std::runtime_error ("actor id must not be empty");
        }
        const auto reservation = _rooms.allocate (request.mode, request.actor_id);
        const auto payload = bingo_room_settings_payload_t{
          "Bingo Room " + reservation.room_id, request.mode, 2, 15, "Game", ""};
        co_await _spots.get_or_create (reservation.room_id, sample_names_t::room_spot)
          .creation_request (payload)
          .submit ();
        co_return allocate_bingo_room_res_t{reservation.room_id};
    }

  private:
    bingo_room_allocator_t &_rooms;
    spot_manager_t &_spots;
};

} // namespace zlink::samples::bingo
