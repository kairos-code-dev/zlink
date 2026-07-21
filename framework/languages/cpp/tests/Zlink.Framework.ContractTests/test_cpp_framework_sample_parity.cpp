/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../../samples/Bingo/Server/Configuration/sample_names.hpp"
#include "../../samples/Bingo/Server/Configuration/sample_topology.hpp"
#include "../../samples/Bingo/Shared/Contracts/messages.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Actors/player_actor_factory.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Handlers/allocate_bingo_room_handler.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo_room_spot.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo_entry_spot.hpp"
#include "../../samples/Bingo/Server/Play/Application/RoomAllocation/bingo_room_allocator.hpp"
#include "../../samples/Bingo/Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../../samples/TicTacToe/Server/Configuration/sample_names.hpp"
#include "../../samples/TicTacToe/Server/Configuration/sample_topology.hpp"
#include "../../samples/TicTacToe/Shared/Contracts/messages.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe_game_spot.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/create_game_http_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Handlers/create_game_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/tictactoe_entry_spot.hpp"
#include "../../samples/TicTacToe/Server/Play/Application/GameCreation/tictactoe_game_creator.hpp"
#include "../../samples/TicTacToe/Server/Play/Domain/TicTacToe/tictactoe_match.hpp"
#include "../../samples/DeliveryDispatch/Shared/Contracts/messages.hpp"
#include "../../samples/GameQuest/Shared/Contracts/messages.hpp"
#include "../../samples/ShoppingMall/Shared/Contracts/messages.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

std::string read_file (const std::filesystem::path &path)
{
    std::ifstream input (path);
    std::ostringstream output;
    output << input.rdbuf ();
    return output.str ();
}

std::filesystem::path cpp_language_root ()
{
    auto path = std::filesystem::path (__FILE__).lexically_normal ();
    while (!path.empty () && path.filename () != "cpp") {
        path = path.parent_path ();
    }
    return path;
}

std::filesystem::path repository_root ()
{
    return cpp_language_root ().parent_path ().parent_path ().parent_path ();
}

bool has_suffix (const std::filesystem::path &path, const std::string &suffix)
{
    const auto value = path.string ();
    return value.size () >= suffix.size ()
           && value.compare (value.size () - suffix.size (), suffix.size (), suffix) == 0;
}

bool contains_any (const std::string &content, const std::vector<std::string> &patterns)
{
    return std::any_of (patterns.begin (), patterns.end (), [&content] (const auto &pattern) {
        return content.find (pattern) != std::string::npos;
    });
}

std::vector<std::filesystem::path> sample_source_files ()
{
    std::vector<std::filesystem::path> files;
    const auto samples_root = cpp_language_root () / "samples";
    for (const auto &entry : std::filesystem::recursive_directory_iterator (samples_root)) {
        if (!entry.is_regular_file ()) {
            continue;
        }
        const auto path = entry.path ();
        if (has_suffix (path, ".cpp") || has_suffix (path, ".hpp")) {
            files.push_back (path);
        }
    }
    return files;
}

std::string relative_sample_path (const std::filesystem::path &path)
{
    return std::filesystem::relative (path, cpp_language_root () / "samples").generic_string ();
}

} // namespace

TEST (CppFrameworkSampleParity, BingoUsesDotNetSamplePacketSurface)
{
    using namespace zlink::samples::bingo;

    EXPECT_STREQ (sample_names_t::player_joined_packet, "PlayerJoinedNotify");
    EXPECT_STREQ (sample_names_t::game_started_packet, "BingoGameStartedNotify");
    EXPECT_STREQ (sample_names_t::number_drawn_packet, "BingoNumberDrawnNotify");
    EXPECT_STREQ (sample_names_t::game_ended_packet, "BingoGameEndedNotify");
    EXPECT_STREQ (bingo_reward_acquired_event_t::packet_name, "BingoRewardAcquiredEvent");

    authenticate_player_handler_t auth;
    const auto authenticated = auth.handle ({"player-1"});
    ASSERT_TRUE (authenticated.accepted);

    sample_topology_t topology;
    const allocate_bingo_room_res_t allocated{"two-player-room-1",
                                              topology.selected_play_node_rid ()};

    zlink::framework::service_collection_t entry_services;
    entry_services.add_scoped<zlink::framework::session_actor_manager_t> ();
    bingo_entry_spot_t entry_spot (topology, entry_services.build_provider ());
    const actor_ref_snapshot_t actor{node_rid_t::from_string (
                                       topology.selected_play_node_rid ()),
                                     authenticated.actor_id, 1};

    player_actor_factory_t actor_factory;
    const auto player_actor = actor_factory.create (actor, authenticated.display_name);
    EXPECT_EQ (player_actor.actor.actor_id, authenticated.actor_id);

    bingo_room_spot_t room_spot (allocated.room_id);
    const auto joined = room_spot.on_actor_join (
      authenticated.actor_id,
      zlink::framework::message_t::from (bingo_room_join_req_t{
                      allocated.room_id, authenticated.actor_id, authenticated.display_name}));
    ASSERT_TRUE (joined.accepted);
    ASSERT_TRUE (joined.reply);
    const auto join_reply = joined.reply->decode<bingo_room_join_res_t> ();
    EXPECT_EQ (join_reply.state.players.size (), 1U);

    zlink::framework::spot_context_t room_context;
    room_spot.configure (room_context);
    const auto room_handlers = room_context.handlers ().descriptors ();
    ASSERT_EQ (room_handlers.size (), 4U);
    EXPECT_EQ (room_handlers[0].kind, zlink::framework::spot_handler_kind_t::actor_request);
    EXPECT_EQ (room_handlers[0].packet_name, submit_bingo_card_req_t::packet_name);

    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 3U);
    EXPECT_EQ (entry_handlers[0].kind, zlink::framework::spot_handler_kind_t::packet);
    EXPECT_EQ (entry_handlers[0].packet_name, ensure_player_actor_req_t::packet_name);

    auto second_actor = actor_factory.create (actor_ref_snapshot_t{{}, "player-2", 1}, "Player 2");
    const auto second_joined = room_spot.on_actor_join (
      "player-2",
      zlink::framework::message_t::from (
                      bingo_room_join_req_t{allocated.room_id, "player-2", "Player 2"}));
    ASSERT_TRUE (second_joined.accepted);
    ASSERT_TRUE (second_joined.reply);
    EXPECT_EQ (second_joined.reply->decode<bingo_room_join_res_t> ().state.players.size (), 1U);
}

TEST (CppFrameworkSampleParity, BingoClientChecksEveryDocumentedScenarioState)
{
    const auto root = cpp_language_root () / "samples/Bingo";
    const auto scenario = read_file (root / "Client/bingo_client_scenario.hpp");
    const auto messages = read_file (root / "Shared/Contracts/messages.hpp");
    const auto room = read_file (
      root
      / "Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo_room_spot.hpp");
    const auto runner = read_file (root / "run_sample.sh");

    EXPECT_NE (scenario.find ("client1_joined.state.players"), std::string::npos)
      << "SMP-CP-34 step 5 must validate the player records carried by the join push";
    EXPECT_NE (messages.find ("int wins"), std::string::npos)
      << "Bingo player state must carry the wins loaded during actor join";
    EXPECT_NE (messages.find ("int losses"), std::string::npos)
      << "Bingo player state must carry the losses loaded during actor join";
    EXPECT_NE (scenario.find ("player.wins == 0 && player.losses == 0"), std::string::npos)
      << "SMP-CP-34 step 5 must validate the loaded record values";
    EXPECT_NE (scenario.find ("client1_card.state.players"), std::string::npos)
      << "SMP-CP-34 step 7 must validate both submitted cards in the second response";
    EXPECT_NE (scenario.find ("same_bingo_room_state (client1_drawn.state, client2_drawn.state)"),
               std::string::npos)
      << "SMP-CP-34 step 8 must compare the complete draw state from both pushes";
    EXPECT_NE (scenario.find ("same_bingo_player_list (client1_ended.state.players"),
               std::string::npos)
      << "SMP-CP-34 step 9 must compare the final player lists from both pushes";
    EXPECT_NE (room.find ("observer returned to entry spot"), std::string::npos)
      << "SMP-CP-34 step 11 must leave server evidence after the observer room leave";
    EXPECT_NE (runner.find ("observer returned to entry spot"), std::string::npos)
      << "SMP-CP-34 runner must require the observer leave evidence";
}

TEST (CppFrameworkSampleParity, BingoWireOmitsTransportIdentityNotification)
{
    const auto contracts = read_file (
      cpp_language_root () / "samples/Bingo/Shared/Contracts/bingo_messages.proto");
    EXPECT_EQ (contracts.find ("BingoActorEntrySpotNotify"), std::string::npos)
      << "Bingo wire must not expose the framework-internal target node routing id";
}

TEST (CppFrameworkSampleParity, BingoRoomGameCopyOwnsItsPlayerState)
{
    using namespace zlink::samples::bingo;

    bingo_room_game_t room ("copy-room");
    (void) room.join ("player-1", "Player 1");
    (void) room.join ("player-2", "Player 2");
    auto projected = room;

    (void) projected.submit_card ("player-1", {1, 2, 3, 4, 5, 6, 7, 8, 9});

    EXPECT_TRUE (room.snapshot ().players[0].card.empty ());
    EXPECT_EQ (projected.snapshot ().players[0].card.size (), 9U);
}

TEST (CppFrameworkSampleParity, BingoRoomIdsAreUniqueAcrossAllocatorInstances)
{
    using namespace zlink::samples::bingo;

    class accepting_match_queue_t final : public bingo_match_queue_t
    {
      public:
        bingo_match_reservation_t reserve (const std::string &,
                                           const std::string &,
                                           const std::string &preferred_owner_node_rid,
                                           const std::string &new_room_id,
                                           int) override
        {
            return {new_room_id, preferred_owner_node_rid, true};
        }
    } first_queue, second_queue;

    bingo_room_allocator_t first (first_queue);
    bingo_room_allocator_t second (second_queue);
    const auto first_room = first.allocate ("two-player", "player-1", "play-a");
    const auto second_room = second.allocate ("two-player", "player-2", "play-b");

    EXPECT_NE (first_room.room_id, second_room.room_id)
      << "independent Play processes must not allocate the same spot routing id";
}

TEST (CppFrameworkSampleParity, BingoRewardSubscriptionDoesNotDriveRoomCleanup)
{
    const auto handler = read_file (
      cpp_language_root ()
      / "samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/"
        "bingo_reward_acquired_event_handler.hpp");

    EXPECT_EQ (handler.find ("leave_finished_actors"), std::string::npos);
    EXPECT_NE (handler.find ("bingo_reward_announced_notify_t"), std::string::npos);
}

