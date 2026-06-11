/* SPDX-License-Identifier: MPL-2.0 */

#include "../../samples/Bingo/Server/Configuration/sample_names.hpp"
#include "../../samples/Bingo/Server/Configuration/sample_topology.hpp"
#include "../../samples/Bingo/Shared/Contracts/messages.hpp"
#include "../../samples/Bingo/Server/Play/Adapters/ZLink/Actors/player_actor_factory.hpp"
#include "../../samples/Bingo/Server/Play/Adapters/ZLink/Handlers/allocate_bingo_room_handler.hpp"
#include "../../samples/Bingo/Server/Play/Adapters/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "../../samples/Bingo/Server/Play/Adapters/ZLink/Notifications/bingo_notification_publisher.hpp"
#include "../../samples/Bingo/Server/Play/Adapters/ZLink/Spots/Handlers/bingo_room_timer_handler.hpp"
#include "../../samples/Bingo/Server/Play/Adapters/ZLink/Spots/bingo_entry_spot.hpp"
#include "../../samples/Bingo/Server/Play/Adapters/ZLink/Spots/bingo_room_spot.hpp"
#include "../../samples/Bingo/Server/Play/Application/RoomAllocation/bingo_room_allocator.hpp"
#include "../../samples/Bingo/Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../../samples/TicTacToe/Server/Configuration/sample_names.hpp"
#include "../../samples/TicTacToe/Server/Configuration/sample_topology.hpp"
#include "../../samples/TicTacToe/Shared/Contracts/messages.hpp"
#include "../../samples/TicTacToe/Server/Play/Adapters/ZLink/Notifications/game_notification_publisher.hpp"
#include "../../samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/tictactoe_game_contract_mapper.hpp"
#include "../../samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/tictactoe_game_spot.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/create_game_http_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Adapters/ZLink/Handlers/create_game_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Adapters/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/tictactoe_entry_spot.hpp"
#include "../../samples/TicTacToe/Server/Play/Application/GameCreation/tictactoe_game_creator.hpp"
#include "../../samples/TicTacToe/Server/Play/Domain/TicTacToe/tictactoe_match.hpp"

#include <gtest/gtest.h>

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

