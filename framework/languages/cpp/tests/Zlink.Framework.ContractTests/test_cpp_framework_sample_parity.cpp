/* SPDX-License-Identifier: MPL-2.0 */

#include "../../samples/Bingo/Server/Configuration/sample_names.hpp"
#include "../../samples/Bingo/Server/Configuration/sample_topology.hpp"
#include "../../samples/Bingo/Shared/Contracts/messages.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Actors/player_actor_factory.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Handlers/allocate_bingo_room_handler.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo_room_spot.hpp"
#include "../../samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo_entry_spot.hpp"
#include "../../samples/Bingo/Server/Play/Application/RoomAllocation/bingo_room_allocator.hpp"
#include "../../samples/Bingo/Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../../samples/TicTacToe/Server/Configuration/sample_names.hpp"
#include "../../samples/TicTacToe/Server/Configuration/sample_topology.hpp"
#include "../../samples/TicTacToe/Shared/Contracts/messages.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/Notifications/game_notification_publisher.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe_game_contract_mapper.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/tictactoe_game_spot.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/authenticate_player_handler.hpp"
#include "../../samples/TicTacToe/Server/Api/Handlers/create_game_http_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Handlers/create_game_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Handlers/ensure_player_actor_handler.hpp"
#include "../../samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/tictactoe_entry_spot.hpp"
#include "../../samples/TicTacToe/Server/Play/Application/GameCreation/tictactoe_game_creator.hpp"
#include "../../samples/TicTacToe/Server/Play/Domain/TicTacToe/tictactoe_match.hpp"
#include "../../samples/SupportChat/Server/Configuration/sample_names.hpp"
#include "../../samples/SupportChat/Shared/Contracts/messages.hpp"
#include "../../samples/SupportChat/Server/Support/Domain/SupportChat/conversation.hpp"
#include "../../samples/SupportChat/Server/Support/Application/ConversationAssignment/agent_assignment_service.hpp"
#include "../../samples/SupportChat/Server/Support/Application/ConversationAssignment/agent_availability_directory.hpp"
#include "../../samples/SupportChat/Server/Support/Application/ConversationAssignment/support_conversation_allocator.hpp"
#include "../../samples/SupportChat/Server/Support/Infrastructure/ZLink/Actors/support_user_actor_factory.hpp"
#include "../../samples/SupportChat/Server/Support/Infrastructure/ZLink/Handlers/allocate_conversation_handler.hpp"
#include "../../samples/SupportChat/Server/Support/Infrastructure/ZLink/Handlers/assign_agent_handler.hpp"
#include "../../samples/SupportChat/Server/Support/Infrastructure/ZLink/Handlers/ensure_support_user_actor_handler.hpp"
#include "../../samples/SupportChat/Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/Handlers/conversation_idle_timer_handler.hpp"
#include "../../samples/SupportChat/Server/Support/Infrastructure/ZLink/Spots/ConversationSpot/conversation_spot.hpp"
#include "../../samples/SupportChat/Server/Support/Infrastructure/ZLink/Spots/EntrySpot/support_entry_spot.hpp"
#include "../../samples/SupportChat/Server/Api/Handlers/authenticate_user_handler.hpp"
#include "../../samples/DeliveryDispatch/Client/delivery_dispatch_client_scenario.hpp"
#include "../../samples/DeliveryDispatch/Server/delivery_dispatch_server_role.hpp"
#include "../../samples/GameQuest/Client/game_quest_client_scenario.hpp"
#include "../../samples/GameQuest/Server/game_quest_server_role.hpp"
#include "../../samples/ShoppingMall/Client/shopping_mall_client_scenario.hpp"
#include "../../samples/ShoppingMall/Server/shopping_mall_server_role.hpp"

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
    EXPECT_STREQ (sample_names_t::state_packet, "BingoStateNotify");
    EXPECT_STREQ (sample_names_t::game_ended_packet, "BingoGameEndedNotify");

    authenticate_player_handler_t auth;
    const auto authenticated = auth.handle ({"player-1"});
    ASSERT_TRUE (authenticated.accepted);

    sample_topology_t topology;
    const allocate_bingo_room_res_t allocated{"two-player-room-1",
                                              topology.selected_play_node_rid ()};

    ensure_player_actor_handler_t actors (topology);
    const auto actor = actors.handle ({authenticated.actor_id, authenticated.display_name});
    EXPECT_STREQ (actor.actor_type.c_str (), sample_names_t::player_actor_type);

    player_actor_factory_t actor_factory;
    const auto player_actor = actor_factory.create (actor.actor, authenticated.display_name);
    EXPECT_EQ (player_actor.actor.actor_id, authenticated.actor_id);

    bingo_room_spot_t room_spot (allocated.room_id);
    const auto joined = room_spot.on_actor_join (
      player_actor, zlink::framework::message_t::from (bingo_room_join_req_t{
                      allocated.room_id, authenticated.actor_id, authenticated.display_name}));
    ASSERT_TRUE (joined.accepted);
    ASSERT_TRUE (joined.reply);
    const auto join_reply = joined.reply->decode<bingo_room_join_res_t> ();
    EXPECT_EQ (join_reply.state.players.size (), 1U);

    zlink::framework::spot_context_t room_context;
    room_spot.configure (room_context);
    const auto room_handlers = room_context.handlers ().descriptors ();
    ASSERT_EQ (room_handlers.size (), 4U);
    EXPECT_EQ (room_handlers[0].kind, zlink::framework::spot_handler_kind_t::actor_packet);
    EXPECT_EQ (room_handlers[0].packet_name, submit_bingo_card_req_t::packet_name);

    bingo_entry_spot_t entry_spot;
    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 2U);
    EXPECT_EQ (entry_handlers[0].packet_name, match_bingo_req_t::packet_name);

    auto second_actor = actor_factory.create (actor_ref_snapshot_t{{}, "player-2", 1}, "Player 2");
    const auto second_joined = room_spot.on_actor_join (
      second_actor, zlink::framework::message_t::from (
                      bingo_room_join_req_t{allocated.room_id, "player-2", "Player 2"}));
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

    const auto &finished = room_spot.snapshot ();
    EXPECT_EQ (finished.room_id, allocated.room_id);
    EXPECT_EQ (finished.status, bingo_room_status_t::finished);
    EXPECT_GT (finished.draw_seq, 0);
    ASSERT_FALSE (finished.winners.empty ());
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
    EXPECT_EQ (room.join (sample_names_t::x_actor_id, {created.room_id, authenticated.player})
                 .state.x_actor_id,
               sample_names_t::x_actor_id);
    EXPECT_EQ (
      room
        .join (sample_names_t::o_actor_id, {created.room_id,
                                            {sample_names_t::o_actor_id, sample_names_t::o_actor_id,
                                             sample_names_t::required_level, 0}})
        .state.status,
      "InProgress");

    entry_spot_t entry_spot;
    tictactoe_game_spot_t game_spot;
    static_cast<tictactoe_match_t &> (game_spot) = tictactoe_match_t (created.room_id);
    const auto x_join =
      game_spot.on_actor_join (player_actor_t{sample_names_t::x_actor_id},
                               zlink::framework::message_t::from (
                                 tictactoe_game_join_req_t{created.room_id, authenticated.player}));
    ASSERT_TRUE (x_join.accepted);
    const auto game_join =
      game_spot.on_actor_join (player_actor_t{sample_names_t::o_actor_id},
                               zlink::framework::message_t::from (tictactoe_game_join_req_t{
                                 created.room_id,
                                 {sample_names_t::o_actor_id, sample_names_t::o_actor_id,
                                  sample_names_t::required_level, 0}}));
    ASSERT_TRUE (game_join.accepted);
    zlink::framework::spot_actor_request_context_t place_context{
      place_mark_req_t::packet_name, "application/json", {}, {}};
    player_actor_t player_actor{sample_names_t::x_actor_id};
    const auto moved = game_spot.place_mark (player_actor, place_context, {0});
    EXPECT_EQ (moved.state.last_move_actor_id, sample_names_t::x_actor_id);

    zlink::framework::spot_context_t game_context;
    game_spot.configure (game_context);
    const auto game_handlers = game_context.handlers ().descriptors ();
    ASSERT_EQ (game_handlers.size (), 2U);
    EXPECT_EQ (game_handlers[0].kind, zlink::framework::spot_handler_kind_t::actor_packet);
    EXPECT_EQ (game_handlers[0].packet_name, place_mark_req_t::packet_name);
    EXPECT_EQ (game_handlers[1].packet_name, leave_game_req_t::packet_name);

    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 3U);
    EXPECT_EQ (entry_handlers[0].packet_name, join_game_req_t::packet_name);
    EXPECT_EQ (entry_handlers[1].packet_name, observe_milestone_req_t::packet_name);
    EXPECT_EQ (entry_handlers[2].kind, zlink::framework::spot_handler_kind_t::subscription);

    const auto mapped = tictactoe_game_contract_mapper_t::to_contract (moved.state);
    EXPECT_EQ (mapped.room_id, created.room_id);

    game_notification_publisher_t publisher;
    publisher.game_state.push_back (
      game_state_notify_t{created.room_id, moved.state.next_turn, moved.state});
    EXPECT_EQ (publisher.game_state.size (), 1U);
}

