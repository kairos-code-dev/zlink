/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../game_notification_publisher.hpp"

#include <utility>

namespace zlink::samples::tictactoe
{

class tictactoe_game_spot_actor_left_handler_t
{
public:
  explicit tictactoe_game_spot_actor_left_handler_t (
    game_notification_publisher_t &publisher)
    : _publisher (publisher)
  {
  }

  void handle (game_ended_notify_t notify)
  {
    _publisher.publish_game_ended (std::move (notify));
  }

private:
  game_notification_publisher_t &_publisher;
};

} // namespace zlink::samples::tictactoe
