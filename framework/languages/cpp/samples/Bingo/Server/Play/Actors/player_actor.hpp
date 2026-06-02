/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

namespace zlink::samples::bingo
{

struct player_actor_t
{
  actor_ref_snapshot_t actor;
  std::string display_name;
};

} // namespace zlink::samples::bingo
