/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>
#include <utility>

namespace zlink::samples::tictactoe
{

struct player_actor_t
{
  std::string actor_id;
};

struct player_actor_factory_t
{
  player_actor_t create (std::string actor_id) const
  {
    return { std::move (actor_id) };
  }
};

struct move_packet_t
{
  int cell = 0;
};

} // namespace zlink::samples::tictactoe
