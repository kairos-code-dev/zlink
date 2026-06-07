/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <utility>
#include <vector>

namespace zlink::samples::tictactoe
{

class game_notification_publisher_t
{
  public:
    void publish_opponent_joined (opponent_joined_notify_t notify)
    {
        opponent_joined.push_back (std::move (notify));
    }

    void publish_turn_changed (turn_changed_notify_t notify)
    {
        turn_changed.push_back (std::move (notify));
    }

    void publish_game_ended (game_ended_notify_t notify)
    {
        game_ended.push_back (std::move (notify));
    }

    std::vector<opponent_joined_notify_t> opponent_joined;
    std::vector<turn_changed_notify_t> turn_changed;
    std::vector<game_ended_notify_t> game_ended;
};

} // namespace zlink::samples::tictactoe
