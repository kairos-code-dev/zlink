/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Configuration/sample_names.hpp"
#include "Configuration/sample_topology.hpp"
#include "Contracts/messages.hpp"
#include "../Client/bingo_notification_inbox.hpp"
#include "../Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../Server/Api/Handlers/match_bingo_handler.hpp"
#include "../Server/Play/BingoRoomSpots/bingo_room.hpp"
#include "../Server/Play/BingoRoomSpots/Handlers/bingo_room_actor_left_handler.hpp"
#include "../Server/Play/BingoRoomSpots/Handlers/bingo_room_join_handler.hpp"
#include "../Server/Play/BingoRoomSpots/Handlers/bingo_room_timer_handler.hpp"
#include "../Server/Play/BingoRoomSpots/Handlers/start_bingo_game_handler.hpp"
#include "../Server/Play/EntrySpot/Handlers/match_bingo_actor_handler.hpp"
#include "../Server/Play/Handlers/allocate_bingo_room_handler.hpp"
#include "../Server/Play/Handlers/ensure_player_actor_handler.hpp"
#include "host_support.hpp"

namespace zlink::samples::bingo
{

using zlink::framework::task_t;

} // namespace zlink::samples::bingo
