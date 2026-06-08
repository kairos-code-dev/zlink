/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <utility>
#include <vector>

namespace zlink::samples::tictactoe
{

class notification_inbox_t
{
  public:
    void player_joined (player_joined_notify_t notify) { players.push_back (std::move (notify)); }

    void game_state (game_state_notify_t notify) { states.push_back (std::move (notify)); }

    void game_ended (game_ended_notify_t notify) { ended.push_back (std::move (notify)); }

    std::vector<player_joined_notify_t> players;
    std::vector<game_state_notify_t> states;
    std::vector<game_ended_notify_t> ended;
};

} // namespace zlink::samples::tictactoe
