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
    void opponent_joined (opponent_joined_notify_t notify) { opponents.push_back (std::move (notify)); }

    void turn_changed (turn_changed_notify_t notify) { turns.push_back (std::move (notify)); }

    void game_ended (game_ended_notify_t notify) { ended.push_back (std::move (notify)); }

    std::vector<opponent_joined_notify_t> opponents;
    std::vector<turn_changed_notify_t> turns;
    std::vector<game_ended_notify_t> ended;
};

} // namespace zlink::samples::tictactoe
