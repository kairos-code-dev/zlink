/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Shared/Contracts/messages.hpp"

#include <vector>

namespace zlink::samples::bingo
{

class bingo_notification_inbox_t
{
  public:
    void on_joined (const player_joined_notify_t &notify) { joined.push_back (notify); }

    void on_drawn (const number_drawn_notify_t &notify) { drawn.push_back (notify); }

    void on_started (const game_started_notify_t &notify) { started.push_back (notify); }

    void on_state (const state_notify_t &notify) { states.push_back (notify); }

    void on_ended (const game_ended_notify_t &notify) { ended.push_back (notify); }

    std::vector<player_joined_notify_t> joined;
    std::vector<game_started_notify_t> started;
    std::vector<number_drawn_notify_t> drawn;
    std::vector<state_notify_t> states;
    std::vector<game_ended_notify_t> ended;
};

} // namespace zlink::samples::bingo