TEST (CppFrameworkSampleParity, BingoRoomClosesAfterItsLastActorLeaves)
{
    const auto room = read_file (
      cpp_language_root ()
      / "samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/"
        "bingo_room_spot.hpp");

    EXPECT_NE (room.find ("if (actors.empty () && observers.empty ())"), std::string::npos)
      << "Bingo room must close only after both player and observer occupancy are empty";
    EXPECT_NE (room.find ("co_await _context.close ()"), std::string::npos)
      << "Bingo room must request spot closure after its last actor leaves";
}

TEST (CppFrameworkSampleParity, DomainOwnsBingoJoinAndSupportChatTimeoutDecisions)
{
    const auto root = cpp_language_root ();
    const auto bingo_domain = read_file (
      root / "samples/Bingo/Server/Play/Domain/Bingo/bingo_room_game.hpp");
    const auto bingo_spot = read_file (
      root
      / "samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo_room_spot.hpp");
    EXPECT_NE (bingo_domain.find ("bingo_room_join_result_t"), std::string::npos);
    EXPECT_NE (bingo_domain.find ("game_started"), std::string::npos);
    EXPECT_NE (bingo_spot.find ("const auto joined = _game.join"), std::string::npos);
    EXPECT_EQ (bingo_spot.find ("state.players.size () == 2"), std::string::npos);

    const auto conversation = read_file (
      root / "samples/SupportChat/Server/Support/Domain/SupportChat/conversation.hpp");
    const auto support = read_file (root / "samples/SupportChat/Server/Support/main.cpp");
    EXPECT_NE (conversation.find ("advance_time"), std::string::npos);
    EXPECT_NE (conversation.find ("_close_deadline_unix_ms"), std::string::npos);
    EXPECT_NE (support.find ("_conversation->advance_time (now_unix_ms ())"),
               std::string::npos);
    EXPECT_EQ (support.find ("now >= state.idle_deadline_unix_ms"), std::string::npos);
    EXPECT_EQ (support.find ("_close_deadline_unix_ms"), std::string::npos);
}

TEST (CppFrameworkSampleParity, SupportChatServingPathUsesAgentAssignmentApplicationService)
{
    const auto root = cpp_language_root ();
    const auto assignment = read_file (
      root
      / "samples/SupportChat/Server/Support/Application/ConversationAssignment/agent_assignment_service.hpp");
    const auto support = read_file (root / "samples/SupportChat/Server/Support/main.cpp");

    EXPECT_NE (assignment.find ("assign_for_conversation"), std::string::npos);
    EXPECT_NE (assignment.find ("release_conversation"), std::string::npos);
    EXPECT_NE (support.find ("_assignment.set_available"), std::string::npos);
    EXPECT_NE (support.find ("_assignment.assign_for_conversation"), std::string::npos);
    EXPECT_EQ (support.find ("_available_agent"), std::string::npos);
}

TEST (CppFrameworkSampleParity, SupportChatSessionRelaysOpenConversationUnchanged)
{
    const auto root = cpp_language_root ();
    const auto messages = read_file (root / "samples/SupportChat/Shared/Contracts/messages.hpp");
    const auto session = read_file (root / "samples/SupportChat/Server/Session/main.cpp");
    const auto support = read_file (root / "samples/SupportChat/Server/Support/main.cpp");

    EXPECT_EQ (session.find ("open_conversation_api_req_t"), std::string::npos);
    EXPECT_EQ (session.find ("open_conversation_req_t{opened.subject"), std::string::npos);
    EXPECT_NE (support.find ("_context.outbound ().request (\"supportchat.api\""),
               std::string::npos);
    EXPECT_NE (support.find ("_runtime.assign_agent (joined.state.conversation_id)"),
               std::string::npos);

    const auto request_start = messages.find ("struct open_conversation_req_t");
    const auto request_end = messages.find ("struct open_conversation_res_t", request_start);
    ASSERT_NE (request_start, std::string::npos);
    ASSERT_NE (request_end, std::string::npos);
    EXPECT_EQ (messages.substr (request_start, request_end - request_start)
                 .find ("conversation_id"),
               std::string::npos);
}

TEST (CppFrameworkSampleParity, TicTacToeUsesDotNetSamplePacketSurface)
{
    using namespace zlink::samples::tictactoe;

    EXPECT_STREQ (sample_names_t::game_state_packet, "GameStateNotify");
    EXPECT_STREQ (sample_names_t::player_joined_packet, "PlayerJoinedNotify");
    EXPECT_STREQ (player_win_milestone_event_t::packet_name, "PlayerWinMilestoneEvent");

    const auto contracts = read_file (
      cpp_language_root () / "samples/TicTacToe/Shared/Contracts/messages.hpp");
    EXPECT_EQ (contracts.find ("GameEndedNotify"), std::string::npos)
      << "the terminal state must use GameStateNotify instead of an extra client push";

    authenticate_player_handler_t auth;
    const auto authenticated = auth.handle ({sample_names_t::x_actor_id});
    ASSERT_TRUE (authenticated.accepted);

    sample_topology_t topology;
    const create_game_res_t created{std::string ("room-1"),
                                    std::string ("tictactoe-game"),
                                    topology.stream_endpoint,
                                    {topology.stream_endpoint, topology.play_b_stream_endpoint},
                                    {{topology.stream_endpoint, topology.play_a_node_rid},
                                     {topology.play_b_stream_endpoint, topology.play_b_node_rid}},
                                    sample_names_t::required_level};
    EXPECT_EQ (created.owner_play_endpoint, topology.stream_endpoint);
    EXPECT_EQ (created.game_name, "tictactoe-game");
    tictactoe_match_t room (created.room_id);
    EXPECT_EQ (room.join (sample_names_t::x_actor_id, created.room_id).state.x_actor_id,
               sample_names_t::x_actor_id);
    EXPECT_EQ (room.join (sample_names_t::o_actor_id, created.room_id).state.status, "InProgress");
    EXPECT_EQ (room.snapshot ().next_turn, tictactoe_marks_t::x);
    const auto first_move = room.place (sample_names_t::x_actor_id, place_mark_req_t{0});
    EXPECT_EQ (first_move.board, "X........");
    EXPECT_EQ (first_move.next_turn, tictactoe_marks_t::o);

    tictactoe_entry_spot_t entry_spot;
    tictactoe_game_spot_t game_spot;

    zlink::framework::spot_context_t game_context;
    game_spot.configure (game_context);
    const auto game_handlers = game_context.handlers ().descriptors ();
    ASSERT_EQ (game_handlers.size (), 2U);
    EXPECT_EQ (game_handlers[0].kind, zlink::framework::spot_handler_kind_t::actor_request);
    EXPECT_EQ (game_handlers[0].packet_name, place_mark_req_t::packet_name);
    EXPECT_EQ (game_handlers[1].packet_name, leave_game_req_t::packet_name);

    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 3U);
    EXPECT_EQ (entry_handlers[0].packet_name, join_game_req_t::packet_name);
    EXPECT_EQ (entry_handlers[1].packet_name, observe_milestone_req_t::packet_name);
    EXPECT_EQ (entry_handlers[2].kind, zlink::framework::spot_handler_kind_t::subscription);
}

TEST (CppFrameworkSampleParity, TicTacToeSeparatesInternalRoomJoinResponse)
{
    const auto contracts = read_file (
      cpp_language_root () / "samples/TicTacToe/Shared/Contracts/messages.hpp");
    const auto handler = read_file (
      cpp_language_root ()
      / "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/play_actor_join_game_handler.hpp");
    EXPECT_NE (contracts.find ("struct tictactoe_game_join_res_t"), std::string::npos);
    EXPECT_NE (handler.find ("async<tictactoe_game_join_res_t>"), std::string::npos)
      << "room admission must decode the documented internal response, not JoinGameRes";
}

TEST (CppFrameworkSampleParity, TicTacToeUsesFrameworkOwnedSpotLocationResolution)
{
    const auto host_factory = read_file (
      cpp_language_root () / "samples/TicTacToe/Server/Play/play_server_host_factory.hpp");

    EXPECT_EQ (host_factory.find ("add_spot_resolver"), std::string::npos)
      << "TicTacToe must use the framework location runtime instead of registering an "
         "application spot resolver";
}

TEST (CppFrameworkSampleParity, DocumentedSampleRolesDoNotBuildProbeProcesses)
{
    const auto cmake = read_file (cpp_language_root () / "CMakeLists.txt");
    const auto support_runner =
      read_file (cpp_language_root () / "samples/SupportChat/run_sample.sh");
    const auto delivery_runner =
      read_file (cpp_language_root () / "samples/DeliveryDispatch/run_sample.sh");

    EXPECT_EQ (cmake.find ("supportchat_probe"), std::string::npos);
    EXPECT_EQ (cmake.find ("deliverydispatch_probe"), std::string::npos);
    EXPECT_EQ (support_runner.find ("supportchat_probe"), std::string::npos);
    EXPECT_EQ (delivery_runner.find ("deliverydispatch_probe"), std::string::npos);
    EXPECT_FALSE (std::filesystem::exists (
      cpp_language_root () / "samples/SupportChat/Probe/main.cpp"));
    EXPECT_FALSE (std::filesystem::exists (
      cpp_language_root () / "samples/DeliveryDispatch/Probe/main.cpp"));
}

TEST (CppFrameworkSampleParity, TicTacToeTurnTimeoutIsADomainTerminalState)
{
    using namespace zlink::samples::tictactoe;

    tictactoe_match_t match ("timeout-room", std::chrono::steady_clock::duration::zero ());
    (void) match.join ("player-x", "timeout-room");
    (void) match.join ("player-o", "timeout-room");

    ASSERT_TRUE (match.tick ());
    const auto &state = match.snapshot ();
    EXPECT_EQ (state.status, tictactoe_status_t::turn_timed_out);
    EXPECT_EQ (state.winner, "player-o");
    EXPECT_EQ (state.last_move_actor_id, "player-x");
    EXPECT_FALSE (state.last_move_cell.has_value ());
    EXPECT_TRUE (state.next_turn.empty ());
    EXPECT_FALSE (match.tick ());
    EXPECT_NO_THROW (match.ensure_can_leave ("player-x"));
    EXPECT_NO_THROW (match.ensure_can_leave ("player-o"));
}

TEST (CppFrameworkSampleParity, TicTacToeAdmissionEvaluationDoesNotMutateMatch)
{
    using namespace zlink::samples::tictactoe;

    tictactoe_match_t match ("evaluation-room");
    const auto evaluated = match.evaluate_join ("player-x", "evaluation-room");
    EXPECT_EQ (evaluated.state.x_actor_id, "player-x");
    EXPECT_FALSE (match.snapshot ().x_actor_id.has_value ());

    const auto joined = match.join ("player-x", "evaluation-room");
    EXPECT_EQ (joined.state.x_actor_id, "player-x");
    EXPECT_EQ (match.snapshot ().x_actor_id, "player-x");
}

