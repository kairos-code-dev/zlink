/* SPDX-License-Identifier: MPL-2.0 */

#include "../../samples/Bingo/Shared/sample.hpp"
#include "../../samples/Bingo/Server/Play/Actors/player_actor_factory.hpp"
#include "../../samples/Bingo/Server/Play/BingoRoomSpots/Handlers/bingo_room_timer_handler.hpp"
#include "../../samples/Bingo/Server/Play/BingoRoomSpots/bingo_notification_publisher.hpp"
#include "../../samples/Bingo/Server/Play/BingoRoomSpots/bingo_room_spot.hpp"
#include "../../samples/Bingo/Server/Play/EntrySpot/bingo_entry_spot.hpp"
#include "../../samples/Bingo/Server/Play/Handlers/allocate_bingo_room_handler.hpp"
#include "../../samples/Bingo/Server/Play/Handlers/bingo_room_directory.hpp"
#include "../../samples/Bingo/Server/Play/Handlers/ensure_player_actor_handler.hpp"
#include "../../samples/Bingo/Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../../samples/TicTacToe/Shared/sample.hpp"
#include "../../samples/TicTacToe/Server/Play/GameSpots/game_notification_publisher.hpp"
#include "../../samples/TicTacToe/Server/Play/GameSpots/tictactoe_game_contract_mapper.hpp"
#include "../../samples/TicTacToe/Server/Play/GameSpots/tictactoe_game_spot.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/authenticate_actor_handler.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/create_match_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/EntrySpot/tictactoe_entry_spot.hpp"
#include "../../samples/TicTacToe/Server/Play/Handlers/create_match_room_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Handlers/ensure_player_actor_handler.hpp"

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

    bingo_room_directory_t rooms;
    allocate_bingo_room_handler_t allocator (rooms);
    const auto allocated = allocator.handle ({"four-player"});

    ensure_player_actor_handler_t actors;
    const auto actor = actors.handle ({authenticated.actor_id, authenticated.display_name});
    EXPECT_STREQ (actor.actor_type.c_str (), sample_names_t::player_actor_type);

    player_actor_factory_t actor_factory;
    const auto player_actor = actor_factory.create (actor.actor, authenticated.display_name);
    EXPECT_EQ (player_actor.actor.actor_id, authenticated.actor_id);

    bingo_room_spot_t room_spot (allocated.room_id);
    const auto joined =
      room_spot.join_actor (player_actor, {allocated.room_id, authenticated.actor_id, authenticated.display_name});
    EXPECT_EQ (joined.state.players.size (), 1U);

    zlink::framework::spot_context_t room_context;
    room_spot.configure (room_context);
    const auto room_handlers = room_context.handlers ().descriptors ();
    ASSERT_EQ (room_handlers.size (), 4U);
    EXPECT_EQ (room_handlers[0].kind, zlink::framework::spot_handler_kind_t::actor_join);
    EXPECT_EQ (room_handlers[0].packet_name, bingo_room_join_req_t::packet_name);
    EXPECT_EQ (room_handlers[1].kind, zlink::framework::spot_handler_kind_t::actor_packet);
    EXPECT_EQ (room_handlers[1].packet_name, start_bingo_game_req_t::packet_name);

    bingo_entry_spot_t entry_spot;
    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 3U);
    EXPECT_EQ (entry_handlers[0].packet_name, match_bingo_req_t::packet_name);

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
    EXPECT_STREQ (sample_names_t::opponent_joined_packet, "OpponentJoinedNotify");
    EXPECT_STREQ (sample_names_t::game_ended_packet, "GameEndedNotify");

    authenticate_actor_handler_t auth;
    const auto authenticated = auth.handle ({sample_names_t::x_actor_id});
    ASSERT_TRUE (authenticated.accepted);

    create_match_room_handler_t rooms;
    sample_topology_t topology;
    auto logger = zlink::framework::logger_factory_t ().create<create_match_handler_t> ();
    create_match_handler_t create (rooms, topology, logger);
    const auto created = create.handle ({authenticated.actor_id});
    EXPECT_EQ (created.play_endpoint, topology.stream_endpoint);
    tictactoe_match_room_t room (created.match_id);
    room.create ({created.owner_actor_id});

    entry_spot_t entry_spot;
    entry_spot.room.create ({created.owner_actor_id});
    zlink::framework::spot_actor_request_context_t join_context{
      join_match_req_t::packet_name, "application/json", {}, {}};
    player_actor_t opponent_actor{sample_names_t::o_actor_id};
    const auto joined = entry_spot.join_match (opponent_actor, join_context,
                                               {entry_spot.room.snapshot ().match_id, sample_names_t::o_actor_id});
    EXPECT_EQ (joined.mark, "O");

    tictactoe_game_spot_t game_spot (created.match_id);
    game_spot.create ({created.owner_actor_id});
    game_spot.join ({created.match_id, sample_names_t::o_actor_id});
    zlink::framework::spot_actor_request_context_t place_context{
      place_mark_req_t::packet_name, "application/json", {}, {}};
    player_actor_t player_actor{sample_names_t::x_actor_id};
    const auto moved =
      game_spot.place_mark (player_actor, place_context, {created.match_id, sample_names_t::x_actor_id, 0});
    EXPECT_EQ (moved.state.last_move_actor_id, sample_names_t::x_actor_id);

    zlink::framework::spot_context_t game_context;
    game_spot.configure (game_context);
    const auto game_handlers = game_context.handlers ().descriptors ();
    ASSERT_EQ (game_handlers.size (), 4U);
    EXPECT_EQ (game_handlers[0].kind, zlink::framework::spot_handler_kind_t::actor_join);
    EXPECT_EQ (game_handlers[0].packet_name, join_match_req_t::packet_name);
    EXPECT_EQ (game_handlers[1].kind, zlink::framework::spot_handler_kind_t::actor_packet);
    EXPECT_EQ (game_handlers[1].packet_name, place_mark_req_t::packet_name);

    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 3U);
    EXPECT_EQ (entry_handlers[0].packet_name, join_match_req_t::packet_name);

    const auto mapped = tictactoe_game_contract_mapper_t::to_contract (moved.state);
    EXPECT_EQ (mapped.match_id, created.match_id);

    game_notification_publisher_t publisher;
    publisher.turn_changed.push_back (turn_changed_notify_t{created.match_id, moved.state.turn_actor_id, moved.state});
    EXPECT_EQ (publisher.turn_changed.size (), 1U);
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
        EXPECT_TRUE (std::filesystem::is_directory (samples_root / sample)) << sample << " sample directory is missing";
    }

    for (const auto &entry : std::filesystem::directory_iterator (samples_root)) {
        if (!entry.is_directory ()) {
            continue;
        }
        const auto name = entry.path ().filename ().generic_string ();
        if (name == "Shared") {
            continue;
        }
        EXPECT_TRUE (name == "Bingo" || name == "TicTacToe") << entry.path () << " adds a sample-name variant suffix";
    }
}