bool has_suffix (const std::filesystem::path &path, const std::string &suffix)
{
    const auto value = path.string ();
    return value.size () >= suffix.size ()
           && value.compare (value.size () - suffix.size (), suffix.size (), suffix) == 0;
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
    EXPECT_STREQ (sample_names_t::state_packet, "BingoStateNotify");
    EXPECT_STREQ (sample_names_t::game_ended_packet, "BingoGameEndedNotify");

    authenticate_player_handler_t auth;
    const auto authenticated = auth.handle ({"player-1"});
    ASSERT_TRUE (authenticated.accepted);

    bingo_room_allocator_t rooms;
    allocate_bingo_room_handler_t allocator (rooms);
    const auto allocated = allocator.handle ({"two-player", authenticated.actor_id});

    ensure_player_actor_handler_t actors;
    const auto actor = actors.handle ({authenticated.actor_id, authenticated.display_name});
    EXPECT_STREQ (actor.actor_type.c_str (), sample_names_t::player_actor_type);

    player_actor_factory_t actor_factory;
    const auto player_actor = actor_factory.create (actor.actor, authenticated.display_name);
    EXPECT_EQ (player_actor.actor.actor_id, authenticated.actor_id);

    bingo_room_spot_t room_spot (allocated.room_id);
    const auto joined = room_spot.on_actor_join (
      player_actor,
      to_stream_payload (bingo_room_join_req_t{allocated.room_id, authenticated.actor_id,
                                               authenticated.display_name}));
    ASSERT_TRUE (joined.accepted);
    ASSERT_TRUE (joined.reply);
    bingo_room_join_res_t join_reply;
    from_stream_payload (*joined.reply, join_reply);
    EXPECT_EQ (join_reply.state.players.size (), 1U);

    zlink::framework::spot_context_t room_context;
    room_spot.configure (room_context);
    const auto room_handlers = room_context.handlers ().descriptors ();
    ASSERT_EQ (room_handlers.size (), 1U);
    EXPECT_EQ (room_handlers[0].kind, zlink::framework::spot_handler_kind_t::actor_packet);
    EXPECT_EQ (room_handlers[0].packet_name, submit_bingo_card_req_t::packet_name);

    bingo_entry_spot_t entry_spot;
    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 1U);
    EXPECT_EQ (entry_handlers[0].packet_name, match_bingo_req_t::packet_name);

    auto second_actor = actor_factory.create (actor_ref_snapshot_t{{}, "player-2", 1}, "Player 2");
    const auto second_joined =
      room_spot.on_actor_join (
        second_actor,
        to_stream_payload (bingo_room_join_req_t{allocated.room_id, "player-2", "Player 2"}));
    ASSERT_TRUE (second_joined.accepted);
    const auto submitted =
      room_spot.submit_card (player_actor,
                             zlink::framework::spot_actor_request_context_t{
                               submit_bingo_card_req_t::packet_name, "application/json", {}, {}},
                             {allocated.room_id, {1, 2, 3, 4, 5, 6, 7, 8, 9}});
    EXPECT_EQ (submitted.state.players[0].card.size (), 9U);
    room_spot.submit_card (second_actor,
                           zlink::framework::spot_actor_request_context_t{
                             submit_bingo_card_req_t::packet_name, "application/json", {}, {}},
                           {allocated.room_id, {7, 8, 9, 10, 11, 12, 13, 14, 15}});

    bingo_notification_publisher_t publisher;
    bingo_room_timer_handler_t timer;
    const auto drawn = timer.handle (room_spot, 1);
    publisher.publish_drawn (drawn);
    EXPECT_EQ (publisher.drawn.size (), 1U);
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
    tictactoe_game_creator_t creator;
    create_game_handler_t rooms (creator, topology);
    const auto created = rooms.handle ({"tictactoe-game"});
    EXPECT_EQ (created.play_endpoint, topology.stream_endpoint);
    EXPECT_EQ (created.game_name, "tictactoe-game");
    tictactoe_match_t room (created.room_id);
    room.create ({created.game_name});
    EXPECT_EQ (room.join (sample_names_t::x_actor_id, {created.room_id}).state.x_actor_id,
               sample_names_t::x_actor_id);
    EXPECT_EQ (room.join (sample_names_t::o_actor_id, {created.room_id}).state.status,
               "InProgress");

    entry_spot_t entry_spot;
    entry_spot.room.create ({created.game_name});
    zlink::framework::spot_actor_request_context_t join_context{
      join_game_req_t::packet_name, "application/json", {}, {}};
    player_actor_t first_actor{sample_names_t::x_actor_id};
    EXPECT_EQ (entry_spot.join_game (first_actor, join_context,
                                     {entry_spot.room.snapshot ().room_id})
                 .state.x_actor_id,
               sample_names_t::x_actor_id);
    player_actor_t opponent_actor{sample_names_t::o_actor_id};
    const auto joined =
      entry_spot.join_game (opponent_actor, join_context, {entry_spot.room.snapshot ().room_id});
    EXPECT_EQ (joined.state.o_actor_id, sample_names_t::o_actor_id);

    tictactoe_game_spot_t game_spot (created.room_id);
    game_spot.create ({created.game_name});
    const auto x_join = game_spot.on_actor_join (
      player_actor_t{sample_names_t::x_actor_id}, to_stream_payload (join_game_req_t{created.room_id}));
    ASSERT_TRUE (x_join.accepted);
    const auto game_join = game_spot.on_actor_join (
      player_actor_t{sample_names_t::o_actor_id},
      to_stream_payload (join_game_req_t{created.room_id}));
    ASSERT_TRUE (game_join.accepted);
    zlink::framework::spot_actor_request_context_t place_context{
      place_mark_req_t::packet_name, "application/json", {}, {}};
    player_actor_t player_actor{sample_names_t::x_actor_id};
    const auto moved = game_spot.place_mark (player_actor, place_context, {0});
    EXPECT_EQ (moved.state.last_move_actor_id, sample_names_t::x_actor_id);

    zlink::framework::spot_context_t game_context;
    game_spot.configure (game_context);
    const auto game_handlers = game_context.handlers ().descriptors ();
    ASSERT_EQ (game_handlers.size (), 1U);
    EXPECT_EQ (game_handlers[0].kind, zlink::framework::spot_handler_kind_t::actor_packet);
    EXPECT_EQ (game_handlers[0].packet_name, place_mark_req_t::packet_name);

    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 1U);
    EXPECT_EQ (entry_handlers[0].packet_name, join_game_req_t::packet_name);

    const auto mapped = tictactoe_game_contract_mapper_t::to_contract (moved.state);
    EXPECT_EQ (mapped.room_id, created.room_id);

    game_notification_publisher_t publisher;
    publisher.game_state.push_back (
      game_state_notify_t{created.room_id, moved.state.next_turn, moved.state});
    EXPECT_EQ (publisher.game_state.size (), 1U);
}

TEST (CppFrameworkSampleParity, SampleHostsUseFrameworkOptionsSurface)
{
    const std::vector<std::string> banned_patterns{"configure_registry_host",
                                                   "configure_api_host",
                                                   "configure_play_host",
                                                   "configure_session_host",
                                                   "app.use_zlink",
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
    const std::vector<std::string> expected_samples{"Bingo", "TicTacToe"};

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
        EXPECT_TRUE (name == "Bingo" || name == "TicTacToe")
          << entry.path () << " adds a sample-name variant suffix";
    }
}