TEST (CppFrameworkSampleParity, DeliveryDispatchUsesDotNetSampleStatusSurface)
{
    using namespace zlink::samples::deliverydispatch;

    EXPECT_STREQ (delivery_status_changed_req_t::packet_name, "DeliveryStatusChangedReq");
    EXPECT_STREQ (delivery_status_changed_res_t::packet_name, "DeliveryStatusChangedRes");
    EXPECT_STREQ (create_delivery_req_t::packet_name, "CreateDeliveryReq");
    EXPECT_STREQ (create_delivery_res_t::packet_name, "CreateDeliveryRes");
    EXPECT_STREQ (subscribe_delivery_req_t::packet_name, "SubscribeDeliveryReq");
    EXPECT_STREQ (subscribe_delivery_res_t::packet_name, "SubscribeDeliveryRes");
    EXPECT_STREQ (ensure_customer_actor_req_t::packet_name, "EnsureCustomerActorReq");
    EXPECT_STREQ (ensure_customer_actor_res_t::packet_name, "EnsureCustomerActorRes");
    EXPECT_STREQ (bind_courier_req_t::packet_name, "BindCourierReq");
    EXPECT_STREQ (bind_courier_res_t::packet_name, "BindCourierRes");
    EXPECT_STREQ (bind_courier_session_req_t::packet_name, "BindCourierSessionReq");
    EXPECT_STREQ (bind_courier_session_res_t::packet_name, "BindCourierSessionRes");
    EXPECT_STREQ (ensure_courier_actor_req_t::packet_name, "EnsureCourierActorReq");
    EXPECT_STREQ (ensure_courier_actor_res_t::packet_name, "EnsureCourierActorRes");
    /* 공통 sample spec §7.4: 제안과 결정은 응답 없는 one-way send 쌍이다. */
    EXPECT_STREQ (offer_delivery_msg_t::packet_name, "OfferDeliveryMsg");
    EXPECT_STREQ (offer_delivery_result_msg_t::packet_name, "OfferDeliveryResultMsg");
    EXPECT_STREQ (delivery_status_notify_t::packet_name, "DeliveryStatusNotify");
    EXPECT_STREQ (offer_delivery_notify_t::packet_name, "OfferDeliveryNotify");
    EXPECT_STREQ (courier_decision_msg_t::packet_name, "CourierDecisionMsg");
    EXPECT_STREQ (delivery_status_t::assigned, "Assigned");
    EXPECT_STREQ (delivery_status_t::reassigned, "Reassigned");
    EXPECT_STREQ (delivery_status_t::delivered, "Delivered");

    const auto status_wire = nlohmann::json (
      delivery_status_notify_t{"delivery-1", delivery_status_t::assigned, "courier-1",
                               "2026-07-15T00:00:00Z"});
    EXPECT_EQ (status_wire.at ("status"), "Assigned");

    const auto actor_snapshot = actor_ref_snapshot_t{
      .node_rid = zlink::framework::node_rid_t::from_string ("courier-node"),
      .actor_id = "courier-1",
      .generation = 7};
    const auto bind_wire = nlohmann::json (
      bind_courier_session_res_t{"courier-1", actor_snapshot, "courier-route"});
    EXPECT_EQ (bind_wire.at ("actor").at ("nodeRid"), "courier-node");
    EXPECT_EQ (bind_wire.at ("actor").at ("actorId"), "courier-1");
    EXPECT_EQ (bind_wire.at ("actor").at ("generation"), 7);

    const auto changed_wire = nlohmann::json (delivery_status_changed_req_t{
      "delivery-1", "customer-2", delivery_status_t::assigned, "courier-1",
      "2026-07-15T00:00:00Z"});
    EXPECT_EQ (changed_wire.at ("customerId"), "customer-2");

    const auto common_doc = read_file (
      repository_root () / "framework/doc/framework/common/sample/deliverydispatch/README.ko.md");
    for (const auto *message : {"CreateDeliveryReq",
                                "CreateDeliveryRes",
                                "SubscribeDeliveryReq",
                                "SubscribeDeliveryRes",
                                "BindCourierSessionReq",
                                "BindCourierSessionRes",
                                "BindCourierReq",
                                "BindCourierRes",
                                "EnsureCourierActorReq",
                                "EnsureCourierActorRes",
                                "OfferDeliveryMsg",
                                "OfferDeliveryResultMsg",
                                "EnsureCustomerActorReq",
                                "EnsureCustomerActorRes",
                                "DeliveryStatusChangedReq",
                                "DeliveryStatusChangedRes"}) {
        EXPECT_NE (common_doc.find (std::string ("`") + message + "`"), std::string::npos)
          << "common DeliveryDispatch doc must use live .NET/C++ message name " << message;
    }
    for (const auto *stale_message : {"CreateDeliveryRequest",
                                      "CreateDeliveryResponse",
                                      "SubscribeDeliveryAccepted",
                                      "OfferDeliveryReq`",
                                      "CourierBound",
                                      "CourierActorEnsured",
                                      "CustomerActorEnsured",
                                      "DeliveryStatusAck",
                                      "SubscribeCustomerToDeliveryReq",
                                      "SubscribeCustomerToDeliveryRes",
                                      "AssignDeliveryRes",
                                      "ReassignDeliveryMsg",
                                      "DeliverySpotCreateReq",
                                      "DeliverySpotCreateReqRes",
                                      "DeliverySpotJoinReq",
                                      "DeliverySpotJoinReqRes"}) {
        EXPECT_EQ (common_doc.find (stale_message), std::string::npos)
          << "common DeliveryDispatch doc still contains stale message name " << stale_message;
    }

    const auto shared_contract =
      read_file (cpp_language_root () / "samples/DeliveryDispatch/Shared/Contracts/messages.hpp");
    const auto tracking_handler = read_file (
      cpp_language_root ()
      / "samples/DeliveryDispatch/Server/Tracking/Handlers/tracking_handlers.hpp");
    EXPECT_EQ (tracking_handler.find ("sample_names_t::customer_id"), std::string::npos)
      << "Tracking must route each status update with DeliveryStatusChangedReq.customerId";
    for (const auto *extra_message : {"OfferDeliveryReq\"",
                                      "SubscribeCustomerToDeliveryReq",
                                      "SubscribeCustomerToDeliveryRes",
                                      "AssignDeliveryRes",
                                      "ReassignDeliveryMsg",
                                      "DeliverySpotCreateReq",
                                      "DeliverySpotCreateReqRes",
                                      "DeliverySpotJoinReq",
                                      "DeliverySpotJoinReqRes"}) {
        EXPECT_EQ (shared_contract.find (extra_message), std::string::npos)
          << "C++ DeliveryDispatch shared sample contract must not expose non-.NET message name "
          << extra_message;
    }
}

TEST (CppFrameworkSampleParity, GameQuestUsesFlatOneWayGameplayMessage)
{
    using namespace zlink::samples::gamequest;

    const auto wire = nlohmann::json (gameplay_msg_t{
      "event-1", "player-1", "MonsterKilled", std::vector<std::uint8_t>{1, 2, 3}, 42});
    EXPECT_EQ (wire.at ("eventId"), "event-1");
    EXPECT_EQ (wire.at ("playerId"), "player-1");
    EXPECT_EQ (wire.at ("type"), "MonsterKilled");
    EXPECT_EQ (wire.at ("payload"), nlohmann::json::array ({1, 2, 3}));
    EXPECT_EQ (wire.at ("occurredAtUnixMs"), 42);
    EXPECT_EQ (wire.find ("event"), wire.end ())
      << "GameplayMsg must not wrap a private gameplay envelope";

    const auto contracts =
      read_file (cpp_language_root () / "samples/GameQuest/Shared/Contracts/messages.hpp");
    EXPECT_EQ (contracts.find ("ApplyGameplayEventReq"), std::string::npos)
      << "entry-to-owner gameplay is the one-way GameplayMsg, not a parallel request";
    EXPECT_NE (contracts.find ("lastSourceEventId"), std::string::npos);
    EXPECT_NE (contracts.find ("\"version\""), std::string::npos);
}

TEST (CppFrameworkSampleParity, GameQuestProgressChecksRejectOvercount)
{
    const auto scenario = read_file (
      cpp_language_root () / "samples/GameQuest/Client/gamequest_client_scenario.hpp");
    EXPECT_EQ (scenario.find ("progress.current_count >= current_count"), std::string::npos)
      << "GameQuest idempotency checks must reject duplicated progress";
    EXPECT_NE (scenario.find ("progress.current_count == current_count"), std::string::npos)
      << "GameQuest progress gates must compare the exact expected count";
}

TEST (CppFrameworkSampleParity, BingoAndGameQuestStartAfterObservedReadiness)
{
    const auto bingo_runner =
      read_file (cpp_language_root () / "samples/Bingo/run_sample.sh");
    const auto gamequest_runner =
      read_file (cpp_language_root () / "samples/GameQuest/run_sample.sh");
    EXPECT_EQ (bingo_runner.find ("BINGO_STARTUP_SETTLE_SECONDS"), std::string::npos)
      << "Bingo must start its client immediately after endpoint readiness";
    EXPECT_EQ (gamequest_runner.find ("GAMEQUEST_CPP_STARTUP_SETTLE_SECONDS"),
               std::string::npos)
      << "GameQuest must not use a fixed delay as topology readiness";
}

TEST (CppFrameworkSampleParity, TicTacToeDisconnectRemovesMilestoneObserver)
{
    const auto entry = read_file (
      cpp_language_root ()
      / "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/"
        "tictactoe_entry_spot.hpp");
    const auto disconnect = entry.find ("task_t<void> on_disconnect_actor");
    ASSERT_NE (disconnect, std::string::npos);
    const auto callback_end = entry.find ("std::vector<std::string> created_actor_ids", disconnect);
    ASSERT_NE (callback_end, std::string::npos);

    EXPECT_NE (entry.substr (disconnect, callback_end - disconnect).find ("observers.erase"),
               std::string::npos)
      << "disconnect must remove the actor from milestone notification targeting";
}

TEST (CppFrameworkSampleParity, TicTacToeOwnsTurnTimeoutLifecycle)
{
    const auto root = cpp_language_root () / "samples/TicTacToe/Server/Play";
    const auto spot = read_file (
      root / "Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe_game_spot.hpp");
    const auto match = read_file (root / "Domain/TicTacToe/tictactoe_match.hpp");
    const auto messages = read_file (
      cpp_language_root () / "samples/TicTacToe/Shared/Contracts/messages.hpp");

    EXPECT_NE (spot.find ("add_timer<tictactoe_game_timer_handler_t>"), std::string::npos)
      << "game spot must register the turn timeout timer";
    EXPECT_NE (match.find ("tick ("), std::string::npos)
      << "match domain must own timeout state transitions";
    EXPECT_NE (messages.find ("TurnTimedOut"), std::string::npos)
      << "timeout must have the shared terminal status used by the reference sample";
}

