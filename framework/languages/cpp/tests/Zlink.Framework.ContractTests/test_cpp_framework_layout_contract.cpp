/* SPDX-License-Identifier: MPL-2.0 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ZLINK_FRAMEWORK_CPP_SOURCE_DIR
#error "ZLINK_FRAMEWORK_CPP_SOURCE_DIR must be defined"
#endif

namespace
{

bool
require_exists (const std::filesystem::path &path)
{
  if (std::filesystem::exists (path)) {
    return true;
  }
  std::cerr << "missing required path: " << path << '\n';
  return false;
}

bool
require_absent (const std::filesystem::path &path,
                const std::string &reason)
{
  if (!std::filesystem::exists (path)) {
    return true;
  }
  std::cerr << "unexpected path: " << path << " (" << reason << ")\n";
  return false;
}

bool
public_headers_do_not_include_runtime (const std::filesystem::path &root)
{
  bool ok = true;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator (root)) {
    if (!entry.is_regular_file () || entry.path ().extension () != ".hpp") {
      continue;
    }

    std::ifstream input (entry.path ());
    std::string line;
    std::size_t line_no = 0;
    while (std::getline (input, line)) {
      ++line_no;
      if (line.find ("src/runtime") != std::string::npos) {
        std::cerr << "public header references runtime implementation: "
                  << entry.path () << ':' << line_no << '\n';
        ok = false;
      }
    }
  }
  return ok;
}

bool
public_headers_do_not_expose_runtime_dependencies (
  const std::filesystem::path &root)
{
  bool ok = true;
  const std::string forbidden[] = {
    "#include <boost",
    "#include \"boost",
    "boost::asio",
    "boost::beast",
    "#include <openssl",
    "#include <OpenSSL",
    "SSL_CTX",
    "SSL_CTX_",
    "SSL *",
    "SSL*",
    "ssl::stream",
    "#include <gtest",
    "#include <gmock",
    "testing::",
    "#include <spdlog",
    "spdlog::",
    "#include <fmt",
    "fmt::",
    "#include <zlink.hpp",
    "#include <zlink/Contracts/Sockets",
    "#include <zlink/Contracts/Service",
    "zlink::context_t",
    "zlink::router_socket_t",
    "zlink::stream_socket_t",
    "zlink::dealer_socket_t",
    "zlink::pub_socket_t",
    "zlink::sub_socket_t"
  };

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator (root)) {
    if (!entry.is_regular_file ()) {
      continue;
    }
    const auto ext = entry.path ().extension ();
    if (ext != ".hpp" && ext != ".h") {
      continue;
    }

    std::ifstream input (entry.path ());
    std::string line;
    std::size_t line_no = 0;
    while (std::getline (input, line)) {
      ++line_no;
      for (const auto &needle : forbidden) {
        if (line.find (needle) != std::string::npos) {
          std::cerr << "public header exposes runtime/test dependency: "
                    << entry.path () << ':' << line_no << " contains "
                    << needle << '\n';
          ok = false;
        }
      }
    }
  }
  return ok;
}

bool
file_contains (const std::filesystem::path &path, const std::string &needle)
{
  std::ifstream input (path);
  std::ostringstream buffer;
  buffer << input.rdbuf ();
  return buffer.str ().find (needle) != std::string::npos;
}

std::size_t
count_occurrences (const std::string &text, const std::string &needle)
{
  std::size_t count = 0;
  std::size_t offset = 0;
  while (true) {
    offset = text.find (needle, offset);
    if (offset == std::string::npos) {
      return count;
    }
    ++count;
    offset += needle.size ();
  }
}

bool
posd_log_has_current_goal_mapping (const std::filesystem::path &root)
{
  const auto path =
    root / "doc/draft/cpp-framework-posd-refactoring-log.ko.md";
  std::ifstream input (path);
  std::ostringstream buffer;
  buffer << input.rdbuf ();
  const auto text = buffer.str ();

  bool ok = true;
  const auto refactor_count =
    count_occurrences (text, "### 적용한 리팩토링");
  if (refactor_count < 22) {
    std::cerr << "POSD refactoring log has only " << refactor_count
              << " refactoring sections; expected at least 22: "
              << path << '\n';
    ok = false;
  }

  const std::string rows[] = {
    "| Goal 1. Repository Skeleton And Tooling |",
    "| Goal 2. Binding Codec Surface Alignment |",
    "| Goal 3. Core Async, Task, Error Model |",
    "| Goal 4. App Host, Configuration, Logging |",
    "| Goal 5. DI Container And Scope Lifetime |",
    "| Goal 6. Application Framework Parity Model |",
    "| Goal 7. Runtime Integration And Execution |",
    "| Goal 8. Handler Registry And Serializer |",
    "| Goal 9. Channel Messaging |",
    "| Goal 10. Backpressure And Reliability |",
    "| Goal 11. SPOT Runtime |",
    "| Goal 12. SPOT Timer |",
    "| Goal 13. STREAM Framework |",
    "| Goal 14. ActorGateway Session Relay |",
    "| Goal 15. Registry And Topology |",
    "| Goal 16. Monitoring, Health, Observability |",
    "| Goal 17. Module System And Hosted Services |",
    "| Goal 18. ZLink HTTP Client |",
    "| Goal 19. HTTP Hosting |",
    "| Goal 20. Stream Connectors |",
    "| Goal 21. Review Samples |",
    "| Goal 22. Final Regression, Package, Extension Boundary |"
  };
  for (const auto &row : rows) {
    if (text.find (row) == std::string::npos) {
      std::cerr << "POSD refactoring log lacks current goal mapping row: "
                << row << '\n';
      ok = false;
    }
  }
  return ok;
}

bool
cmake_extension_boundaries_hold (const std::filesystem::path &root)
{
  const auto path = root / "CMakeLists.txt";
  std::ifstream input (path);
  std::ostringstream buffer;
  buffer << input.rdbuf ();
  const auto text = buffer.str ();

  bool ok = true;
  const auto extension_count =
    count_occurrences (text, "add_zlink_framework_extension(");
  if (extension_count != 11) {
    std::cerr << "expected 11 framework extension targets, got "
              << extension_count << ": " << path << '\n';
    ok = false;
  }
  if (text.find ("target_link_libraries(${target_name} INTERFACE zlink::framework)") ==
      std::string::npos) {
    std::cerr << "framework extension helper must depend on core only: "
              << path << '\n';
    ok = false;
  }

  const std::string forbidden[] = {
    "target_link_libraries(zlink_framework PUBLIC zlink::framework_extension_",
    "target_link_libraries(zlink_framework PRIVATE zlink::framework_extension_",
    "target_link_libraries(zlink_framework PUBLIC Kafka",
    "target_link_libraries(zlink_framework PRIVATE Kafka",
    "target_link_libraries(zlink_framework PUBLIC gRPC",
    "target_link_libraries(zlink_framework PRIVATE gRPC",
    "target_link_libraries(zlink_framework PUBLIC yaml",
    "target_link_libraries(zlink_framework PRIVATE yaml",
    "target_link_libraries(zlink_framework PUBLIC FlatBuffers",
    "target_link_libraries(zlink_framework PRIVATE FlatBuffers",
    "find_package(Kafka",
    "find_package(gRPC",
    "find_package(yaml",
    "find_package(YAML",
    "find_package(FlatBuffers"
  };
  for (const auto &needle : forbidden) {
    if (text.find (needle) != std::string::npos) {
      std::cerr << "core framework target must not depend on extension "
                   "package/target by default: "
                << needle << '\n';
      ok = false;
    }
  }
  return ok;
}

bool
contract_headers_have_compile_coverage (const std::filesystem::path &root,
                                        const std::filesystem::path &include_dir,
                                        const std::string &include_prefix)
{
  const auto coverage_file =
    root /
    "tests/Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp";
  std::ifstream coverage_input (coverage_file);
  std::ostringstream coverage_buffer;
  coverage_buffer << coverage_input.rdbuf ();
  const auto coverage_text = coverage_buffer.str ();

  bool ok = true;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator (root / include_dir)) {
    if (!entry.is_regular_file () || entry.path ().extension () != ".hpp") {
      continue;
    }
    const auto relative = std::filesystem::relative (
      entry.path (), root / include_dir);
    const auto include = std::string ("#include <") + include_prefix +
                         relative.generic_string () + ">";
    if (coverage_text.find (include) == std::string::npos) {
      std::cerr << "public contract header lacks direct compile coverage: "
                << entry.path () << '\n';
      ok = false;
    }
  }
  return ok;
}

bool
sample_application_code_uses_message_codec (const std::filesystem::path &root)
{
  bool ok = true;
  const auto samples_root = root / "samples";
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator (samples_root)) {
    if (!entry.is_regular_file ()) {
      continue;
    }
    const auto ext = entry.path ().extension ();
    if (ext != ".hpp" && ext != ".cpp") {
      continue;
    }

    const auto relative =
      std::filesystem::relative (entry.path (), samples_root).generic_string ();
    const bool dto_contract_file =
      relative.find ("/Shared/Contracts/") != std::string::npos;

    std::ifstream input (entry.path ());
    std::string line;
    std::size_t line_no = 0;
    while (std::getline (input, line)) {
      ++line_no;
      if (line.find ("nlohmann::json::parse") != std::string::npos) {
        std::cerr << "sample application code must use message_t/serializer "
                     "instead of direct JSON parse: "
                  << entry.path () << ':' << line_no << '\n';
        ok = false;
      }
      if (!dto_contract_file &&
          line.find ("json.at") != std::string::npos) {
        std::cerr << "sample application code must not extract JSON fields "
                     "outside DTO serializer hooks: "
                  << entry.path () << ':' << line_no << '\n';
        ok = false;
      }
    }
  }
  return ok;
}

bool
sample_server_code_does_not_block_on_task_result (
  const std::filesystem::path &root)
{
  bool ok = true;
  const auto samples_root = root / "samples";
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator (samples_root)) {
    if (!entry.is_regular_file ()) {
      continue;
    }
    const auto ext = entry.path ().extension ();
    if (ext != ".hpp" && ext != ".cpp") {
      continue;
    }

    const auto relative =
      std::filesystem::relative (entry.path (), samples_root).generic_string ();
    if (relative.find ("/Server/") == std::string::npos &&
        relative.find ("/Shared/") == std::string::npos) {
      continue;
    }

    std::ifstream input (entry.path ());
    std::string line;
    std::size_t line_no = 0;
    while (std::getline (input, line)) {
      ++line_no;
      if (line.find (".result (") != std::string::npos ||
          line.find (".result(") != std::string::npos) {
        std::cerr << "sample server/shared code must use task_t await or "
                     "callback completion instead of blocking result(): "
                  << entry.path () << ':' << line_no << '\n';
        ok = false;
      }
    }
  }
  return ok;
}

bool
client_sample_uses_connector (const std::filesystem::path &root,
                              const std::filesystem::path &client_file)
{
  const auto path = root / client_file;
  bool ok = true;
  ok &= file_contains (path, "zlink/stream_connector.hpp");
  ok &= file_contains (path, "../../Shared/client_connector_helpers.hpp");
  ok &= file_contains (path, "connector_factory_t::create");
  ok &= file_contains (path, "connect_client_connector");
  ok &= file_contains (path, "request_client_packet");
  ok &= file_contains (path, "zlink::stream_connector::codecs::on<");
  if (!ok) {
    std::cerr << "client sample does not use shared connector policy helper: "
              << path << '\n';
  }
  return ok;
}

bool
client_sample_does_not_include_server_implementation (
  const std::filesystem::path &root,
  const std::filesystem::path &client_root)
{
  bool ok = true;
  const auto path = root / client_root;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator (path)) {
    if (!entry.is_regular_file ()) {
      continue;
    }
    const auto ext = entry.path ().extension ();
    if (ext != ".hpp" && ext != ".cpp") {
      continue;
    }

    std::ifstream input (entry.path ());
    std::string line;
    std::size_t line_no = 0;
    while (std::getline (input, line)) {
      ++line_no;
      if (line.find ("../Server/") != std::string::npos ||
          line.find ("Server/") != std::string::npos) {
        std::cerr << "client sample references server implementation: "
                  << entry.path () << ':' << line_no << '\n';
        ok = false;
      }
    }
  }
  return ok;
}

bool
file_does_not_contain (const std::filesystem::path &path,
                       const std::string &needle,
                       const std::string &message)
{
  if (!file_contains (path, needle)) {
    return true;
  }
  std::cerr << message << ": " << path << '\n';
  return false;
}

} // namespace

int
main ()
{
  const std::filesystem::path root { ZLINK_FRAMEWORK_CPP_SOURCE_DIR };

  bool ok = true;
  ok &= require_exists (
    root / "framework/include/zlink/framework/contracts");
  ok &= require_exists (
    root / "framework/include/zlink/framework/contracts/assembly");
  ok &= require_exists (
    root / "framework/include/zlink/framework/contracts/detail/message_name.hpp");
  ok &= require_exists (root / "framework/src/runtime");
  ok &= require_exists (root / "framework/src/runtime/backend/contracts");
  ok &= require_exists (
    root / "framework/src/runtime/backend/native_route_backend.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/backend/native_route_backend.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_packet_dispatcher.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_packet_dispatcher.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_pending_requests.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_pending_requests.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_reply_writer.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_reply_writer.hpp");
  ok &= require_exists (root / "framework/src/runtime/execution");
  ok &= require_exists (
    root / "framework/src/runtime/execution/serial_execution_queue.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/execution/serial_execution_queue.hpp");
  ok &= require_exists (root / "framework/src/runtime/messaging");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/client_call_codec.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/client_call_codec.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/envelope_codec.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/envelope_codec.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/pending_operation.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/pending_operation_state.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/pending_submit.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/pending_submit.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/request_failure_mapper.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/request_failure_mapper.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/submit_queue.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/messaging/submit_queue.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_runtime_bundle.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_runtime_bundle.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_bundle_factory.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_bundle_factory.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_runtime_manager.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_runtime_manager.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_message_pump.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_message_pump.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_receive_loop.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/channel_receive_loop.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_connection_set.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_connection_set.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_channel_registration.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_channel_registration.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_channel_runtime.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_channel_runtime.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_handler_registry.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_handler_registry.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_handler_invoker.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_handler_invoker.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_internal_packet_dispatcher.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_internal_packet_dispatcher.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_packet.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_packet_dispatcher.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_packet_dispatcher.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_receive_pump.cpp");
  ok &= require_exists (
    root / "framework/src/runtime/channels/route_receive_pump.hpp");
  ok &= require_exists (
    root / "framework/src/runtime/configuration/builders");
  ok &= require_exists (
    root /
    "framework/src/runtime/configuration/builders/configuration_builder.cpp");
  ok &= require_exists (
    root / "connector/include/zlink/stream_connector/contracts");
  ok &= require_exists (
    root / "connector/include/zlink/stream_connector/contracts/calls");
  ok &= require_exists (
    root /
    "connector/include/zlink/stream_connector/contracts/calls/zlink_stream_calls.hpp");
  ok &= require_exists (
    root /
    "connector/include/zlink/stream_connector/contracts/zlink_stream_connector_options.hpp");
  ok &= require_exists (
    root /
    "connector/include/zlink/stream_connector/contracts/zlink_stream_models.hpp");
  ok &= require_exists (root / "connector/src/runtime");
  ok &= require_exists (root / "connector/src/runtime/calls");
  ok &= require_exists (root / "connector/src/runtime/protocol");
  ok &= require_exists (root / "connector/src/runtime/protocol/compression");
  ok &= require_exists (root / "connector/src/runtime/protocol/framing");
  ok &= require_exists (root / "connector/src/runtime/transport");
  ok &= require_exists (
    root / "connector/src/runtime/connector_lifecycle.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/heartbeat_monitor.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/calls/zlink_stream_calls.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/protocol/compression/lz4_compression_codec.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/protocol/framing/frame_codec.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/protocol/framing.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/protocol/header_codec.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/protocol/metadata_codec.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/protocol/packet_name_resolver.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/transport/stream_connection.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/transport/stream_transport_factory.cpp");
  ok &= require_exists (
    root / "connector/src/runtime/transport/websocket_connection.cpp");
  ok &= require_exists (root / "connector/src/runtime/backend/contracts");
  ok &= require_exists (root / "http-client/include/zlink/http_client.hpp");
  ok &= require_exists (
    root / "http-client/include/zlink/http_client/contracts/client.hpp");
  ok &= require_exists (root / "http-client/src/runtime");
  ok &= require_exists (
    root / "http-client/src/runtime/http_client_runtime.hpp");
  ok &= require_exists (
    root / "http-client/src/runtime/http_client_runtime.cpp");
  ok &= require_exists (root / "tests/Zlink.Framework.UnitTests");
  ok &= require_exists (root / "tests/Zlink.Framework.ContractTests");
  ok &= require_exists (root / "tests/Zlink.Framework.E2ETests");
  ok &= require_exists (
    root / "tests/Systems.Zlink.Stream.Connector.Tests");
  ok &= require_exists (
    root / "tests/Zlink.Unreal.Stream.Connector.Tests");
  ok &= require_exists (
    root /
    "tests/Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp");
  ok &= require_exists (
    root /
    "tests/Zlink.Framework.UnitTests/test_cpp_framework_handler_registry.cpp");
  ok &= require_exists (
    root /
    "tests/Zlink.Framework.E2ETests/Samples/verify_sample_client_log.cmake");
  ok &= require_exists (
    root /
    "tests/Systems.Zlink.Stream.Connector.Tests/test_cpp_stream_connector.cpp");
  ok &= require_exists (
    root /
    "tests/Zlink.Unreal.Stream.Connector.Tests/test_unreal_stream_connector.cpp");
  ok &= require_exists (
    root / "unreal-connector/Source/ZLinkStreamConnector/Public");
  ok &= require_exists (
    root / "unreal-connector/Source/ZLinkStreamConnector/Private");
  ok &= require_exists (
    root /
    "unreal-connector/Source/ZLinkStreamConnector/Private/ZLinkStreamConnectorAutomationTests.cpp");
  ok &= require_exists (
    root / "samples/Bingo/Shared/Configuration/sample_names.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Shared/Configuration/sample_topology.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Shared/Contracts/messages.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Api/Handlers/authenticate_player_handler.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Api/api_server_host_factory.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Api/api_server_framework.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Api/Handlers/match_bingo_handler.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/BingoRoomSpots/bingo_room.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/Actors/player_actor.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/Actors/player_actor_factory.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/BingoRoomSpots/bingo_card.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/BingoRoomSpots/bingo_notification_publisher.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/BingoRoomSpots/bingo_room_models.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/BingoRoomSpots/bingo_room_spot.hpp");
  ok &= require_exists (
    root /
    "samples/Bingo/Server/Play/BingoRoomSpots/Handlers/bingo_room_join_handler.hpp");
  ok &= require_exists (
    root /
    "samples/Bingo/Server/Play/BingoRoomSpots/Handlers/start_bingo_game_handler.hpp");
  ok &= require_exists (
    root /
    "samples/Bingo/Server/Play/BingoRoomSpots/Handlers/bingo_room_timer_handler.hpp");
  ok &= require_absent (
    root / "samples/Bingo/Server/Play/BingoRoomSpots/bingo_room_handlers.hpp",
    "sample handler aggregate headers hide the real .NET-aligned Handlers owner");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/EntrySpot/bingo_entry_spot.hpp");
  ok &= require_exists (
    root /
    "samples/Bingo/Server/Play/EntrySpot/Handlers/match_bingo_actor_handler.hpp");
  ok &= require_absent (
    root / "samples/Bingo/Server/Play/EntrySpot/match_bingo_actor_handler.hpp",
    "sample handler wrappers hide the real .NET-aligned Handlers owner");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/Handlers/allocate_bingo_room_handler.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/Handlers/bingo_room_directory.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/Handlers/ensure_player_actor_handler.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Play/play_server_host_factory.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Registry/registry_host_factory.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Session/main.cpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Session/session_server_host_factory.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Server/Session/Sessions/bingo_session.hpp");
  ok &= require_exists (
    root /
    "samples/Bingo/Server/Session/Sessions/Handlers/authenticate_session_handler.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Client/bingo_notification_inbox.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Client/bingo_client_options.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Client/bingo_player_client.hpp");
  ok &= require_exists (
    root / "samples/Bingo/Client/bingo_client_app.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Shared/Actors/player_actor.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Shared/Configuration/sample_names.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Shared/Configuration/sample_topology.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Shared/Contracts/messages.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Api/Handlers/authenticate_actor_handler.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Api/api_server_host_factory.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Api/Handlers/create_match_handler.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Api/api_server_framework.hpp");
  ok &= require_exists (
    root /
    "samples/TicTacToe/Server/Play/EntrySpot/Handlers/join_match_handler.hpp");
  ok &= require_absent (
    root / "samples/TicTacToe/Server/Play/EntrySpot/join_match_handler.hpp",
    "sample handler wrappers hide the real .NET-aligned Handlers owner");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Play/GameSpots/tictactoe_match_room.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Play/GameSpots/game_notification_publisher.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Play/GameSpots/tictactoe_game_contract_mapper.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Play/GameSpots/tictactoe_game_models.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Play/GameSpots/tictactoe_game_spot.hpp");
  ok &= require_exists (
    root /
    "samples/TicTacToe/Server/Play/GameSpots/Handlers/tictactoe_game_join_handler.hpp");
  ok &= require_exists (
    root /
    "samples/TicTacToe/Server/Play/GameSpots/Handlers/place_mark_handler.hpp");
  ok &= require_absent (
    root / "samples/TicTacToe/Server/Play/GameSpots/place_mark_handler.hpp",
    "sample handler wrappers hide the real .NET-aligned Handlers owner");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Play/Handlers/create_match_room_handler.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Play/Handlers/ensure_player_actor_handler.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Play/play_server_host_factory.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Registry/registry_host_factory.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Server/Session/session_server_host_factory.hpp");
  ok &= require_exists (
    root /
    "samples/TicTacToe/Server/Session/Sessions/session_relay_session.hpp");
  ok &= require_exists (
    root /
    "samples/TicTacToe/Server/Session/Sessions/Handlers/authenticate_session_packet_handler.hpp");
  ok &= require_exists (
    root /
    "samples/TicTacToe/Server/Session/Sessions/Handlers/create_match_session_packet_handler.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Client/session_actor_notification_inbox.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Client/tictactoe_client_options.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Client/tictactoe_player_client.hpp");
  ok &= require_exists (
    root / "samples/TicTacToe/Client/tictactoe_client.hpp");

  ok &= client_sample_uses_connector (
    root, "samples/Bingo/Client/bingo_player_client.hpp");
  ok &= client_sample_uses_connector (
    root, "samples/TicTacToe/Client/tictactoe_player_client.hpp");
  ok &= client_sample_does_not_include_server_implementation (
    root, "samples/Bingo/Client");
  ok &= client_sample_does_not_include_server_implementation (
    root, "samples/TicTacToe/Client");
  ok &= file_does_not_contain (
    root / "connector/src/runtime/connector_runtime.hpp",
    "socket_fd",
    "C++ connector runtime must not use raw fd state");
  ok &= file_does_not_contain (
    root / "connector/src/runtime/connector_runtime.hpp",
    "recv(",
    "C++ connector runtime must not expose raw recv state");
  ok &= file_contains (
    root / "framework/src/runtime/handlers/handler_registry.cpp",
    "runtime::handler_coroutine_executor ().submit");
  ok &= file_contains (
    root / "framework/src/runtime/handlers/handler_registry.cpp",
    "co_await runtime::await_task_result");
  ok &= file_contains (
    root / "framework/src/runtime/http/http_host_service.cpp",
    "handler_coroutine_executor ().submit");
  ok &= file_contains (
    root / "framework/src/runtime/http/http_host_service.cpp",
    "co_await await_task_result");
  ok &= file_contains (
    root / "framework/src/runtime/spots/spot_runtime.cpp",
    "runtime::handler_coroutine_executor ().submit");
  ok &= file_contains (
    root / "framework/src/runtime/spots/spot_runtime.cpp",
    "co_await runtime::await_task_result");
  ok &= file_contains (
    root / "framework/src/runtime/streams/stream_runtime.cpp",
    "runtime::handler_coroutine_executor ().submit");
  ok &= file_contains (
    root / "framework/src/runtime/streams/stream_runtime.cpp",
    "co_await runtime::await_task_result");
  ok &= file_does_not_contain (
    root / "framework/src/runtime/channels/route_handler_invoker.cpp",
    ".result (",
    "route handler dispatch must await task_t instead of blocking with result()");
  ok &= file_does_not_contain (
    root / "framework/src/runtime/channels/route_handler_invoker.cpp",
    ".result(",
    "route handler dispatch must await task_t instead of blocking with result()");
  ok &= file_does_not_contain (
    root / "connector/include/zlink/stream_connector/contracts/calls/zlink_stream_calls.hpp",
    ".result",
    "connector callback submit must observe task completion instead of blocking");
  ok &= file_does_not_contain (
    root / "connector/include/zlink/stream_connector/codecs/auto_codec.hpp",
    ".result",
    "connector auto codec callback submit must observe task completion instead of blocking");
  ok &= file_does_not_contain (
    root / "connector/include/zlink/stream_connector/codecs/auto_codec.hpp",
    "on_completed ([&",
    "connector auto codec must not depend on immediate callback completion");
  ok &= file_does_not_contain (
    root / "connector/include/zlink/stream_connector/codecs/auto_codec.hpp",
    "std::optional<result_t",
    "connector auto codec must let task_t own result storage details");
  ok &= file_does_not_contain (
    root / "connector/src/runtime/connector_runtime.cpp",
    ".result",
    "connector send callback submit must observe task completion instead of blocking");
  ok &= file_does_not_contain (
    root /
      "unreal-connector/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp",
    "zlink/stream_connector",
    "Unreal connector must not wrap the general C++ connector runtime");
  ok &= file_does_not_contain (
    root / "unreal-connector/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h",
    "zlink/stream_connector",
    "Unreal public API must not include the general C++ connector surface");
  ok &= file_does_not_contain (
    root / "unreal-connector/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h",
    "task_t",
    "Unreal public API must not expose connector coroutine task_t");
  ok &= file_does_not_contain (
    root / "unreal-connector/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h",
    "<coroutine>",
    "Unreal public API must not include coroutine support");
  ok &= file_does_not_contain (
    root / "unreal-connector/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h",
    "co_await",
    "Unreal public API must not expose coroutine await syntax");
  ok &= file_does_not_contain (
    root / "unreal-connector/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h",
    "submit",
    "Unreal public API must use delegates instead of connector submit calls");
  ok &= file_does_not_contain (
    root / "CMakeLists.txt",
    "target_link_libraries(zlink_unreal_stream_connector PUBLIC zlink::stream_connector)",
    "Unreal connector target must not publicly wrap the general C++ connector");
  ok &= file_does_not_contain (
    root / "CMakeLists.txt",
    "target_link_libraries(zlink_unreal_stream_connector PRIVATE zlink::stream_connector)",
    "Unreal connector target must not privately wrap the general C++ connector");
  ok &= file_does_not_contain (
    root / "CMakeLists.txt",
    "target_include_directories(zlink_unreal_stream_connector PRIVATE\n  ${ZLINK_FRAMEWORK_CPP_DIR}/connector/src)",
    "Unreal connector target must not include general connector runtime internals");
  ok &= file_contains (
    root / "CMakeLists.txt",
    "option(ZLINK_STREAM_CONNECTOR_WITH_JSON \"Enable Stream Connector JSON helpers\" ON)");
  ok &= file_contains (
    root / "CMakeLists.txt",
    "option(ZLINK_STREAM_CONNECTOR_WITH_LZ4 \"Enable Stream Connector LZ4 compression\" ON)");
  ok &= file_contains (
    root / "unreal-connector/Source/ZLinkStreamConnector/ZLinkStreamConnector.Build.cs",
    "\"Sockets\"");
  ok &= file_contains (
    root / "unreal-connector/Source/ZLinkStreamConnector/ZLinkStreamConnector.Build.cs",
    "\"Networking\"");
  ok &= file_contains (
    root /
      "unreal-connector/Source/ZLinkStreamConnector/Private/ZLinkStreamConnectorAutomationTests.cpp",
    "IMPLEMENT_SIMPLE_AUTOMATION_TEST");
  ok &= file_contains (
    root /
      "unreal-connector/Source/ZLinkStreamConnector/Private/ZLinkStreamConnectorAutomationTests.cpp",
    "FSocket");

  ok &= public_headers_do_not_include_runtime (
    root / "framework/include");
  ok &= public_headers_do_not_include_runtime (
    root / "connector/include");
  ok &= public_headers_do_not_include_runtime (
    root / "http-client/include");
  ok &= public_headers_do_not_expose_runtime_dependencies (
    root / "framework/include");
  ok &= public_headers_do_not_expose_runtime_dependencies (
    root / "connector/include");
  ok &= public_headers_do_not_expose_runtime_dependencies (
    root / "http-client/include");
  ok &= public_headers_do_not_expose_runtime_dependencies (
    root / "unreal-connector/Source/ZLinkStreamConnector/Public");
  ok &= sample_application_code_uses_message_codec (root);
  ok &= sample_server_code_does_not_block_on_task_result (root);
  ok &= contract_headers_have_compile_coverage (
    root,
    "framework/include",
    "");
  ok &= contract_headers_have_compile_coverage (
    root,
    "connector/include",
    "");
  ok &= contract_headers_have_compile_coverage (
    root,
    "http-client/include",
    "");
  ok &= posd_log_has_current_goal_mapping (root);
  ok &= cmake_extension_boundaries_hold (root);

  return ok ? 0 : 1;
}