TEST (CppFrameworkSampleParity, SupportChatUsesDotNetSamplePacketSurface)
{
    using namespace zlink::samples::supportchat;

    EXPECT_STREQ (sample_names_t::participant_joined_packet, "ParticipantJoinedNotify");
    EXPECT_STREQ (sample_names_t::conversation_assigned_packet, "ConversationAssignedNotify");
    EXPECT_STREQ (sample_names_t::chat_message_packet, "ChatMessageNotify");
    EXPECT_STREQ (sample_names_t::typing_changed_packet, "TypingChangedNotify");
    EXPECT_STREQ (sample_names_t::conversation_idle_packet, "ConversationIdleNotify");
    EXPECT_STREQ (sample_names_t::conversation_closed_packet, "ConversationClosedNotify");

    authenticate_user_handler_t auth;
    const auto customer = auth.handle ({support_chat_tokens_t::customer1});
    ASSERT_TRUE (customer.accepted);
    EXPECT_EQ (customer.actor_id, std::string ("customer-1"));
    EXPECT_EQ (customer.role, std::string (support_chat_roles_t::customer));
    const auto agent = auth.handle ({support_chat_tokens_t::agent1});
    ASSERT_TRUE (agent.accepted);
    EXPECT_EQ (agent.role, std::string (support_chat_roles_t::agent));
    const auto rejected = auth.handle ({"unknown-token"});
    EXPECT_FALSE (rejected.accepted);

    ensure_support_user_actor_handler_t actors;
    const auto ensured = actors.handle ({customer.actor_id, customer.display_name, customer.role});
    EXPECT_EQ (ensured.actor_type, std::string (sample_names_t::support_actor_type));
    EXPECT_EQ (ensured.actor.actor_id, customer.actor_id);
    const auto ensured_again =
      actors.handle ({customer.actor_id, customer.display_name, customer.role});
    EXPECT_EQ (ensured_again.actor.generation, ensured.actor.generation)
      << "reconnect must reuse the existing actor generation";

    support_user_actor_factory_t actor_factory;
    const auto customer_actor =
      actor_factory.create (ensured.actor, customer.display_name, customer.role);
    EXPECT_EQ (customer_actor.actor_id (), customer.actor_id);
    EXPECT_EQ (customer_actor.role, std::string (support_chat_roles_t::customer));

    support_conversation_allocator_t allocator;
    const auto conversation_id = allocator.allocate (customer.actor_id, "checkout payment failed");
    EXPECT_FALSE (conversation_id.empty ());

    agent_availability_directory_t availability;
    agent_assignment_service_t assignment (availability);
    EXPECT_FALSE (assignment.assign_next_agent ().has_value ());
    availability.set_available (agent.actor_id, agent.display_name, true);
    const auto picked = assignment.assign_next_agent ();
    ASSERT_TRUE (picked.has_value ());
    EXPECT_EQ (picked->actor_id, agent.actor_id);

    allocate_conversation_handler_t allocate_handler (allocator);
    const auto allocated =
      allocate_handler.handle ({customer.actor_id, customer.display_name, "subject"});
    EXPECT_EQ (allocated.status, std::string (conversation_statuses_t::waiting_for_agent));

    agent_availability_directory_t empty_availability;
    agent_assignment_service_t empty_assignment (empty_availability);
    assign_agent_handler_t assign_handler (empty_assignment);
    const auto unassigned =
      assign_handler.handle ({allocated.conversation_id, ""}).result ().value ();
    EXPECT_EQ (unassigned.status, std::string (conversation_statuses_t::waiting_for_agent))
      << "no available agent must stay in WaitingForAgent, not error";
    EXPECT_TRUE (unassigned.agent_actor_id.empty ());

    // Domain aggregate state transitions.
    conversation_t conversation (allocated.conversation_id, "checkout payment failed",
                                 customer.actor_id, customer.display_name, 1000);
    EXPECT_EQ (conversation.status (), std::string (conversation_statuses_t::waiting_for_agent));
    const auto assigned = conversation.join_agent (agent.actor_id, agent.display_name, 1100);
    EXPECT_EQ (assigned.state.status, std::string (conversation_statuses_t::active));
    EXPECT_EQ (assigned.state.agent_actor_id, agent.actor_id);
    ASSERT_EQ (assigned.events.size (), 2U);
    EXPECT_EQ (assigned.events[0].kind, conversation_event_kind_t::participant_joined);
    EXPECT_EQ (assigned.events[1].kind, conversation_event_kind_t::assigned);

    const auto sent = conversation.send_message (agent.actor_id, "greeting", 2000);
    EXPECT_EQ (sent.state.last_message_seq, 1ULL);
    ASSERT_FALSE (sent.events.empty ());
    ASSERT_TRUE (sent.events[0].message.has_value ());
    EXPECT_EQ (sent.events[0].message->message_seq, 1ULL);
    EXPECT_TRUE (sent.state.has_idle_deadline);

    const auto typing = conversation.set_typing (agent.actor_id, true);
    ASSERT_FALSE (typing.events.empty ());
    EXPECT_EQ (typing.events[0].kind, conversation_event_kind_t::typing_changed);
    EXPECT_TRUE (typing.events[0].is_typing.value_or (false));

    const auto idle = conversation.mark_idle (sent.state.idle_deadline_unix_ms + 1);
    EXPECT_EQ (idle.state.status, std::string (conversation_statuses_t::waiting_for_close));
    ASSERT_FALSE (idle.events.empty ());
    EXPECT_EQ (idle.events[0].kind, conversation_event_kind_t::idle);

    const auto closed = conversation.close (customer.actor_id, "resolved");
    EXPECT_EQ (closed.state.status, std::string (conversation_statuses_t::closed));
    EXPECT_THROW ((void) conversation.send_message (customer.actor_id, "after close", 9000),
                  std::runtime_error);
    EXPECT_THROW ((void) conversation.close (customer.actor_id, "again"), std::runtime_error);

    // Conversation Spot registers the in-conversation actor handlers.
    conversation_spot_t conversation_spot;
    zlink::framework::spot_context_t conversation_context;
    conversation_spot.configure (conversation_context);
    const auto conversation_handlers = conversation_context.handlers ().descriptors ();
    ASSERT_EQ (conversation_handlers.size (), 3U);
    for (const auto &descriptor : conversation_handlers) {
        EXPECT_EQ (descriptor.kind, zlink::framework::spot_handler_kind_t::actor_packet);
    }

    // Entry Spot registers the admission handlers and rejects non-customer opens.
    support_entry_spot_t entry_spot;
    zlink::framework::spot_context_t entry_context;
    entry_spot.configure (entry_context);
    const auto entry_handlers = entry_context.handlers ().descriptors ();
    ASSERT_EQ (entry_handlers.size (), 2U);

    auto agent_actor = actor_factory.create (actor_ref_snapshot_t{{}, "agent-9", 1}, "Agent 9",
                                             support_chat_roles_t::agent);
    zlink::framework::spot_actor_request_context_t available_context{
      set_agent_available_req_t::packet_name, "application/json", {}, {}};
    const auto availability_reply =
      entry_spot.set_agent_available (agent_actor, available_context, {true});
    EXPECT_TRUE (availability_reply.is_available);

    auto customer_only = actor_factory.create (actor_ref_snapshot_t{{}, "customer-9", 1},
                                               "Customer 9", support_chat_roles_t::customer);
    EXPECT_THROW ((void) entry_spot.set_agent_available (customer_only, available_context, {true}),
                  zlink::framework::framework_exception_t)
      << "customer actors must not register availability";

    // Idle timer handler forwards a time signal to the spot domain.
    conversation_idle_timer_handler_t idle_timer;
    (void) idle_timer;
}