TEST (CppFrameworkSampleParity, TicTacToeSpotComposesItsDomainMatch)
{
    const auto spot = read_file (
      cpp_language_root ()
      / "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/"
        "tictactoe_game_spot.hpp");

    EXPECT_EQ (spot.find ("public tictactoe_match_t"), std::string::npos)
      << "framework Spot must not inherit the domain aggregate";
    EXPECT_EQ (spot.find ("static_cast<tictactoe_match_t"), std::string::npos)
      << "Spot creation must not replace the domain through a base-class assignment";
    EXPECT_NE (spot.find ("std::optional<tictactoe_match_t> _match"), std::string::npos)
      << "Spot must own the match through composition";
    EXPECT_NE (spot.find ("match ().evaluate_join"), std::string::npos)
      << "admission must evaluate without mutating the match";
}

TEST (CppFrameworkSampleParity, TicTacToeNotificationPublisherDeliversToRoomActors)
{
    const auto publisher = read_file (
      cpp_language_root ()
      / "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/"
        "TicTacToeGameSpot/Notifications/game_notification_publisher.hpp");

    EXPECT_NE (publisher.find ("bound_session ().send"), std::string::npos)
      << "the notification publisher must deliver through each room actor session";
    EXPECT_EQ (publisher.find ("std::vector"), std::string::npos)
      << "the notification publisher must not retain an unread parallel event history";
}

TEST (CppFrameworkSampleParity, DeliveryDispatchTrackingHasNoDeadSpotModel)
{
    const auto tracking =
      cpp_language_root () / "samples/DeliveryDispatch/Server/Tracking";
    EXPECT_FALSE (std::filesystem::exists (tracking / "Actors"))
      << "Tracking does not own customer actors";
    EXPECT_FALSE (std::filesystem::exists (tracking / "Spots"))
      << "Tracking is a channel server and evidence store, not a spot node";

    const auto main = read_file (tracking / "main.cpp");
    const auto handlers = read_file (tracking / "Handlers/tracking_handlers.hpp");
    EXPECT_NE (main.find ("add_route_mesh"), std::string::npos)
      << "Tracking still needs mesh participation for outbound customer actor delivery";
    EXPECT_EQ (handlers.find ("delivery_spot_directory_t"), std::string::npos)
      << "status handling must not maintain an unread parallel history";
}

TEST (CppFrameworkSampleParity, DeliveryDispatchTrackingUsesActorDirectory)
{
    const auto handler = read_file (
      cpp_language_root ()
      / "samples/DeliveryDispatch/Server/Tracking/Handlers/tracking_handlers.hpp");

    EXPECT_NE (handler.find ("actor_directory_t"), std::string::npos)
      << "Tracking must use the framework actor location abstraction";
    EXPECT_NE (handler.find ("_actor_directory.find"), std::string::npos)
      << "customer actor lookup must go through actor_directory_t";
    EXPECT_EQ (handler.find ("request_to_spot"), std::string::npos)
      << "Tracking must not hand-build a blocking CustomerEntry lookup hop";
    EXPECT_EQ (handler.find ("find_customer_actor_req_t"), std::string::npos)
      << "Tracking must not depend on the CustomerEntry lookup packet";
}

TEST (CppFrameworkSampleParity, ShoppingMallOwnerSchedulesItsContinuation)
{
    const auto owner = read_file (
      cpp_language_root () / "samples/ShoppingMall/Server/OrderWorkflow/main.cpp");
    const auto edge = read_file (
      cpp_language_root () / "samples/ShoppingMall/Server/CommerceApi/main.cpp");

    EXPECT_NE (owner.find ("schedule_continue"), std::string::npos)
      << "OrderWorkflow owner must schedule its continuation after creating the order";
    EXPECT_EQ (edge.find ("schedule_continue"), std::string::npos)
      << "CommerceApi must not own the OrderWorkflow continuation lifecycle";
}

TEST (CppFrameworkSampleParity, ShoppingMallClientFlowLivesInScenario)
{
    const auto root = cpp_language_root () / "samples/ShoppingMall/Client";
    const auto scenario = read_file (root / "shoppingmall_client_scenario.hpp");
    const auto main = read_file (root / "main.cpp");

    EXPECT_NE (scenario.find ("class shoppingmall_client_scenario_t"), std::string::npos)
      << "ShoppingMall must expose a named client scenario";
    EXPECT_NE (scenario.find ("/orders/start"), std::string::npos)
      << "the scenario must own the actual workflow calls";
    EXPECT_NE (main.find ("shoppingmall_client_scenario_t"), std::string::npos)
      << "the client entrypoint must invoke the scenario";
    EXPECT_EQ (main.find ("/orders/start"), std::string::npos)
      << "the client entrypoint must not retain scenario orchestration";
}

TEST (CppFrameworkSampleParity, SampleRoomJoinHandlersRejectRejectedVariants)
{
    for (const auto &relative : {
           "samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/"
           "match_bingo_actor_handler.hpp",
           "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/Handlers/"
           "play_actor_join_game_handler.hpp",
           "samples/SupportChat/Server/Support/main.cpp"}) {
        const auto path = cpp_language_root () / relative;
        const auto handler = read_file (path);
        EXPECT_EQ (handler.find ("std::visit"), std::string::npos)
          << path << " must not flatten accepted and rejected actor join variants";
        EXPECT_NE (handler.find ("actor_join_accepted_t"), std::string::npos)
          << path << " must explicitly require an accepted actor join";
    }
}

TEST (CppFrameworkSampleParity, SampleHostsUseFrameworkOptionsSurface)
{
    const std::vector<std::string> banned_patterns{"configure_registry_host",
                                                   "configure_api_host",
                                                   "configure_play_host",
                                                   "configure_session_host",
                                                   "app.use_zlink",
                                                   "app.advanced ().zlink",
                                                   "app.advanced().zlink",
                                                   "app.services ()",
                                                   "app.handlers ()",
                                                   "app.advanced ()",
                                                   "service_collection_t",
                                                   "serializer_registry_t",
                                                   "handler_registry_t",
                                                   ".add_factory<",
                                                   ".on_request<",
                                                   ".channel (",
                                                   ".channel(",
                                                   "channel.enable_server",
                                                   "channel.enable_client"};

    for (const auto &path : sample_source_files ()) {
        const auto content = read_file (path);
        for (const auto &pattern : banned_patterns) {
            EXPECT_EQ (content.find (pattern), std::string::npos)
              << path << " contains low-level framework configuration pattern " << pattern;
        }
    }
}

TEST (CppFrameworkSampleParity, PublicSampleNamesDoNotUseVariantSuffixes)
{
    const auto samples_root = cpp_language_root () / "samples";
    const std::vector<std::string> expected_samples{
      "Bingo", "TicTacToe", "DeliveryDispatch", "GameQuest", "SupportChat", "ShoppingMall"};

    for (const auto &sample : expected_samples) {
        EXPECT_TRUE (std::filesystem::is_directory (samples_root / sample))
          << sample << " sample directory is missing";
    }

    for (const auto &entry : std::filesystem::directory_iterator (samples_root)) {
        if (!entry.is_directory ()) {
            continue;
        }
        const auto name = entry.path ().filename ().generic_string ();
        if (name == "Shared") {
            continue;
        }
        EXPECT_TRUE (std::find (expected_samples.begin (), expected_samples.end (), name)
                     != expected_samples.end ())
          << entry.path () << " adds a sample-name variant suffix";
    }
}

TEST (CppFrameworkSampleParity, SharedSampleDirectoryContainsOnlyContracts)
{
    const auto samples_root = cpp_language_root () / "samples";
    for (const auto &sample : {"Bingo", "TicTacToe", "DeliveryDispatch"}) {
        const auto shared_root = samples_root / sample / "Shared";
        ASSERT_TRUE (std::filesystem::is_directory (shared_root)) << shared_root;
        for (const auto &entry : std::filesystem::recursive_directory_iterator (shared_root)) {
            if (!entry.is_regular_file ()) {
                continue;
            }
            const auto relative = std::filesystem::relative (entry.path (), shared_root);
            EXPECT_TRUE (relative.generic_string ().rfind ("Contracts/", 0) == 0)
              << entry.path () << " belongs in a role-specific sample directory";
        }
    }
}

TEST (CppFrameworkSampleParity, ClientSamplesDoNotCallServerHandlersDirectly)
{
    const std::vector<std::string> banned_client_patterns{"../Server/",
                                                          "/Server/",
                                                          "Handlers/",
                                                          "../Shared/E2E/",
                                                          "/Shared/E2E/",
                                                          "zlink/Contracts/Sockets",
                                                          "zlink/Contracts/Service",
                                                          "zlink::context_t",
                                                          "zlink::stream_socket_t",
                                                          "run_client_e2e_stream_server",
                                                          "use_embedded_server"};

    for (const auto &path : sample_source_files ()) {
        const auto relative_path = relative_sample_path (path);
        if (relative_path.find ("/Client/") == std::string::npos) {
            continue;
        }
        const auto content = read_file (path);
        for (const auto &pattern : banned_client_patterns) {
            EXPECT_EQ (content.find (pattern), std::string::npos)
              << path << " makes the client depend on server handler internals via " << pattern;
        }
    }
}

TEST (CppFrameworkSampleParity, JsonFieldAccessStaysInsideDtoSerializers)
{
    const std::vector<std::string> banned_json_patterns{"nlohmann::json::parse", ".at (", ".at(",
                                                        "json["};

    for (const auto &path : sample_source_files ()) {
        const auto relative_path = relative_sample_path (path);
        if (relative_path.find ("/Shared/Contracts/") != std::string::npos) {
            continue;
        }
        const auto content = read_file (path);
        for (const auto &pattern : banned_json_patterns) {
            EXPECT_EQ (content.find (pattern), std::string::npos)
              << path << " reads JSON fields outside DTO serializer hooks via " << pattern;
        }
    }
}