TEST (CppFrameworkSampleParity, SharedSampleHeadersDoNotAggregateRoleCode)
{
    const auto samples_root = cpp_language_root () / "samples";
    const std::vector<std::filesystem::path> shared_headers{
      samples_root / "Bingo/Shared/sample.hpp", samples_root / "Bingo/Shared/host_support.hpp",
      samples_root / "TicTacToe/Shared/sample.hpp", samples_root / "TicTacToe/Shared/host_support.hpp"};
    const std::vector<std::string> forbidden_patterns{"../Client/", "../Server/", "/Client/", "/Server/"};

    for (const auto &path : shared_headers) {
        const auto content = read_file (path);
        for (const auto &pattern : forbidden_patterns) {
            EXPECT_EQ (content.find (pattern), std::string::npos)
              << path << " aggregates role-specific code via " << pattern;
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
    const std::vector<std::string> banned_json_patterns{"nlohmann::json::parse", ".at (", ".at(", "json["};

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

TEST (CppFrameworkSampleParity, SampleReadmesDescribePublicExecutablesAndLogEvidence)
{
    const auto cpp_root = cpp_language_root ();
    const auto cmake = read_file (cpp_root / "CMakeLists.txt");
    struct sample_readme_case_t
    {
        std::string readme_path;
        std::vector<std::string> public_targets;
        std::string log_file;
    };
    const std::vector<sample_readme_case_t> cases{
      {"samples/Bingo/README.ko.md",
       {"sample_cpp_framework_bingo_registry", "sample_cpp_framework_bingo_api", "sample_cpp_framework_bingo_play",
        "sample_cpp_framework_bingo_session", "sample_cpp_framework_bingo_client"},
       "bingo-server.log"},
      {"samples/TicTacToe/README.ko.md",
       {"sample_cpp_framework_tictactoe_registry", "sample_cpp_framework_tictactoe_api",
        "sample_cpp_framework_tictactoe_play", "sample_cpp_framework_tictactoe_session",
        "sample_cpp_framework_tictactoe_client"},
       "tictactoe-server.log"}};

    for (const auto &sample : cases) {
        const auto readme = read_file (cpp_root / sample.readme_path);
        for (const auto &target : sample.public_targets) {
            EXPECT_NE (cmake.find (target), std::string::npos) << target << " is missing from CMake sample targets";
            EXPECT_NE (readme.find ("`" + target + "`"), std::string::npos)
              << sample.readme_path << " does not document " << target;
        }

        EXPECT_EQ (readme.find ("_e2e_server`"), std::string::npos)
          << sample.readme_path << " should not document internal e2e server "
          << "targets as public sample executables";
        EXPECT_NE (readme.find (sample.log_file), std::string::npos)
          << sample.readme_path << " does not document the server log file";
        EXPECT_NE (readme.find ("monitoring event"), std::string::npos)
          << sample.readme_path << " does not document monitoring event evidence";
        EXPECT_NE (readme.find ("receive, reply, push"), std::string::npos)
          << sample.readme_path << " does not document request/reply/push " << "log evidence";
    }

    const auto tictactoe_readme = read_file (cpp_root / "samples/TicTacToe/README.ko.md");
    EXPECT_NE (tictactoe_readme.find ("HTTP client `POST /games`"), std::string::npos);
    EXPECT_NE (tictactoe_readme.find ("`zlink::http_client`"), std::string::npos);
    EXPECT_NE (tictactoe_readme.find ("HTTP request"), std::string::npos);
}

TEST (CppFrameworkSampleParity, TicTacToeHostsUseDiscoveryLikeDotNet)
{
    const auto tictactoe_root = cpp_language_root () / "samples/TicTacToe";
    const auto api_factory = read_file (tictactoe_root / "Server/Api/api_server_host_factory.hpp");
    const auto client = read_file (tictactoe_root / "Client/tictactoe_client.hpp");
    const auto play_factory = read_file (tictactoe_root / "Server/Play/play_server_host_factory.hpp");
    const auto session_factory = read_file (tictactoe_root / "Server/Session/session_server_host_factory.hpp");
    const auto registry_factory = read_file (tictactoe_root / "Server/Registry/registry_host_factory.hpp");

    EXPECT_NE (api_factory.find ("options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint)"),
               std::string::npos);
    EXPECT_NE (play_factory.find ("options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint)"),
               std::string::npos);
    EXPECT_NE (
      session_factory.find ("options.use_discovery ().add_registry_endpoint (topology.registry_router_endpoint)"),
      std::string::npos);
    EXPECT_NE (play_factory.find ("options.use_registry_spot_remote_addresses"), std::string::npos);
    EXPECT_NE (session_factory.find ("options.use_registry_spot_remote_addresses"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_route_mesh_channel"), std::string::npos);
    EXPECT_NE (session_factory.find ("options.add_route_mesh_channel"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (session_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_entry_spot<entry_spot_t> ()"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_spot<tictactoe_game_spot_t> (sample_names_t::match_spot)"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_spot<tictactoe_match_room_t>"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (session_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (play_factory.find (".accept_routes_from_channel"), std::string::npos);
    EXPECT_NE (session_factory.find (".accept_routes_from_channel"), std::string::npos);
    EXPECT_NE (registry_factory.find ("topology.registry_pub_endpoint"), std::string::npos);
    EXPECT_NE (registry_factory.find ("topology.registry_router_endpoint"), std::string::npos);
    EXPECT_NE (api_factory.find (".listen (topology.api_http_endpoint)"), std::string::npos);
    EXPECT_NE (api_factory.find (".map_post<create_match_handler_t> (\"/games\")"), std::string::npos);
    EXPECT_EQ (api_factory.find (".enable_client (topology.play_endpoint)"), std::string::npos);
    EXPECT_NE (client.find ("#include <zlink/http_client.hpp>"), std::string::npos);
    EXPECT_NE (client.find ("zlink::http_client::client_t::create ()"), std::string::npos);
    EXPECT_NE (client.find (".base_url (run_options.api_http_endpoint)"), std::string::npos);
    EXPECT_NE (client.find (".post (\"/games\")"), std::string::npos);
    EXPECT_NE (client.find (".submit<create_match_res_t> ()"), std::string::npos);
    EXPECT_NE (client.find ("result.http_game_created = http_ready"), std::string::npos);
    EXPECT_NE (client.find ("tictactoe-client.log"), std::string::npos);
    EXPECT_NE (client.find ("client game completed"), std::string::npos);
}

TEST (CppFrameworkSampleParity, BingoHostsUseSpotMeshCapabilitiesLikeDotNet)
{
    const auto bingo_root = cpp_language_root () / "samples/Bingo";
    const auto play_factory = read_file (bingo_root / "Server/Play/play_server_host_factory.hpp");
    const auto session_factory = read_file (bingo_root / "Server/Session/session_server_host_factory.hpp");
    const auto client = read_file (bingo_root / "Client/bingo_client_app.hpp");

    EXPECT_NE (play_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (session_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (session_factory.find (".enable_router"), std::string::npos);
    EXPECT_NE (play_factory.find (".enable_pub_sub"), std::string::npos);
    EXPECT_NE (session_factory.find (".enable_pub_sub"), std::string::npos);
    EXPECT_NE (play_factory.find (".attach_channel_client"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_entry_spot<bingo_entry_spot_t> ()"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_spot<bingo_room_spot_t> (sample_names_t::room_spot)"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_spot<bingo_room_t>"), std::string::npos);
    EXPECT_NE (client.find ("bingo-client.log"), std::string::npos);
    EXPECT_NE (client.find ("client game completed"), std::string::npos);
}

int main (int argc, char **argv)
{
    testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
