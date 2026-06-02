/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include <utility>
#include <vector>

namespace zlink::samples::bingo
{

class bingo_notification_publisher_t
{
public:
  void publish_joined (player_joined_notify_t notify)
  {
    joined.push_back (std::move (notify));
  }

  void publish_started (game_started_notify_t notify)
  {
    started.push_back (std::move (notify));
  }

  void publish_drawn (number_drawn_notify_t notify)
  {
    drawn.push_back (std::move (notify));
  }

  std::vector<player_joined_notify_t> joined;
  std::vector<game_started_notify_t> started;
  std::vector<number_drawn_notify_t> drawn;
};

} // namespace zlink::samples::bingo