TEST (CppFrameworkSampleParity, SampleReadmesDescribePublicExecutablesAndRunnerScope)
{
    const auto cpp_root = cpp_language_root ();
    const auto cmake = read_file (cpp_root / "CMakeLists.txt");
    struct sample_readme_case_t
    {
        std::string readme_path;
        std::vector<std::string> public_targets;
    };
    const std::vector<sample_readme_case_t> cases{
      {"samples/Bingo/README.ko.md",
       {"sample_cpp_framework_bingo_api", "sample_cpp_framework_bingo_play",
        "sample_cpp_framework_bingo_session", "sample_cpp_framework_bingo_client"}},
      {"samples/TicTacToe/README.ko.md",
       {"sample_cpp_framework_tictactoe_api", "sample_cpp_framework_tictactoe_play",
        "sample_cpp_framework_tictactoe_client"}},
      {"samples/DeliveryDispatch/README.ko.md",
       {"sample_cpp_framework_deliverydispatch_dispatch",
        "sample_cpp_framework_deliverydispatch_courier_actor_node",
        "sample_cpp_framework_deliverydispatch_customer_gateway",
        "sample_cpp_framework_deliverydispatch_courier_session",
        "sample_cpp_framework_deliverydispatch_tracking",
        "sample_cpp_framework_deliverydispatch_client"}}};

    for (const auto &sample : cases) {
        const auto readme = read_file (cpp_root / sample.readme_path);
        for (const auto &target : sample.public_targets) {
            EXPECT_NE (cmake.find (target), std::string::npos)
              << target << " is missing from CMake sample targets";
            EXPECT_NE (readme.find ("`" + target + "`"), std::string::npos)
              << sample.readme_path << " does not document " << target;
        }

        EXPECT_EQ (readme.find ("_e2e_server`"), std::string::npos)
          << sample.readme_path << " should not document internal e2e server "
          << "targets as public sample executables";
        EXPECT_NE (readme.find ("테스트 전용 fake 서버"), std::string::npos)
          << sample.readme_path << " does not document that fake servers stay out of samples";
        EXPECT_NE (readme.find ("client scenario"), std::string::npos)
          << sample.readme_path << " does not document client scenario evidence";
        EXPECT_NE (readme.find ("full client/server"), std::string::npos)
          << sample.readme_path << " does not document current runner scope";
    }

    const auto tictactoe_readme = read_file (cpp_root / "samples/TicTacToe/README.ko.md");
    EXPECT_NE (tictactoe_readme.find ("HTTP `POST /games`"), std::string::npos);
    EXPECT_NE (tictactoe_readme.find ("`zlink::http_client`"), std::string::npos);
    EXPECT_NE (tictactoe_readme.find ("`POST /games`를 호출"), std::string::npos);

    const auto top_level_readme = read_file (cpp_root / "samples/README.ko.md");
    EXPECT_NE (top_level_readme.find ("client self-check"), std::string::npos)
      << "C++ sample overview must describe full self-check scope";
    EXPECT_NE (top_level_readme.find ("samples/TicTacToe/run_sample.sh"), std::string::npos)
      << "C++ sample overview must name the TicTacToe full self-check";
    EXPECT_NE (top_level_readme.find ("samples/Bingo/run_sample.sh"), std::string::npos)
      << "C++ sample overview must name the Bingo full self-check";
    EXPECT_NE (top_level_readme.find ("DeliveryDispatch 샘플은 현재 Linux 또는 WSL용"),
               std::string::npos)
      << "C++ sample overview must describe DeliveryDispatch runner availability";

    const auto tictactoe_runner = read_file (cpp_root / "samples/TicTacToe/run_sample.sh");
    EXPECT_NE (tictactoe_runner.find ("full client/server self-check completed"), std::string::npos)
      << "TicTacToe runner must report the public client/server self-check";
    EXPECT_NE (tictactoe_runner.find ("observer-win-milestone=verified"), std::string::npos)
      << "TicTacToe runner must verify observer milestone delivery";
    EXPECT_NE (tictactoe_runner.find ("\n\"$CLIENT_BIN\""), std::string::npos)
      << "TicTacToe runner must execute the public client binary";
    EXPECT_EQ (tictactoe_runner.find ("full e2e completed"), std::string::npos)
      << "TicTacToe runner should name the specific client/server self-check, not a broad e2e";

    const auto bingo_runner = read_file (cpp_root / "samples/Bingo/run_sample.sh");
    EXPECT_EQ (bingo_runner.find ("full e2e completed"), std::string::npos)
      << "Bingo runner must not claim full e2e completion";
    EXPECT_NE (bingo_runner.find ("full client/server self-check completed"), std::string::npos)
      << "Bingo runner must report the public client/server self-check";
    EXPECT_NE (bingo_runner.find ("\n\"$CLIENT_BIN\""), std::string::npos)
      << "Bingo runner must execute the public client binary";
    EXPECT_NE (bingo_runner.find ("BINGO_REDIS_ENDPOINT"), std::string::npos)
      << "Bingo runner must support externally supplied Redis";

    const auto deliverydispatch_runner =
      read_file (cpp_root / "samples/DeliveryDispatch/run_sample.sh");
    EXPECT_NE (deliverydispatch_runner.find ("deliverydispatch-reassignment=completed"),
               std::string::npos)
      << "DeliveryDispatch runner must verify timeout reassignment";
    EXPECT_NE (deliverydispatch_runner.find ("deliverydispatch-server-evidence=completed"),
               std::string::npos)
      << "DeliveryDispatch runner must verify server evidence self-check";
    EXPECT_NE (deliverydispatch_runner.find ("deliverydispatch=completed"), std::string::npos)
      << "DeliveryDispatch runner must verify final client scenario completion";
    EXPECT_EQ (deliverydispatch_runner.find ("delivery-dispatch e2e result"), std::string::npos)
      << "DeliveryDispatch sample runner must not report e2e completion wording";

    const auto courier_actor_node =
      read_file (cpp_root / "samples/DeliveryDispatch/Server/CourierActorNode/main.cpp");
    /* 공통 sample spec §7.2: courier actor는 이 노드의 entry spot이 route 요청으로 찾고 만들고
     * offer한다. 별도 gateway나 per-node client-server 채널을 두지 않는다. */
    EXPECT_NE (courier_actor_node.find ("add_handler<&courier_entry_spot_t::find_courier_actor>"),
               std::string::npos)
      << "DeliveryDispatch CourierActorNode must answer FindCourierActorReq on its entry spot";
    EXPECT_NE (courier_actor_node.find ("add_handler<&courier_entry_spot_t::ensure_courier_actor>"),
               std::string::npos)
      << "DeliveryDispatch CourierActorNode must answer EnsureCourierActorReq on its entry spot";
    EXPECT_NE (courier_actor_node.find ("add_handler<&courier_entry_spot_t::offer_delivery_route>"),
               std::string::npos)
      << "DeliveryDispatch CourierActorNode must relay OfferDeliveryMsg on its entry spot";
    EXPECT_EQ (courier_actor_node.find ("enable_server"), std::string::npos)
      << "DeliveryDispatch CourierActorNode must be reached through the spot mesh, not a per-node "
         "client-server channel";
    /* 공통 sample spec §7.4: 노드는 배송원의 결정을 기다리지 않는다. 결정이 오면 배차 채널로
     * one-way로 돌려보낼 뿐이다. */
    EXPECT_EQ (courier_actor_node.find ("condition_variable"), std::string::npos)
      << "DeliveryDispatch CourierActorNode must not wait for the courier decision";
    EXPECT_NE (courier_actor_node.find ("offer_delivery_result_msg_t"), std::string::npos)
      << "DeliveryDispatch CourierActorNode must send the courier decision back to the dispatch "
         "channel";

    const auto dispatch = read_file (cpp_root / "samples/DeliveryDispatch/Server/Dispatch/main.cpp");
    EXPECT_NE (dispatch.find ("class dispatch_state_t"), std::string::npos)
      << "DeliveryDispatch Dispatch must record the offer state instead of awaiting the decision";
    EXPECT_NE (dispatch.find ("class dispatch_worker_t"), std::string::npos)
      << "DeliveryDispatch Dispatch must run the dispatch worker beside its HTTP edge";
    EXPECT_NE (dispatch.find ("class courier_selection_policy_t"), std::string::npos)
      << "DeliveryDispatch Dispatch worker must own the courier selection policy";
    EXPECT_NE (dispatch.find ("class offer_deadline_sweeper_t"), std::string::npos)
      << "DeliveryDispatch Dispatch worker must own the offer deadline and sweep expired offers";
}

TEST (CppFrameworkSampleParity, CommonSampleSpecsDocumentActorDestroyLifecycle)
{
    const auto root = repository_root ();
    const std::vector<std::string> common_specs{
      "framework/doc/framework/common/sample/bingo/README.ko.md",
      "framework/doc/framework/common/sample/tictactoe/README.ko.md"};

    for (const auto &spec_path : common_specs) {
        const auto spec = read_file (root / spec_path);
        EXPECT_NE (spec.find ("`onCreateActor`를 한 번 호출"), std::string::npos)
          << spec_path << " must document actor creation lifecycle";
        EXPECT_NE (spec.find ("`leaveActor`로 actor를 room에서 내보낸다"), std::string::npos)
          << spec_path << " must document room leave before destroy";
        EXPECT_NE (spec.find ("Entry Spot context의 `destroyActor`를 호출한다"), std::string::npos)
          << spec_path << " must document Entry Spot-owned destroy";
        EXPECT_NE (spec.find ("`destroyActor`는 `onLeaveActor`나 다른 lifecycle callback을 "
                              "호출하지 않고"),
                   std::string::npos)
          << spec_path << " must document destroy callback isolation";
        EXPECT_NE (spec.find ("disconnect cleanup만으로 actor destroy가 실행되지 않는다"),
                   std::string::npos)
          << spec_path << " must document disconnect isolation";
        EXPECT_NE (spec.find ("actor를 즉시 destroy하지 않는다"), std::string::npos)
          << spec_path << " must keep disconnect separate from actor lifetime";
    }
}

TEST (CppFrameworkSampleParity, BingoUsesProtobufCodecSurface)
{
    const auto bingo_root = cpp_language_root () / "samples/Bingo";
    const auto readme = read_file (bingo_root / "README.ko.md");
    const auto inventory = read_file (bingo_root / "sample-porting-inventory.ko.md");
    const auto common_codecs = read_file (bingo_root / "Server/common_codecs.hpp");
    const auto session = read_file (bingo_root / "Server/Session/Sessions/bingo_session.hpp");
    const auto client = read_file (bingo_root / "Client/main.cpp");

    EXPECT_NE (readme.find ("Protobuf codec extension"), std::string::npos)
      << "Bingo README must describe the Protobuf codec path";
    EXPECT_EQ (readme.find ("framework 기본 JSON codec"), std::string::npos)
      << "Bingo README must not claim JSON payloads";
    EXPECT_NE (inventory.find ("Protobuf codec extension"), std::string::npos)
      << "Bingo inventory must record the Protobuf codec path";
    EXPECT_EQ (inventory.find ("framework 기본 JSON codec"), std::string::npos)
      << "Bingo inventory must not mark JSON codec parity as done";
    EXPECT_NE (common_codecs.find ("#include <zlink/codecs/protobuf.hpp>"), std::string::npos);
    EXPECT_NE (common_codecs.find ("protobuf_codec_extension_t::register_payload_serializer"),
               std::string::npos)
      << "Bingo framework payloads must be registered with the Protobuf codec extension";
    EXPECT_EQ (session.find ("stream_codec_t"), std::string::npos)
      << "Bingo session code must not select the framework-owned bound stream codec";
    EXPECT_NE (client.find (".codecs ().use (zlink::framework_codecs::protobuf ())"),
               std::string::npos)
      << "Bingo client connectors must enable the Protobuf stream codec";
}

