/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Actors/player_actor.hpp"
#include "Configuration/sample_names.hpp"
#include "Configuration/sample_topology.hpp"
#include "Contracts/messages.hpp"
#include "../Client/session_actor_notification_inbox.hpp"
#include "../Server/Api/Handlers/authenticate_actor_handler.hpp"
#include "../Server/Api/Handlers/create_match_handler.hpp"
#include "../Server/Play/EntrySpot/Handlers/join_match_handler.hpp"
#include "../Server/Play/EntrySpot/tictactoe_entry_spot.hpp"
#include "../Server/Play/GameSpots/Handlers/place_mark_handler.hpp"
#include "../Server/Play/GameSpots/tictactoe_match_room.hpp"
#include "../Server/Play/Handlers/create_match_room_handler.hpp"
#include "../Server/Play/Handlers/ensure_player_actor_handler.hpp"
#include "host_support.hpp"

namespace zlink::samples::tictactoe
{

using zlink::framework::task_t;

} // namespace zlink::samples::tictactoe
