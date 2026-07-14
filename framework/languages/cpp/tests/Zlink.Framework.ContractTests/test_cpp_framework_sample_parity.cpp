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
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/Notifications/game_notification_publisher.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe_game_spot.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/create_game_http_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Handlers/create_game_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/tictactoe_entry_spot.hpp"
#include "../../samples/TicTacToe/Server/Play/Application/GameCreation/tictactoe_game_creator.hpp"
#include "../../samples/TicTacToe/Server/Play/Domain/TicTacToe/tictactoe_match.hpp"
#include "../../samples/DeliveryDispatch/Shared/Contracts/messages.hpp"

#include <gtest/gtest.h>

#include <algorithm>
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

TEST (CppFrameworkSampleParity, BingoRewardSubscriptionDoesNotDriveRoomCleanup)
{
    const auto handler = read_file (
      cpp_language_root ()
      / "samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/Handlers/"
        "bingo_reward_acquired_event_handler.hpp");

    EXPECT_EQ (handler.find ("leave_finished_actors"), std::string::npos);
    EXPECT_NE (handler.find ("bingo_reward_announced_notify_t"), std::string::npos);
}

TEST (CppFrameworkSampleParity, TicTacToeUsesDotNetSamplePacketSurface)
{
    using namespace zlink::samples::tictactoe;

    EXPECT_STREQ (sample_names_t::game_state_packet, "GameStateNotify");
    EXPECT_STREQ (sample_names_t::player_joined_packet, "PlayerJoinedNotify");
    EXPECT_STREQ (sample_names_t::game_ended_packet, "GameEndedNotify");

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
    static_cast<tictactoe_match_t &> (game_spot) = tictactoe_match_t (created.room_id);
    const auto x_join =
      game_spot.on_actor_join (sample_names_t::x_actor_id,
                               zlink::framework::message_t::from (
                                 tictactoe_game_join_req_t{created.room_id, authenticated.player}));
    ASSERT_TRUE (x_join.accepted);
    const auto game_join =
      game_spot.on_actor_join (sample_names_t::o_actor_id,
                               zlink::framework::message_t::from (tictactoe_game_join_req_t{
                                 created.room_id,
                                 {sample_names_t::o_actor_id, sample_names_t::o_actor_id,
                                  sample_names_t::required_level, 0}}));
    ASSERT_TRUE (game_join.accepted);
    ASSERT_TRUE (game_join.reply);
    const auto projected_join = game_join.reply->decode<join_game_res_t> ();
    EXPECT_EQ (projected_join.state.status, tictactoe_status_t::waiting_for_players);

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

    game_notification_publisher_t publisher;
    publisher.game_state.push_back (
      game_state_notify_t{created.room_id, projected_join.state.next_turn, projected_join.state});
    EXPECT_EQ (publisher.game_state.size (), 1U);
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
        "sample_cpp_framework_deliverydispatch_probe",
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
    EXPECT_NE (top_level_readme.find ("full client/server self-check"), std::string::npos)
      << "C++ sample overview must describe full self-check scope";
    EXPECT_NE (top_level_readme.find ("TicTacToe sample-local script"), std::string::npos)
      << "C++ sample overview must name the TicTacToe full self-check";
    EXPECT_NE (top_level_readme.find ("Bingo sample-local script"), std::string::npos)
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
    EXPECT_NE (deliverydispatch_runner.find ("topology=ready"), std::string::npos)
      << "DeliveryDispatch runner must verify registry/discovery readiness";
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
    EXPECT_NE (session.find ("stream_codec_t::protobuf"), std::string::npos)
      << "Bingo bound session stream relay must use the Protobuf stream codec";
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

    EXPECT_NE (client.find ("wait_status_sequence"), std::string::npos);
    EXPECT_NE (client.find ("message.status == expected_status"), std::string::npos);
    EXPECT_EQ (client.find ("sleep_for"), std::string::npos);
    EXPECT_EQ (client.find ("wait_status ("), std::string::npos);
}

TEST (CppFrameworkSampleParity, SampleActorDestroyFlowStaysInEntrySpot)
{
    const auto cpp_root = cpp_language_root ();
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
        EXPECT_NE (session.find ("unbind_session"), std::string::npos)
          << sample.session_path << " must detach actor binding on disconnect";
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
        EXPECT_NE (readme.find ("`on_leave_actor`를 호출하지 않는다"), std::string::npos)
          << sample.readme_path << " must document destroy callback isolation";
        EXPECT_NE (readme.find ("actor lookup에서 사라지는지"), std::string::npos)
          << sample.readme_path << " must document post-destroy registry cleanup evidence";
        EXPECT_NE (readme.find ("같은 actor id 재생성"), std::string::npos)
          << sample.readme_path << " must document post-destroy recreate evidence";

        EXPECT_NE (runner.find ("test_cpp_framework_sample_parity"), std::string::npos)
          << sample.runner_path << " must run sample parity gate";
        EXPECT_NE (runner.find ("test_cpp_framework_spot_runtime"), std::string::npos)
          << sample.runner_path << " must run actor lifecycle runtime gate";
        EXPECT_NE (runner.find ("test_cpp_framework_ActorGateway_actor_session_relay"),
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
    EXPECT_EQ (play_factory.find ("options.add_route_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_entry_spot<tictactoe_entry_spot_t> ()"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_spot<tictactoe_game_spot_t> (sample_names_t::match_spot)"),
               std::string::npos);
    EXPECT_EQ (play_factory.find (".add_spot<tictactoe_match_t>"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_router"), std::string::npos);
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
    EXPECT_NE (client.find (".fetch<create_game_http_res_t> ()"), std::string::npos);
    EXPECT_EQ (client.find (".submit<create_game_http_res_t> ()"), std::string::npos);
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

TEST (CppFrameworkSampleParity, BingoHostsUseSpotMeshCapabilitiesLikeDotNet)
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

    EXPECT_NE (play_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (session_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (session_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_pub_sub"), std::string::npos);
    EXPECT_NE (session_factory.find (".enable_pub_sub"), std::string::npos);
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
