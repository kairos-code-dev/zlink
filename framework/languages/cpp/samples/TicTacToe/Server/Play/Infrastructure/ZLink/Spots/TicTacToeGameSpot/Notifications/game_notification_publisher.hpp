/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "../../../../../../../Shared/Contracts/messages.hpp"

#include <utility>
#include <vector>

namespace zlink::samples::tictactoe
{

class game_notification_publisher_t
{
  public:
    void publish_player_joined (player_joined_notify_t notify)
    {
        player_joined.push_back (std::move (notify));
    }

    void publish_game_state (game_state_notify_t notify)
    {
        game_state.push_back (std::move (notify));
    }

    void publish_game_ended (game_ended_notify_t notify)
    {
        game_ended.push_back (std::move (notify));
    }

    std::vector<player_joined_notify_t> player_joined;
    std::vector<game_state_notify_t> game_state;
    std::vector<game_ended_notify_t> game_ended;
};

} // namespace zlink::samples::tictactoe