TEST (CppFrameworkSampleParity, SharedSampleDirectoryContainsOnlyContracts)
{
    const auto samples_root = cpp_language_root () / "samples";
    for (const auto &sample : {"Bingo", "TicTacToe"}) {
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
       {"sample_cpp_framework_bingo_registry", "sample_cpp_framework_bingo_api",
        "sample_cpp_framework_bingo_play", "sample_cpp_framework_bingo_session",
        "sample_cpp_framework_bingo_client"}},
      {"samples/TicTacToe/README.ko.md",
       {"sample_cpp_framework_tictactoe_api", "sample_cpp_framework_tictactoe_play",
        "sample_cpp_framework_tictactoe_client"}}};

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
}

TEST (CppFrameworkSampleParity, TicTacToeHostsUseManualEndpointsWithoutSessionGateway)
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
    EXPECT_EQ (play_factory.find ("options.use_registry_spot_remote_addresses"), std::string::npos);
    EXPECT_NE (api_factory.find (".enable_client (topology.play_endpoint)"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_route_mesh_channel"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_entry_spot<entry_spot_t> ()"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_spot<tictactoe_game_spot_t> (sample_names_t::match_spot)"),
               std::string::npos);
    EXPECT_EQ (play_factory.find (".add_spot<tictactoe_match_t>"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (play_factory.find (".accept_routes_from_channel"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_stream_node (sample_names_t::stream_name)"),
               std::string::npos);
    EXPECT_NE (play_factory.find (".register_session<play_session_t> ()"), std::string::npos);
    EXPECT_NE (play_factory.find (".attach_actor_gateway (sample_names_t::spot_node)"),
               std::string::npos);
    EXPECT_NE (api_factory.find (".listen (topology.api_http_endpoint)"), std::string::npos);
    EXPECT_NE (api_factory.find (".map_post<create_game_http_handler_t> (\"/games\")"),
               std::string::npos);
    EXPECT_NE (api_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_NE (client.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (api_factory.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (client.find (".add_protobuf"), std::string::npos);
    EXPECT_NE (create_game_handler.find ("zlink::framework::channel_client_t"),
               std::string::npos);
    EXPECT_NE (create_game_handler.find ("sample_names_t::play_channel"), std::string::npos);
    EXPECT_NE (create_game_handler.find ("create_game_req_t{game_name}"),
               std::string::npos);
    EXPECT_NE (play_factory.find ("add_singleton<tictactoe_game_creator_t>"),
               std::string::npos);
    EXPECT_NE (play_factory.find (".add<create_game_handler_t> (\"play\")"),
               std::string::npos);
    EXPECT_EQ (api_factory.find ("add_singleton<create_game_room_handler_t>"),
               std::string::npos);
    EXPECT_FALSE (std::filesystem::exists (
      tictactoe_root / "Server/Play/Application/GameCreation/create_game_room_handler.hpp"));
    EXPECT_EQ (client_main.find ("#include <zlink/http_client.hpp>"), std::string::npos);
    EXPECT_EQ (client_main.find (".post (\"/games\")"), std::string::npos);
    EXPECT_NE (client.find ("#include <zlink/http_client.hpp>"), std::string::npos);
    EXPECT_NE (client.find ("zlink::http_client::client_t::create ()"), std::string::npos);
    EXPECT_NE (client.find (".base_url (options.api_http_endpoint)"), std::string::npos);
    EXPECT_NE (client.find (".post (\"/games\")"), std::string::npos);
    EXPECT_NE (client.find (".submit<create_game_http_res_t> ()"), std::string::npos);
    EXPECT_NE (client.find ("connector_options.endpoint = room.play_endpoint"), std::string::npos);
    EXPECT_NE (client.find ("client1.request<authenticate_res_t>"), std::string::npos);
    EXPECT_NE (client.find ("client1.wait_for<game_state_notify_t>"), std::string::npos);
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
    const auto client = read_file (bingo_root / "Client/bingo_client_scenario.hpp");

    EXPECT_NE (play_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (session_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (session_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_pub_sub"), std::string::npos);
    EXPECT_NE (session_factory.find (".enable_pub_sub"), std::string::npos);
    EXPECT_NE (play_factory.find (".attach_channel_client"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_entry_spot<bingo_entry_spot_t> ()"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_spot<bingo_room_spot_t> (sample_names_t::room_spot)"),
               std::string::npos);
    EXPECT_EQ (play_factory.find (".add_spot<bingo_room_t>"), std::string::npos);
    EXPECT_NE (api_framework.find (".add_protobuf"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_protobuf"), std::string::npos);
    EXPECT_NE (session_factory.find (".add_protobuf"), std::string::npos);
    EXPECT_NE (client.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (api_framework.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (session_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client.find ("bingo-client.log"), std::string::npos);
    EXPECT_EQ (client.find ("std::ofstream"), std::string::npos);
}

int main (int argc, char **argv)
{
    testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