TEST (CppFrameworkSampleParity, TicTacToeInventoryAndRunnersMatchCommonRedisContract)
{
    const auto tictactoe_root = cpp_language_root () / "samples/TicTacToe";
    const auto inventory = read_file (tictactoe_root / "sample-porting-inventory.ko.md");
    const auto shell_runner = read_file (tictactoe_root / "run_sample.sh");
    const auto powershell_runner = read_file (tictactoe_root / "run_sample.ps1");
    const auto readme = read_file (tictactoe_root / "README.ko.md");

    EXPECT_NE (inventory.find (".NET: Client/TicTacToeClientScenario.cs"), std::string::npos)
      << "TicTacToe inventory must map the .NET client scenario";
    EXPECT_NE (inventory.find ("common: 2 API, 2 Play 수동 endpoint scale-out"),
               std::string::npos)
      << "TicTacToe inventory must record the common scale-out requirement";
    EXPECT_NE (inventory.find ("common: runner가 Docker Redis 준비"),
               std::string::npos)
      << "TicTacToe inventory must record the runner-owned Redis contract";
    EXPECT_EQ (inventory.find ("| pending |"), std::string::npos)
      << "TicTacToe inventory must not leave pending rows";
    EXPECT_EQ (inventory.find ("| gap |"), std::string::npos)
      << "TicTacToe inventory must not leave unresolved gaps";

    for (const auto *runner_content : {&shell_runner, &powershell_runner}) {
        EXPECT_NE (runner_content->find ("docker"), std::string::npos)
          << "TicTacToe runners must provision their own Redis container";
        EXPECT_NE (runner_content->find ("TICTACTOE_CPP_REDIS_KEY_PREFIX"),
                   std::string::npos)
          << "TicTacToe runners must pass an isolated Redis key prefix";
        EXPECT_NE (runner_content->find ("api-b"), std::string::npos)
          << "TicTacToe runners must launch a second API role";
        EXPECT_NE (runner_content->find ("play-b"), std::string::npos)
          << "TicTacToe runners must launch a second Play role";
        EXPECT_NE (runner_content->find ("observer-win-milestone=verified"),
                   std::string::npos)
          << "TicTacToe runners must verify observer milestone delivery";
        EXPECT_NE (runner_content->find ("tictactoe=completed"), std::string::npos)
          << "TicTacToe runners must verify the final client marker";
    }

    EXPECT_NE (readme.find ("전용 Redis Docker container"), std::string::npos)
      << "TicTacToe README must document the runner-owned Redis container";
}

TEST (CppFrameworkSampleParity, TicTacToeClientGateChecksCommonContractFields)
{
    const auto client = read_file (
      cpp_language_root () / "samples/TicTacToe/Client/tictactoe_client_scenario.hpp");

    EXPECT_NE (client.find ("room.play_nodes.size () == room.play_endpoints.size ()"),
               std::string::npos);
    EXPECT_NE (client.find ("client1_auth.player.level >= room.required_level"),
               std::string::npos);
    EXPECT_NE (client.find ("client2_auth.player.level >= room.required_level"),
               std::string::npos);
    EXPECT_NE (client.find ("client1_saw_client2_join.display_name"), std::string::npos);
    EXPECT_NE (client.find ("client1_saw_client2_join.level"), std::string::npos);
    EXPECT_NE (client.find ("client1_saw_client2_join.state.status"), std::string::npos);
    EXPECT_NE (client.find ("milestone.display_name == client1_auth.player.display_name"),
               std::string::npos);
    for (const auto *required : {
           "client1_first_move.state.board == \"X........\"",
           "client1_first_move.state.next_turn == tictactoe_marks_t::o",
           "same_state (client2_saw_first_move.state, client1_first_move.state)",
           "client2_first_move.state.board == \"X..O.....\"",
           "client2_first_move.state.next_turn == tictactoe_marks_t::x",
           "same_state (client1_saw_first_o_move.state, client2_first_move.state)",
           "client1_second_move.state.board == \"XX.O.....\"",
           "client1_second_move.state.next_turn == tictactoe_marks_t::o",
           "same_state (client2_saw_second_x_move.state, client1_second_move.state)",
           "client2_second_move.state.board == \"XX.OO....\"",
           "client2_second_move.state.next_turn == tictactoe_marks_t::x",
           "same_state (client1_saw_second_o_move.state, client2_second_move.state)"}) {
        EXPECT_NE (client.find (required), std::string::npos) << required;
    }
}

TEST (CppFrameworkSampleParity, DeliveryDispatchClientGateChecksStatusArrivalOrder)
{
    const auto client = read_file (
      cpp_language_root () / "samples/DeliveryDispatch/Client/delivery_dispatch_client_scenario.hpp");

    EXPECT_NE (client.find ("wait_for_sequence<delivery_status_notify_t>"), std::string::npos);
    EXPECT_NE (client.find ("message.status == delivery_status_t::assigned"), std::string::npos);
    EXPECT_NE (client.find ("message.status == delivery_status_t::reassigned"), std::string::npos);
    EXPECT_NE (client.find ("message.status == delivery_status_t::accepted"), std::string::npos);
    EXPECT_NE (client.find ("message.status == delivery_status_t::picked_up"), std::string::npos);
    EXPECT_NE (client.find ("message.status == delivery_status_t::delivered"), std::string::npos);
    EXPECT_EQ (client.find ("wait_status_sequence"), std::string::npos);
    EXPECT_EQ (client.find ("sleep_for"), std::string::npos);
    EXPECT_EQ (client.find ("wait_status ("), std::string::npos);
}

TEST (CppFrameworkSampleParity, CoroutineSampleWaitsDoNotBlockConnectorDelivery)
{
    const auto cpp_root = cpp_language_root ();
    for (const auto *path : {
           "samples/Bingo/Client/bingo_client_scenario.hpp",
           "samples/TicTacToe/Client/tictactoe_client_scenario.hpp"}) {
        const auto client = read_file (cpp_root / path);
        EXPECT_EQ (client.find (".to_future ("), std::string::npos)
          << path << " must keep connector waits as awaitable tasks";
        EXPECT_EQ (client.find (".get ()"), std::string::npos)
          << path << " must not block the shared connector delivery runner";
    }
}

TEST (CppFrameworkSampleParity, SampleActorDestroyFlowStaysInEntrySpot)
{
    const auto cpp_root = cpp_language_root ();
    const auto stream_host =
      read_file (cpp_root / "framework/src/runtime/streams/stream_host_service.cpp");
    EXPECT_NE (stream_host.find ("session_actor_manager_access_t::disconnect"),
               std::string::npos)
      << "framework stream host must detach session actor bindings on disconnect";
    struct sample_lifecycle_case_t
    {
        std::string entry_spot_path;
        std::string user_spot_path;
        std::string user_handler_path;
        std::string actor_path;
        std::string session_path;
        std::string readme_path;
        std::string runner_path;
    };
    const std::vector<sample_lifecycle_case_t> cases{
      {"samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo_entry_spot.hpp",
       "samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo_room_spot.hpp",
       "",
       "samples/Bingo/Server/Play/Infrastructure/ZLink/Actors/player_actor.hpp",
       "samples/Bingo/Server/Session/Sessions/bingo_session.hpp", "samples/Bingo/README.ko.md",
       "samples/Bingo/run_sample.sh"},
      {"samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/"
       "tictactoe_entry_spot.hpp",
       "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/"
       "tictactoe_game_spot.hpp",
       "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/Handlers/"
       "play_actor_leave_game_handler.hpp",
       "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Actors/player_actor.hpp",
       "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Sessions/play_session.hpp",
       "samples/TicTacToe/README.ko.md", "samples/TicTacToe/run_sample.sh"}};

    for (const auto &sample : cases) {
        const auto entry = read_file (cpp_root / sample.entry_spot_path);
        auto user = read_file (cpp_root / sample.user_spot_path);
        if (!sample.user_handler_path.empty ()) {
            user += read_file (cpp_root / sample.user_handler_path);
        }
        const auto actor = read_file (cpp_root / sample.actor_path);
        const auto session = read_file (cpp_root / sample.session_path);
        const auto readme = read_file (cpp_root / sample.readme_path);
        const auto runner = read_file (cpp_root / sample.runner_path);

        EXPECT_NE (entry.find ("on_create_actor"), std::string::npos)
          << sample.entry_spot_path << " must show actor creation callback";
        EXPECT_NE (entry.find ("on_actor_joined"), std::string::npos)
          << sample.entry_spot_path << " must show Entry Spot re-entry callback";
        EXPECT_NE (entry.find ("on_leave_actor"), std::string::npos)
          << sample.entry_spot_path << " must show Entry Spot leave callback";
        EXPECT_NE (entry.find ("on_disconnect_actor"), std::string::npos)
          << sample.entry_spot_path << " must show Entry Spot disconnect callback";
        EXPECT_NE (entry.find (".destroy_actor ("), std::string::npos)
          << sample.entry_spot_path << " must destroy actors from Entry Spot context";
        EXPECT_NE (entry.find ("destroy_after_entry_spot_join"), std::string::npos)
          << sample.entry_spot_path << " must guard destroy after Entry Spot re-entry";
        EXPECT_NE (entry.find ("mark_disconnected"), std::string::npos)
          << sample.entry_spot_path << " must mark actor disconnect state";
        EXPECT_NE (user.find (".leave_actor ("), std::string::npos)
          << sample.user_spot_path << " must return actors to Entry Spot with leave_actor";
        EXPECT_NE (user.find ("on_disconnect_actor"), std::string::npos)
          << sample.user_spot_path << " must show user Spot disconnect callback";
        EXPECT_NE (user.find ("mark_for_destroy_after_room_leave"), std::string::npos)
          << sample.user_spot_path << " must mark destroy intent before leave_actor";
        EXPECT_NE (user.find ("mark_disconnected"), std::string::npos)
          << sample.user_spot_path << " must mark actor disconnect state";
        EXPECT_EQ (user.find ("destroy_actor"), std::string::npos)
          << sample.user_spot_path << " must not destroy actors from user Spot";
        EXPECT_NE (actor.find ("destroy_after_entry_spot_join"), std::string::npos)
          << sample.actor_path << " must hold destroy-after-entry-join state";
        EXPECT_NE (actor.find ("mark_for_destroy_after_room_leave"), std::string::npos)
          << sample.actor_path << " must expose room cleanup destroy marker";
        EXPECT_NE (actor.find ("mark_disconnected"), std::string::npos)
          << sample.actor_path << " must expose disconnect cleanup state";
        EXPECT_NE (session.find ("on_disconnected"), std::string::npos)
          << sample.session_path << " must implement session disconnect cleanup";
        EXPECT_EQ (session.find ("unbind_session"), std::string::npos)
          << sample.session_path << " must leave binding cleanup to the framework stream host";
        EXPECT_EQ (session.find ("leave_actor"), std::string::npos)
          << sample.session_path << " must not leave rooms on disconnect";
        EXPECT_EQ (session.find ("destroy_actor"), std::string::npos)
          << sample.session_path << " must not destroy actors on disconnect";

        EXPECT_NE (readme.find ("`on_create_actor`"), std::string::npos)
          << sample.readme_path << " must document actor creation callback";
        EXPECT_NE (readme.find ("`leave_actor`"), std::string::npos)
          << sample.readme_path << " must document room leave responsibility";
        EXPECT_NE (readme.find ("`destroy_actor`"), std::string::npos)
          << sample.readme_path << " must document Entry Spot destroy responsibility";
        EXPECT_NE (readme.find ("추가 `on_leave_actor`가 없음을 확인"), std::string::npos)
          << sample.readme_path << " must document destroy callback isolation";
        EXPECT_NE (readme.find ("actor lookup에서 사라지는지"), std::string::npos)
          << sample.readme_path << " must document post-destroy registry cleanup evidence";
        EXPECT_NE (readme.find ("같은 actor id 재생성"), std::string::npos)
          << sample.readme_path << " must document post-destroy recreate evidence";

        EXPECT_NE (runner.find ("test_cpp_framework_sample_parity"), std::string::npos)
          << sample.runner_path << " must run sample parity gate";
        EXPECT_NE (runner.find ("zlink_cpp_framework_mesh_node_vertical_test"), std::string::npos)
          << sample.runner_path << " must run the current MeshNode Actor vertical gate";
        EXPECT_NE (runner.find ("test_cpp_framework_actor_gateway"),
                   std::string::npos)
          << sample.runner_path << " must run ActorGateway registry cleanup gate";
    }
}