TEST (CppFrameworkSampleParity, SupportChatEntrySpotUsesApiChannelOrchestration)
{
    const auto support_entry = read_file (cpp_language_root ()
                                          / "samples/SupportChat/Server/Support/Infrastructure/"
                                            "ZLink/Spots/EntrySpot/support_entry_spot.hpp");
    const auto api_handler =
      read_file (cpp_language_root ()
                 / "samples/SupportChat/Server/Api/Handlers/open_conversation_handler.hpp");

    EXPECT_NE (support_entry.find ("_context.outbound ()"), std::string::npos);
    EXPECT_NE (support_entry.find ("request_to_channel"), std::string::npos);
    EXPECT_NE (support_entry.find ("sample_names_t::api_channel"), std::string::npos);
    EXPECT_NE (support_entry.find ("open_conversation_api_req_t"), std::string::npos);
    EXPECT_EQ (support_entry.find ("allocator.allocate"), std::string::npos)
      << "Entry Spot must not bypass the API channel allocation path";
    EXPECT_EQ (support_entry.find ("assign_agent ("), std::string::npos)
      << "Entry Spot must not run local agent assignment";

    EXPECT_NE (api_handler.find ("sample_names_t::support_channel"), std::string::npos);
    EXPECT_NE (api_handler.find ("allocate_conversation_req_t"), std::string::npos);
    EXPECT_NE (api_handler.find ("assign_agent_req_t"), std::string::npos);
}

