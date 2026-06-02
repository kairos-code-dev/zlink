/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../game_notification_publisher.hpp"

#include <utility>

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_actor_joined_handler_t
{
public:
  explicit tictactoe_game_spot_actor_joined_handler_t (
    game_notification_publisher_t &publisher)
    : _publisher (publisher)
  {
  }

  void handle (opponent_joined_notify_t notify)
  {
    _publisher.publish_opponent_joined (std::move (notify));
  }

private:
  game_notification_publisher_t &_publisher;
};

} // namespace zlink::samples::tictactoe
