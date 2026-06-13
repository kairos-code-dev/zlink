/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../Configuration/sample_topology.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"
#include "../../../Application/GameCreation/tictactoe_game_creator.hpp"
#include "../Spots/tictactoe_game_spot.hpp"

#include <zlink/framework.hpp>

#include "runtime/spots/spot_runtime.hpp"

namespace zlink::samples::tictactoe
{

class create_game_handler_t
{
  public:
    using request_type = create_game_req_t;
    using reply_type = create_game_res_t;
    using dependency_types =
      zlink::framework::dependency_list_t<tictactoe_game_creator_t,
                                          sample_topology_t,
                                          zlink::framework::detail::spot_node_runtime_t>;
    static constexpr const char *topic_name = "CreateGame";

    create_game_handler_t (tictactoe_game_creator_t &creator,
                           sample_topology_t &topology,
                           zlink::framework::detail::spot_node_runtime_t &spots) :
        _creator (creator), _topology (topology), _spots (spots)
    {
    }

    create_game_res_t handle (const create_game_req_t &request)
    {
        const auto room_id = _creator.create_room_id ();
        const auto spot_rid = zlink::framework::spot_rid_t::from_string (
          std::string (sample_names_t::spot_node) + ":" + room_id);
        auto created = _spots.get_or_create_spot (sample_names_t::match_spot, spot_rid,
                                                  zlink::message_t::from (room_id));
        if (created.state == zlink::framework::spot_create_state_t::rejected) {
            throw zlink::framework::framework_exception_t (
              zlink::framework::framework_error_kind_t::spot_create_failed,
              "TicTacToe room Spot rejected create request");
        }
        return {room_id, _topology.stream_endpoint, request.game_name};
    }

  private:
    tictactoe_game_creator_t &_creator;
    sample_topology_t &_topology;
    zlink::framework::detail::spot_node_runtime_t &_spots;
};

} // namespace zlink::samples::tictactoe
