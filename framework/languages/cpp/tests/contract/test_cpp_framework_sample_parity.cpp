/* SPDX-License-Identifier: MPL-2.0 */

#include "../../samples/Bingo/Shared/sample.hpp"
#include "../../samples/Bingo/Server/Play/Actors/player_actor_factory.hpp"
#include "../../samples/Bingo/Server/Play/BingoRoomSpots/Handlers/bingo_room_timer_handler.hpp"
#include "../../samples/Bingo/Server/Play/BingoRoomSpots/bingo_notification_publisher.hpp"
#include "../../samples/Bingo/Server/Play/BingoRoomSpots/bingo_room_spot.hpp"
#include "../../samples/TicTacToe/Shared/sample.hpp"
#include "../../samples/TicTacToe/Server/Play/GameSpots/game_notification_publisher.hpp"
#include "../../samples/TicTacToe/Server/Play/GameSpots/tictactoe_game_contract_mapper.hpp"
#include "../../samples/TicTacToe/Server/Play/GameSpots/tictactoe_game_spot.hpp"

#include <gtest/gtest.h>

TEST (CppFrameworkSampleParity, BingoUsesDotNetSamplePacketSurface)
{
  using namespace zlink::samples::bingo;

  EXPECT_STREQ (sample_names_t::player_joined_packet, "PlayerJoinedNotify");
  EXPECT_STREQ (sample_names_t::game_started_packet, "BingoGameStartedNotify");
  EXPECT_STREQ (sample_names_t::number_drawn_packet, "BingoNumberDrawnNotify");
  EXPECT_STREQ (sample_names_t::state_packet, "BingoStateNotify");
  EXPECT_STREQ (sample_names_t::game_ended_packet, "BingoGameEndedNotify");

  authenticate_player_handler_t auth;
  const auto authenticated = auth.handle ({ "player-1" });
  ASSERT_TRUE (authenticated.accepted);

  bingo_room_directory_t rooms;
  allocate_bingo_room_handler_t allocator (rooms);
  const auto allocated = allocator.handle ({ "four-player" });
  bingo_room_join_handler_t join (rooms);
  const auto joined = join.handle (
    { allocated.room_id, authenticated.actor_id, authenticated.display_name });
  EXPECT_EQ (joined.state.players.size (), 1U);

  ensure_player_actor_handler_t actors;
  const auto actor =
    actors.handle ({ authenticated.actor_id, authenticated.display_name });
  EXPECT_STREQ (actor.actor_type.c_str (), sample_names_t::player_actor_type);

  player_actor_factory_t actor_factory;
  const auto player_actor = actor_factory.create (actor.actor);
  EXPECT_EQ (player_actor.actor.actor_id, authenticated.actor_id);

  bingo_room_spot_t room_spot (allocated.room_id);
  bingo_room_timer_handler_t timer;
  const auto drawn = timer.handle (room_spot, 1);
  EXPECT_EQ (drawn.number, 1);

  bingo_notification_publisher_t publisher;
  publisher.publish_drawn (drawn);
  EXPECT_EQ (publisher.drawn.size (), 1U);
}

TEST (CppFrameworkSampleParity, TicTacToeUsesDotNetSamplePacketSurface)
{
  using namespace zlink::samples::tictactoe;

  EXPECT_STREQ (sample_names_t::turn_changed_packet, "TurnChangedNotify");
  EXPECT_STREQ (sample_names_t::opponent_joined_packet,
                "OpponentJoinedNotify");
  EXPECT_STREQ (sample_names_t::game_ended_packet, "GameEndedNotify");

  authenticate_actor_handler_t auth;
  const auto authenticated =
    auth.handle ({ sample_names_t::x_actor_id });
  ASSERT_TRUE (authenticated.accepted);

  create_match_room_handler_t rooms;
  create_match_handler_t create (rooms);
  const auto created = create.handle ({ authenticated.actor_id });
  tictactoe_match_room_t room (created.match_id);
  room.create ({ created.owner_actor_id });

  join_match_handler_t join (room);
  const auto joined =
    join.handle ({ created.match_id, sample_names_t::o_actor_id });
  EXPECT_EQ (joined.mark, "O");

  place_mark_handler_t place (room);
  const auto moved =
    place.handle ({ created.match_id, sample_names_t::x_actor_id, 0 });
  EXPECT_EQ (moved.state.last_move_actor_id, sample_names_t::x_actor_id);

  tictactoe_game_spot_t game_spot (created.match_id);
  const auto mapped =
    tictactoe_game_contract_mapper_t::to_contract (moved.state);
  EXPECT_EQ (mapped.match_id, created.match_id);

  game_notification_publisher_t publisher;
  publisher.turn_changed.push_back (
    { created.match_id, moved.state.turn_actor_id, moved.state });
  EXPECT_EQ (publisher.turn_changed.size (), 1U);
}

int
main (int argc, char **argv)
{
  testing::InitGoogleTest (&argc, argv);
  return RUN_ALL_TESTS ();
}
