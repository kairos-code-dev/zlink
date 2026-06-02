/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../bingo_notification_publisher.hpp"

#include <utility>

namespace zlink::samples::bingo
{

class bingo_room_actor_joined_handler_t
{
public:
  explicit bingo_room_actor_joined_handler_t (
    bingo_notification_publisher_t &publisher)
    : _publisher (publisher)
  {
  }

  void handle (player_joined_notify_t notify)
  {
    _publisher.publish_joined (std::move (notify));
  }

private:
  bingo_notification_publisher_t &_publisher;
};

} // namespace zlink::samples::bingo