/* 공통 sample spec §6: TicTacToe는 location store 자동 연결이 아니라 수동 endpoint
 * scale-out을 보여 준다. API<->Play channel은 설정에 적힌 두 peer endpoint를 직접 연결한다. */
TEST (CppFrameworkSampleParity, TicTacToeHostsUseManualEndpointScaleOutWithActorGatewayRelay)
{
    const auto tictactoe_root = cpp_language_root () / "samples/TicTacToe";
    const auto api_factory = read_file (tictactoe_root / "Server/Api/api_server_host_factory.hpp");
    const auto client = read_file (tictactoe_root / "Client/tictactoe_client_scenario.hpp");
    const auto client_main = read_file (tictactoe_root / "Client/main.cpp");
    const auto create_game_handler =
      read_file (tictactoe_root / "Server/Api/Handlers/create_game_http_handler.hpp");
    const auto play_factory =
      read_file (tictactoe_root / "Server/Play/play_server_host_factory.hpp");

    EXPECT_FALSE (std::filesystem::exists (tictactoe_root / "Server/Session"));
    EXPECT_FALSE (std::filesystem::exists (tictactoe_root / "Server/Registry"));
    EXPECT_EQ (api_factory.find ("options.use_discovery ()"), std::string::npos);
    EXPECT_EQ (play_factory.find ("options.use_discovery ()"), std::string::npos);
    EXPECT_EQ (play_factory.find (".use_registry_spot_resolver"), std::string::npos);
    EXPECT_EQ (api_factory.find (".enable_client ()"), std::string::npos);
    EXPECT_EQ (play_factory.find (".enable_client ()"), std::string::npos);
    EXPECT_NE (api_factory.find ("topology.all_play_endpoints ()"), std::string::npos);
    EXPECT_NE (play_factory.find ("topology.all_api_endpoints ()"), std::string::npos);
    EXPECT_NE (api_factory.find (".enable_client (endpoint)"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_client (endpoint)"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_route_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_entry_spot<tictactoe_entry_spot_t> ()"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_spot<tictactoe_game_spot_t> (sample_names_t::match_spot)"),
               std::string::npos);
    EXPECT_EQ (play_factory.find (".add_spot<tictactoe_match_t>"), std::string::npos);
    EXPECT_NE (play_factory.find (".peer_connections ()"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_stream_node (sample_names_t::stream_name)"),
               std::string::npos);
    EXPECT_NE (play_factory.find (".register_session<play_session_t> ()"), std::string::npos);
    EXPECT_NE (api_factory.find (".listen (topology.selected_api_http_endpoint ())"),
               std::string::npos);
    EXPECT_NE (api_factory.find (".map_post<create_game_http_handler_t> (\"/games\")"),
               std::string::npos);
    EXPECT_EQ (api_factory.find (".add_json"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_json"), std::string::npos);
    EXPECT_EQ (client.find (".add_json"), std::string::npos);
    EXPECT_EQ (api_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (api_factory.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (client.find (".add_protobuf"), std::string::npos);
    EXPECT_NE (create_game_handler.find ("channel_client_t"), std::string::npos);
    EXPECT_NE (create_game_handler.find ("sample_names_t::play_channel"), std::string::npos);
    EXPECT_NE (
      create_game_handler.find ("const auto create_request = create_game_req_t{game_name}"),
      std::string::npos);
    EXPECT_NE (create_game_handler.find (".request (sample_names_t::play_channel, create_request)"),
               std::string::npos);
    EXPECT_NE (play_factory.find ("add_singleton<tictactoe_game_creator_t"), std::string::npos);
    EXPECT_NE (play_factory.find ("redis_room_route_store_t"), std::string::npos);
    /* Application은 port에만 의존한다(공통 sample spec §7). */
    EXPECT_NE (play_factory.find ("add_singleton<room_route_store_t>"), std::string::npos);
    EXPECT_NE (play_factory.find (".group (\"play\")"), std::string::npos);
    EXPECT_NE (play_factory.find (".add<create_game_handler_t> ()"), std::string::npos);
    EXPECT_EQ (api_factory.find ("add_singleton<create_game_room_handler_t>"), std::string::npos);
    EXPECT_FALSE (std::filesystem::exists (
      tictactoe_root / "Server/Play/Application/GameCreation/create_game_room_handler.hpp"));
    EXPECT_EQ (client_main.find ("#include <zlink/http_client.hpp>"), std::string::npos);
    EXPECT_EQ (client_main.find (".post (\"/games\")"), std::string::npos);
    EXPECT_NE (client.find ("#include <zlink/http_client.hpp>"), std::string::npos);
    EXPECT_NE (client.find ("zlink::http_client::client_t::create (options.api_http_endpoint)"),
               std::string::npos);
    EXPECT_NE (client.find (".post (\"/games\")"), std::string::npos);
    EXPECT_NE (client.find (".async<create_game_http_res_t> ().result ().value ().body"), std::string::npos);
    EXPECT_EQ (client.find (".json ()"), std::string::npos);
    EXPECT_EQ (client.find ("create_room (options)"), std::string::npos);
    EXPECT_EQ (client.find ("static create_game_http_res_t create_room"), std::string::npos);
    EXPECT_NE (client.find ("connector_options.endpoint = room.owner_play_endpoint"),
               std::string::npos);
    EXPECT_NE (client.find ("observe_milestone_req_t"), std::string::npos);
    EXPECT_NE (client.find ("win_milestone_notify_t"), std::string::npos);
    EXPECT_NE (client.find (".request (client1_auth_request)"), std::string::npos);
    EXPECT_NE (client.find ("const auto client1_auth_request = authenticate_req_t"),
               std::string::npos);
    EXPECT_EQ (client.find ("client1, client1_auth_request"), std::string::npos);
    EXPECT_NE (client.find (".async<authenticate_res_t> ()"), std::string::npos);
    EXPECT_NE (client.find ("client2.wait_for<game_state_notify_t> ()"), std::string::npos);
    EXPECT_EQ (client.find ("tictactoe-client.log"), std::string::npos);
    EXPECT_EQ (client.find ("std::ofstream"), std::string::npos);
}

TEST (CppFrameworkSampleParity, BingoHostsUseRouteMeshCapabilities)
{
    const auto bingo_root = cpp_language_root () / "samples/Bingo";
    const auto api_framework = read_file (bingo_root / "Server/Api/api_server_framework.hpp");
    const auto play_factory = read_file (bingo_root / "Server/Play/play_server_host_factory.hpp");
    const auto session_factory =
      read_file (bingo_root / "Server/Session/session_server_host_factory.hpp");
    const auto common_codecs = read_file (bingo_root / "Server/common_codecs.hpp");
    const auto session = read_file (bingo_root / "Server/Session/Sessions/bingo_session.hpp");
    const auto contracts = read_file (bingo_root / "Shared/Contracts/messages.hpp");
    const auto client = read_file (bingo_root / "Client/bingo_client_scenario.hpp");
    const auto client_main = read_file (bingo_root / "Client/main.cpp");

    EXPECT_NE (play_factory.find ("options.add_route_mesh"), std::string::npos);
    EXPECT_NE (session_factory.find ("options.add_route_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_entry_spot<bingo_entry_spot_t> ("), std::string::npos);
    EXPECT_NE (play_factory.find (".add_spot<bingo_room_spot_t> (sample_names_t::room_spot)"),
               std::string::npos);
    EXPECT_EQ (play_factory.find (".add_spot<bingo_room_t>"), std::string::npos);
    EXPECT_NE (api_framework.find ("use_default_bingo_codecs (options.codecs ())"),
               std::string::npos);
    EXPECT_NE (play_factory.find ("use_default_bingo_codecs (options.codecs ())"),
               std::string::npos);
    EXPECT_NE (session_factory.find ("use_default_bingo_codecs (options.codecs ())"),
               std::string::npos);
    EXPECT_EQ (common_codecs.find ("codecs.use (framework_codecs::protobuf ())"),
               std::string::npos);
    EXPECT_NE (client_main.find ("core_client1.codecs ().use (zlink::framework_codecs::protobuf ())"),
               std::string::npos);
    EXPECT_NE (client_main.find ("core_client2.codecs ().use (zlink::framework_codecs::protobuf ())"),
               std::string::npos);
    EXPECT_EQ (client.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (api_framework.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (session_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client_main.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client.find ("bingo-client.log"), std::string::npos);
    EXPECT_EQ (client.find ("std::ofstream"), std::string::npos);
    EXPECT_EQ (session_factory.find (".enable_actor_gateway"), std::string::npos);
    EXPECT_NE (session.find (".relay_request (payload)"), std::string::npos);
    EXPECT_NE (session.find (".relay (payload)"), std::string::npos);
    EXPECT_EQ (session.find ("payload.to_raw ()"), std::string::npos);
    EXPECT_EQ (session.find ("RemoteActorPacket"), std::string::npos);
    EXPECT_EQ (play_factory.find ("remote_actor_packet_handler"), std::string::npos);
    EXPECT_EQ (contracts.find ("RemoteActorPacket"), std::string::npos);
}

TEST (CppFrameworkSampleParity, SampleRunnersDoNotEnableInternalAutoConnectTracing)
{
    const auto samples = cpp_language_root () / "samples";
    for (const auto *name : {"Bingo", "DeliveryDispatch", "GameQuest"}) {
        const auto runner = read_file (samples / name / "run_sample.sh");
        EXPECT_EQ (runner.find ("ZLINK_CPP_AUTO_CONNECT_TRACE"), std::string::npos)
          << name << " runner must not configure framework internals through the environment";
        EXPECT_EQ (runner.find ("zlink auto-connect"), std::string::npos)
          << name << " runner must not use internal trace text as a release oracle";
    }
}

TEST (CppFrameworkSampleParity, SampleRunnersBuildBeforeStartingRedis)
{
    const auto samples = cpp_language_root () / "samples";
    for (const auto *name : {"SupportChat", "ShoppingMall", "GameQuest", "DeliveryDispatch"}) {
        const auto runner = read_file (samples / name / "run_sample.sh");
        const auto build = runner.find ("cmake --build");
        const auto redis = runner.find ("zlink_redis_start_scoped_assign");
        ASSERT_NE (build, std::string::npos) << name << " runner must build its sample targets";
        ASSERT_NE (redis, std::string::npos) << name << " runner must start scoped Redis";
        EXPECT_LT (build, redis) << name << " runner must not hold Redis during compilation";
    }
}

TEST (CppFrameworkSampleParity, SupportChatReleaseGateUsesThePublicClient)
{
    const auto runner = read_file (
      cpp_language_root () / "samples/SupportChat/run_sample.sh");

    EXPECT_NE (runner.find ("sample_cpp_framework_supportchat_client\" --stream-endpoint"),
               std::string::npos)
      << "SupportChat release gate must execute the public stream client";
    EXPECT_EQ (runner.find ("\"$BIN_DIR/sample_cpp_framework_supportchat_probe\""),
               std::string::npos)
      << "SupportChat release gate must not execute the in-memory server story probe";
    EXPECT_EQ (runner.find ("supportchat server-invariants=verified"), std::string::npos)
      << "SupportChat release gate must not accept the forged probe marker";
}

TEST (CppFrameworkSampleParity, SupportChatPushWaitsStayTyped)
{
    const auto scenario = read_file (
      cpp_language_root () / "samples/SupportChat/Client/supportchat_client_scenario.hpp");

    EXPECT_EQ (scenario.find ("wait_for<zlink::stream_connector::packet_t>"), std::string::npos)
      << "SupportChat must not wait for raw connector packets";
    EXPECT_EQ (scenario.find (".parse_json<"), std::string::npos)
      << "SupportChat must not manually decode typed push payloads";
    for (const auto *type : {"conversation_assigned_notify_t", "participant_joined_notify_t",
                             "chat_message_notify_t", "typing_changed_notify_t"}) {
        EXPECT_NE (scenario.find (std::string ("wait_for<") + type + ">"), std::string::npos)
          << "SupportChat typed wait is missing for " << type;
    }
}

TEST (CppFrameworkSampleParity, SupportChatConversationJoinCarriesParticipantIdentity)
{
    const auto contracts = read_file (
      cpp_language_root () / "samples/SupportChat/Shared/Contracts/messages.hpp");
    const auto support = read_file (
      cpp_language_root () / "samples/SupportChat/Server/Support/main.cpp");
    EXPECT_NE (contracts.find ("std::string participant_id;"), std::string::npos);
    EXPECT_NE (contracts.find ("std::string role;"), std::string::npos);
    EXPECT_NE (contracts.find ("std::string display_name;"), std::string::npos);
    EXPECT_NE (contracts.find ("{\"participantId\", value.participant_id}"),
               std::string::npos);
    EXPECT_NE (support.find ("join_conversation_req_t{participant_id"), std::string::npos)
      << "Support server must fill participant identity for the conversation actor join";
}

TEST (CppFrameworkSampleParity, SupportChatUsesFrameworkActorRefSnapshot)
{
    const auto contracts = read_file (
      cpp_language_root () / "samples/SupportChat/Shared/Contracts/messages.hpp");
    const auto support = read_file (
      cpp_language_root () / "samples/SupportChat/Server/Support/main.cpp");
    EXPECT_EQ (contracts.find ("support_actor_ref_snapshot_t"), std::string::npos);
    EXPECT_NE (contracts.find ("actor_ref_snapshot_t actor;"), std::string::npos);
    EXPECT_NE (support.find ("actor_ref_snapshot_t::from (actor_ref)"), std::string::npos);
}

TEST (CppFrameworkSampleParity, TicTacToeStatePreservesNullableWireFields)
{
    const nlohmann::json wire = {{"roomId", "room-nullable"},
                                 {"board", "........."},
                                 {"status",
                                  zlink::samples::tictactoe::tictactoe_status_t::waiting_for_players},
                                 {"winner", nullptr},
                                 {"nextTurn", ""},
                                 {"xActorId", nullptr},
                                 {"oActorId", nullptr},
                                 {"lastMoveActorId", nullptr},
                                 {"lastMoveCell", nullptr}};

    zlink::samples::tictactoe::tictactoe_state_t state;
    EXPECT_NO_THROW (wire.get_to (state));
    EXPECT_FALSE (state.winner.has_value ());
    EXPECT_FALSE (state.x_actor_id.has_value ());
    EXPECT_FALSE (state.o_actor_id.has_value ());
    EXPECT_FALSE (state.last_move_actor_id.has_value ());
    EXPECT_FALSE (state.last_move_cell.has_value ());

    const nlohmann::json projected = zlink::samples::tictactoe::tictactoe_state_t{};
    EXPECT_TRUE (projected.at ("winner").is_null ());
    EXPECT_TRUE (projected.at ("xActorId").is_null ());
    EXPECT_TRUE (projected.at ("oActorId").is_null ());
    EXPECT_TRUE (projected.at ("lastMoveActorId").is_null ());
    EXPECT_TRUE (projected.at ("lastMoveCell").is_null ());
    EXPECT_FALSE (projected.contains ("draw"));
}

TEST (CppFrameworkSampleParity, ShoppingMallUsesNullableDecimalAmounts)
{
    const auto contracts = read_file (
      cpp_language_root () / "samples/ShoppingMall/Shared/Contracts/messages.hpp");
    EXPECT_EQ (contracts.find ("double amount"), std::string::npos);

    const nlohmann::json wire = {{"orderId", "order-null-amount"},
                                 {"status", "Created"},
                                 {"shippingAddressId", "shipping-1"},
                                 {"reservationId", nullptr},
                                 {"paymentId", nullptr},
                                 {"reason", nullptr},
                                 {"amount", nullptr},
                                 {"currency", "USD"},
                                 {"updatedAtUnixMs", 1}};
    zlink::samples::shoppingmall::order_state_t state;
    EXPECT_NO_THROW (wire.get_to (state));
    EXPECT_FALSE (state.amount.has_value ());

    const nlohmann::json projected = zlink::samples::shoppingmall::order_state_t{};
    EXPECT_TRUE (projected.at ("amount").is_null ());

    const auto amount = zlink::samples::shoppingmall::decimal_t ("120.01");
    const nlohmann::json amount_wire = amount;
    EXPECT_TRUE (amount_wire.is_number ());
    EXPECT_EQ (amount_wire.get<zlink::samples::shoppingmall::decimal_t> (), amount);
}

TEST (CppFrameworkSampleParity, ChannelSendBackpressureUsesIndependentDefault)
{
    const auto source = read_file (
      cpp_language_root () / "framework/src/runtime/channels/channel_outbound_exchange.cpp");
    const auto submit_send = source.find ("channel_outbound_exchange_t::submit_send");
    ASSERT_NE (submit_send, std::string::npos);
    const auto submit_send_body = source.substr (submit_send);

    EXPECT_NE (source.find ("default_send_wait_timeout = std::chrono::milliseconds (1000)"),
               std::string::npos)
      << "one-way send backpressure must use the contract's 1000ms default";
    EXPECT_NE (submit_send_body.find ("resolve_send_wait_timeout"), std::string::npos)
      << "one-way send must use its own backpressure policy";
    EXPECT_EQ (submit_send_body.find ("resolve_channel_wait_timeout"), std::string::npos)
      << "one-way send must not reuse request/reply timeout policy";
}

/* 샘플은 codec을 직접 짜지 않는다. protobuf payload는 protoc이 만든 message로 옮겨 싣고,
 * 직렬화는 codec extension이 한다. 손으로 varint를 쓰거나 JSON을 protobuf인 척 포장하는 것은
 * 금지다. connector의 payload 훅(to_stream_payload)은 그 message로 위임할 때만 쓴다. */
TEST (CppFrameworkSampleParity, SamplesDoNotHandRollCodecs)
{
    const std::vector<std::string> banned_patterns{
      "json_to_protobuf_payload",
      "json_from_protobuf_payload",
      "append_protobuf_varint",
      "read_protobuf_varint",
    };
    std::vector<std::string> violations;

    for (const auto &file : sample_source_files ()) {
        const auto relative = relative_sample_path (file);
        const auto content = read_file (file);
        for (const auto &pattern : banned_patterns) {
            if (content.find (pattern) != std::string::npos) {
                violations.push_back (relative + ":" + pattern);
            }
        }
        /* payload 훅을 정의하는 파일은 protobuf message로 위임해야 한다(호출만 하는 파일은
         * 무관하다). */
        if ((content.find ("inline zlink::message_t to_stream_payload") != std::string::npos
             || content.find ("inline void from_stream_payload") != std::string::npos)
            && content.find ("SerializeAsString") == std::string::npos
            && content.find ("ParseFromString") == std::string::npos) {
            violations.push_back (relative + ":stream-payload-hook-without-protobuf");
        }
    }

    EXPECT_TRUE (violations.empty ()) << "hand-rolled codec:\n"
                                      << testing::PrintToString (violations);
}

int main (int argc, char **argv)
{
    testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