TEST (CppFrameworkSampleParity, DeliveryDispatchUsesDotNetSampleScenarioSurface)
{
    using namespace zlink::samples::deliverydispatch;

    delivery_dispatch_server_role_t server;
    const auto successful =
      server.create_delivery ({"delivery-success", "customer-1", "north gate", "south gate"});
    const auto reassigned =
      server.create_delivery ({"delivery-reassign", "customer-2", "east gate", "west gate"});
    server.subscribe_delivery (successful.delivery_id);
    server.subscribe_delivery (reassigned.delivery_id);
    EXPECT_TRUE (server.assert_evidence (successful.delivery_id, reassigned.delivery_id).passed);
}

TEST (CppFrameworkSampleParity, GameQuestUsesDotNetSampleScenarioSurface)
{
    using namespace zlink::samples::gamequest;

    game_quest_server_role_t server;
    server.enter_area ({"player-1", "quest-wolf-den", "area-1"});
    server.kill_monster ({"player-1", "wolf", "quest-wolf-den", "kill-1"});
    server.collect_item ({"player-1", "herb", 1, "item-1"});
    server.kill_monster ({"player-1", "wolf", "quest-wolf-den", "kill-2"});
    server.complete_mission ({"player-1", "quest-wolf-den", "mission-1"});
    const auto progress = server.get_progress ("player-1").active_quests;
    EXPECT_FALSE (progress.empty ());
}

