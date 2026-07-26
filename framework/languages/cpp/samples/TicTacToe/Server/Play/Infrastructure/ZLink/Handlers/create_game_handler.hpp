/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../../Configuration/sample_topology.hpp"
#include "../../../../../Shared/Contracts/messages.hpp"
#include "../../../Application/GameCreation/tictactoe_game_creator.hpp"
#include "../Spots/TicTacToeGameSpot/tictactoe_game_spot.hpp"

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

using namespace framework;

class create_game_handler_t
{
  public:
    using request_type = create_game_req_t;
    using reply_type = create_game_res_t;
    using dependency_types =
      dependency_list_t<tictactoe_game_creator_t, spot_manager_t>;
    static constexpr const char *topic_name = "CreateGame";

    create_game_handler_t (tictactoe_game_creator_t &creator,
                           spot_manager_t &spots) :
        _creator (creator), _spots (spots)
    {
    }

    task_t<create_game_res_t> handle (const create_game_req_t &request,
                                      const message_context_t &)
    {
        auto response = _creator.create (request.game_name);
        co_await _spots.get_or_create (response.room_id, sample_names_t::match_spot)
          .submit ();
        co_return response;
    }

  private:
    tictactoe_game_creator_t &_creator;
    spot_manager_t &_spots;
};

} // namespace zlink::samples::tictactoe