TEST (CppFrameworkSampleParity, ShoppingMallUsesDotNetSampleScenarioSurface)
{
    using namespace zlink::samples::shoppingmall;

    shopping_mall_server_role_t server;
    const auto started =
      server.start_order ({"cart-success", "shipping-1", "pm-ok", "order-success-001"});
    EXPECT_EQ (started.status, std::string (order_status_t::created));
    const auto continued = server.continue_workflow (started.order_id);
    EXPECT_EQ (continued.state.status, std::string (order_status_t::confirmed));
    EXPECT_EQ (server.get_order (started.order_id).state.status,
               std::string (order_status_t::confirmed));
}

TEST (CppFrameworkSampleParity, DotNetParitySamplesUseRunnerOwnedServerProcess)
{
    struct sample_case_t
    {
        std::string sample;
        std::string client;
        std::string server_target;
        std::string port_env;
    };

    const std::vector<sample_case_t> samples{
      {"DeliveryDispatch", "Client/delivery_dispatch_client_scenario.hpp",
       "sample_cpp_framework_deliverydispatch_server", "DELIVERYDISPATCH_DISPATCH_ENDPOINT"},
      {"GameQuest", "Client/game_quest_client_scenario.hpp",
       "sample_cpp_framework_gamequest_server", "GAMEQUEST_QUEST_ENDPOINT"},
      {"ShoppingMall", "Client/shopping_mall_client_scenario.hpp",
       "sample_cpp_framework_shoppingmall_server", "SHOPPINGMALL_WORKFLOW_ENDPOINT"}};

    const auto samples_root = cpp_language_root () / "samples";
    for (const auto &sample : samples) {
        const auto client = read_file (samples_root / sample.sample / sample.client);
        const auto server = read_file (samples_root / sample.sample / "Server/main.cpp");
        const auto runner = read_file (samples_root / sample.sample / "run_sample.sh");
        if (sample.sample == "DeliveryDispatch" || sample.sample == "GameQuest"
            || sample.sample == "ShoppingMall") {
            EXPECT_NE (client.find ("channel_client_t"), std::string::npos)
              << sample.sample << " client must use the framework channel client";
            EXPECT_NE (client.find ("request_to_channel"), std::string::npos)
              << sample.sample << " client must request over a framework channel";
            EXPECT_NE (server.find ("add_client_server_channel"), std::string::npos)
              << sample.sample << " server must expose a framework channel";
            EXPECT_NE (server.find (".enable_server"), std::string::npos)
              << sample.sample << " server must bind the framework channel";
            EXPECT_EQ (client.find ("support::request_line"), std::string::npos)
              << sample.sample << " client must not use the temporary line protocol";
        }
        EXPECT_EQ (client.find ("../Server/"), std::string::npos)
          << sample.sample << " client must not include server role internals";
        EXPECT_EQ (client.find ("_state_t{\""), std::string::npos)
          << sample.sample << " client must not satisfy the scenario by mutating local state";
        EXPECT_NE (runner.find (sample.server_target), std::string::npos)
          << sample.sample << " runner must start the server executable";
        EXPECT_NE (runner.find (sample.port_env), std::string::npos)
          << sample.sample << " runner must pass a concrete endpoint to the client";
        EXPECT_NE (runner.find ("trap cleanup EXIT"), std::string::npos)
          << sample.sample << " runner must own server cleanup";
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
      "Bingo", "DeliveryDispatch", "GameQuest", "ShoppingMall", "SupportChat", "TicTacToe"};

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
    for (const auto &sample :
         {"Bingo", "DeliveryDispatch", "GameQuest", "ShoppingMall", "SupportChat", "TicTacToe"}) {
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

    const auto top_level_readme = read_file (cpp_root / "samples/README.ko.md");
    EXPECT_NE (top_level_readme.find ("full client/server self-check"), std::string::npos)
      << "C++ sample overview must describe full self-check scope";
    EXPECT_NE (top_level_readme.find ("TicTacToe sample-local script"), std::string::npos)
      << "C++ sample overview must name the TicTacToe full self-check";
    EXPECT_NE (top_level_readme.find ("Bingo sample-local script"), std::string::npos)
      << "C++ sample overview must name the Bingo full self-check";

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

TEST (CppFrameworkSampleParity, SampleActorDestroyFlowStaysInEntrySpot)
{
    const auto cpp_root = cpp_language_root ();
    struct sample_lifecycle_case_t
    {
        std::string entry_spot_path;
        std::string user_spot_path;
        std::string actor_path;
        std::string session_path;
        std::string readme_path;
        std::string runner_path;
    };
    const std::vector<sample_lifecycle_case_t> cases{
      {"samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/bingo_entry_spot.hpp",
       "samples/Bingo/Server/Play/Infrastructure/ZLink/Spots/BingoRoomSpot/bingo_room_spot.hpp",
       "samples/Bingo/Server/Play/Infrastructure/ZLink/Actors/player_actor.hpp",
       "samples/Bingo/Server/Session/Sessions/bingo_session.hpp", "samples/Bingo/README.ko.md",
       "samples/Bingo/run_sample.sh"},
      {"samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/EntrySpot/"
       "tictactoe_entry_spot.hpp",
       "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Spots/TicTacToeGameSpot/"
       "tictactoe_game_spot.hpp",
       "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Actors/player_actor.hpp",
       "samples/TicTacToe/Server/Play/Infrastructure/ZLink/Sessions/play_session.hpp",
       "samples/TicTacToe/README.ko.md", "samples/TicTacToe/run_sample.sh"}};

    for (const auto &sample : cases) {
        const auto entry = read_file (cpp_root / sample.entry_spot_path);
        const auto user = read_file (cpp_root / sample.user_spot_path);
        const auto actor = read_file (cpp_root / sample.actor_path);
        const auto session = read_file (cpp_root / sample.session_path);
        const auto readme = read_file (cpp_root / sample.readme_path);
        const auto runner = read_file (cpp_root / sample.runner_path);

        EXPECT_NE (entry.find ("onCreateActor"), std::string::npos)
          << sample.entry_spot_path << " must show actor creation callback";
        EXPECT_NE (entry.find ("on_actor_joined"), std::string::npos)
          << sample.entry_spot_path << " must show Entry Spot re-entry callback";
        EXPECT_NE (entry.find ("onLeaveActor"), std::string::npos)
          << sample.entry_spot_path << " must show Entry Spot leave callback";
        EXPECT_NE (entry.find ("onDisconnectActor"), std::string::npos)
          << sample.entry_spot_path << " must show Entry Spot disconnect callback";
        EXPECT_NE (entry.find (".destroyActor ("), std::string::npos)
          << sample.entry_spot_path << " must destroy actors from Entry Spot context";
        EXPECT_NE (entry.find ("destroy_after_entry_spot_join"), std::string::npos)
          << sample.entry_spot_path << " must guard destroy after Entry Spot re-entry";
        EXPECT_NE (entry.find ("mark_disconnected"), std::string::npos)
          << sample.entry_spot_path << " must mark actor disconnect state";
        EXPECT_NE (user.find (".leaveActor ("), std::string::npos)
          << sample.user_spot_path << " must return actors to Entry Spot with leaveActor";
        EXPECT_NE (user.find ("onDisconnectActor"), std::string::npos)
          << sample.user_spot_path << " must show user Spot disconnect callback";
        EXPECT_NE (user.find ("mark_for_destroy_after_room_leave"), std::string::npos)
          << sample.user_spot_path << " must mark destroy intent before leaveActor";
        EXPECT_NE (user.find ("mark_disconnected"), std::string::npos)
          << sample.user_spot_path << " must mark actor disconnect state";
        EXPECT_EQ (user.find ("destroyActor"), std::string::npos)
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
        EXPECT_EQ (session.find ("leaveActor"), std::string::npos)
          << sample.session_path << " must not leave rooms on disconnect";
        EXPECT_EQ (session.find ("destroyActor"), std::string::npos)
          << sample.session_path << " must not destroy actors on disconnect";

        EXPECT_NE (readme.find ("`onCreateActor`"), std::string::npos)
          << sample.readme_path << " must document actor creation callback";
        EXPECT_NE (readme.find ("`leaveActor`"), std::string::npos)
          << sample.readme_path << " must document room leave responsibility";
        EXPECT_NE (readme.find ("`destroyActor`"), std::string::npos)
          << sample.readme_path << " must document Entry Spot destroy responsibility";
        EXPECT_NE (readme.find ("`onLeaveActor`를 호출하지 않는다"), std::string::npos)
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

TEST (CppFrameworkSampleParity, TicTacToeHostsUseManualEndpointsWithActorGateway)
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
    EXPECT_NE (play_factory.find (".use_registry_spot_resolver"), std::string::npos);
    EXPECT_NE (api_factory.find (".enable_client (topology.selected_play_endpoint ())"),
               std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_route_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find ("options.add_spot_mesh"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_entry_spot<entry_spot_t> ()"), std::string::npos);
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
    EXPECT_NE (api_factory.find (".add_json"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_json"), std::string::npos);
    EXPECT_NE (client.find (".add_json"), std::string::npos);
    EXPECT_EQ (api_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (api_factory.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (client.find (".add_protobuf"), std::string::npos);
    EXPECT_NE (create_game_handler.find ("zlink::framework::channel_client_t"), std::string::npos);
    EXPECT_NE (create_game_handler.find ("sample_names_t::play_channel"), std::string::npos);
    EXPECT_NE (create_game_handler.find ("create_game_req_t{game_name}"), std::string::npos);
    EXPECT_NE (play_factory.find ("add_singleton<tictactoe_game_creator_t"), std::string::npos);
    EXPECT_NE (play_factory.find ("redis_room_route_store_t"), std::string::npos);
    EXPECT_NE (play_factory.find (".add<create_game_handler_t> (\"play\")"), std::string::npos);
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
    EXPECT_NE (client.find ("stream_e2e_client::codecs::request"), std::string::npos);
    EXPECT_NE (client.find ("client1, authenticate_req_t"), std::string::npos);
    EXPECT_NE (client.find (".async<authenticate_res_t> ()"), std::string::npos);
    EXPECT_NE (client.find ("stream_e2e_client::codecs::wait_for<game_state_notify_t>"),
               std::string::npos);
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
    EXPECT_NE (play_factory.find (".add_entry_spot<bingo_entry_spot_t> ()"), std::string::npos);
    EXPECT_NE (play_factory.find (".add_spot<bingo_room_spot_t> (sample_names_t::room_spot)"),
               std::string::npos);
    EXPECT_EQ (play_factory.find (".add_spot<bingo_room_t>"), std::string::npos);
    EXPECT_NE (api_framework.find ("codecs ().use"), std::string::npos);
    EXPECT_NE (play_factory.find ("codecs ().use"), std::string::npos);
    EXPECT_NE (session_factory.find ("codecs ().use"), std::string::npos);
    EXPECT_NE (client_main.find ("core_client1.codecs ().use"), std::string::npos);
    EXPECT_NE (client_main.find ("core_client2.codecs ().use"), std::string::npos);
    EXPECT_EQ (client.find (".add_protobuf"), std::string::npos);
    EXPECT_EQ (api_framework.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (play_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (session_factory.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client_main.find (".add_message_pack"), std::string::npos);
    EXPECT_EQ (client.find ("bingo-client.log"), std::string::npos);
    EXPECT_EQ (client.find ("std::ofstream"), std::string::npos);
    EXPECT_NE (session_factory.find (".enable_actor_gateway ()"), std::string::npos);
    EXPECT_NE (session.find (".relay_request (payload)"), std::string::npos);
    EXPECT_NE (session.find (".relay (payload)"), std::string::npos);
    EXPECT_EQ (session.find ("payload.to_raw ()"), std::string::npos);
    EXPECT_EQ (session.find ("RemoteActorPacket"), std::string::npos);
    EXPECT_EQ (play_factory.find ("remote_actor_packet_handler"), std::string::npos);
    EXPECT_EQ (contracts.find ("RemoteActorPacket"), std::string::npos);
}

TEST (CppFrameworkSampleParity, CodecHelpersStayConfinedToRawLifecycleBoundaries)
{
    const std::vector<std::string> helper_patterns{
      "to_stream_payload",          "from_stream_payload",    "json_to_protobuf_payload",
      "json_from_protobuf_payload", "append_protobuf_varint", "read_protobuf_varint",
    };
    std::vector<std::string> violations;

    for (const auto &file : sample_source_files ()) {
        const auto relative = relative_sample_path (file);
        const auto content = read_file (file);
        if (!contains_any (content, helper_patterns)) {
            continue;
        }
        violations.push_back (relative);
        if (content.find ("append_protobuf_varint") != std::string::npos
            || content.find ("read_protobuf_varint") != std::string::npos) {
            violations.push_back (relative + ":manual-protobuf-varint");
        }
        if (content.find ("json_to_protobuf_payload") != std::string::npos
            || content.find ("json_from_protobuf_payload") != std::string::npos) {
            violations.push_back (relative + ":json-protobuf-wrapper");
        }
    }

    EXPECT_TRUE (violations.empty ()) << "codec helper leakage:\n"
                                      << testing::PrintToString (violations);
}

int main (int argc, char **argv)
{
    testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
