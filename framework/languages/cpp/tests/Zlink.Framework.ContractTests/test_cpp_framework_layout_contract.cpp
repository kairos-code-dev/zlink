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

bool require_exists (const std::filesystem::path &path)
{
    if (std::filesystem::exists (path)) {
        return true;
    }
    std::cerr << "missing required path: " << path << '\n';
    return false;
}

bool require_absent (const std::filesystem::path &path, const std::string &reason)
{
    if (!std::filesystem::exists (path)) {
        return true;
    }
    std::cerr << "unexpected path: " << path << " (" << reason << ")\n";
    return false;
}

bool public_headers_do_not_include_runtime (const std::filesystem::path &root)
{
    bool ok = true;
    for (const auto &entry : std::filesystem::recursive_directory_iterator (root)) {
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
            if (line.find ("src/runtime") != std::string::npos
                || line.find ("/Private/") != std::string::npos
                || line.find ("Private/") != std::string::npos) {
                std::cerr << "public header references runtime implementation: " << entry.path ()
                          << ':' << line_no << '\n';
                ok = false;
            }
        }
    }
    return ok;
}

bool public_headers_do_not_expose_runtime_dependencies (const std::filesystem::path &root)
{
    bool ok = true;
    const std::string forbidden[] = {"#include <boost",
                                     "#include \"boost",
                                     "boost::asio",
                                     "boost::beast",
                                     "boost::asio::awaitable",
                                     "#include <future>",
                                     "std::future",
                                     "std::promise",
                                     "cancellation_token",
                                     "cancellation_source",
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
                                     "spdlog",
                                     "spdlog::",
                                     "#include <fmt",
                                     "fmt::",
                                     "#include <nlohmann",
                                     "nlohmann::",
                                     "#include <msgpack",
                                     "msgpack::",
                                     "#include <google/protobuf",
                                     "google::protobuf",
                                     "#include <kafka",
                                     "#include <Kafka",
                                     "Kafka::",
                                     "RdKafka",
                                     "#include <grpc",
                                     "#include <gRPC",
                                     "grpc::",
                                     "#include <yaml",
                                     "#include <YAML",
                                     "YAML::",
                                     "#include <flatbuffers",
                                     "#include <FlatBuffers",
                                     "flatbuffers::",
                                     "#include <zlink.hpp",
                                     "#include <zlink/Contracts/Sockets",
                                     "#include <zlink/Contracts/Service",
                                     "zlink::context_t",
                                     "zlink::router_socket_t",
                                     "zlink::stream_socket_t",
                                     "zlink::dealer_socket_t",
                                     "zlink::pub_socket_t",
                                     "zlink::sub_socket_t"};

    for (const auto &entry : std::filesystem::recursive_directory_iterator (root)) {
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
                    std::cerr << "public header exposes runtime/test dependency: " << entry.path ()
                              << ':' << line_no << " contains " << needle << '\n';
                    ok = false;
                }
            }
        }
    }
    return ok;
}

bool file_contains_quiet (const std::filesystem::path &path, const std::string &needle)
{
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    return buffer.str ().find (needle) != std::string::npos;
}

bool file_contains (const std::filesystem::path &path, const std::string &needle)
{
    if (file_contains_quiet (path, needle)) {
        return true;
    }
    std::cerr << "file lacks required text: " << path << " :: " << needle << '\n';
    return false;
}

std::size_t count_occurrences (const std::string &text, const std::string &needle)
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

bool draft_tracking_table_matches_files (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("## 6. 참고 문서 추적표");
    const auto end = text.find ("## 7. 기능 축 추적표");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks a bounded reference document tracking table: "
                  << path << '\n';
        return false;
    }
    const auto table = text.substr (start, end - start);

    bool ok = true;
    std::size_t file_count = 0;
    const std::pair<const char *, const char *> scopes[] = {{"../../doc/framework/cpp/spec", "(../spec/"},
                                                            {"../../doc/framework/cpp/internals", "("}};
    for (const auto &[scope_dir, link_prefix] : scopes) {
        for (const auto &entry : std::filesystem::directory_iterator (root / scope_dir)) {
            if (!entry.is_regular_file () || entry.path ().extension () != ".md") {
                continue;
            }
            const auto filename = entry.path ().filename ().generic_string ();
            if (filename.size () < 6 || filename.substr (filename.size () - 6) != ".ko.md") {
                continue;
            }
            ++file_count;
            const auto link = std::string (link_prefix) + filename + ")";
            const auto link_count = count_occurrences (table, link);
            if (link_count != 1) {
                std::cerr << "reference document tracking table must reference exactly once: "
                          << filename << " (got " << link_count << ")\n";
                ok = false;
            }
        }
    }

    // 별도 산출물의 계약 문서도 추적표에 정확히 한 번 올린다.
    const std::string artifact_links[] = {
      "(../../../stream-connector/cpp/guide/INDEX.ko.md)",
      "(../../../http-client/cpp/spec/cpp-http-client.ko.md)"};
    for (const auto &link : artifact_links) {
        if (count_occurrences (table, link) != 1) {
            std::cerr << "reference document tracking table must reference exactly once: " << link
                      << '\n';
            ok = false;
        }
    }

    const auto tracked_count = count_occurrences (table, ".ko.md)");
    if (tracked_count != file_count + std::size (artifact_links)) {
        std::cerr << "reference document tracking table count mismatch: tracked " << tracked_count
                  << ", files " << file_count + std::size (artifact_links) << '\n';
        ok = false;
    }
    return ok;
}

bool non_empty_directories_do_not_keep_gitkeep (const std::filesystem::path &root)
{
    bool ok = true;
    const std::filesystem::path roots[] = {
      root / "framework/include",
      root / "framework/src/runtime",
      root / "connector/core/include",
      root / "connector/core/src/runtime",
      root / "http-client/include",
      root / "http-client/src/runtime",
      root / "extensions/include",
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public",
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private"};

    for (const auto &scan_root : roots) {
        if (!std::filesystem::exists (scan_root)) {
            continue;
        }
        for (const auto &entry : std::filesystem::recursive_directory_iterator (scan_root)) {
            if (!entry.is_regular_file () || entry.path ().filename () != ".gitkeep") {
                continue;
            }

            const auto dir = entry.path ().parent_path ();
            bool has_real_entry = false;
            for (const auto &sibling : std::filesystem::directory_iterator (dir)) {
                if (sibling.path ().filename () != ".gitkeep") {
                    has_real_entry = true;
                    break;
                }
            }
            if (has_real_entry) {
                std::cerr << "non-empty framework directory still keeps placeholder: "
                          << entry.path () << '\n';
                ok = false;
            }
        }
    }
    return ok;
}

bool framework_overview_role_tables_cover_files (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-overview.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("## 2. 문서 구조와 역할 분담");
    const auto end = text.find ("## 3. 핵심 방향");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "framework overview lacks bounded document role tables: " << path << '\n';
        return false;
    }
    const auto tables = text.substr (start, end - start);

    bool ok = true;
    const std::pair<const char *, const char *> scopes[] = {{"../../doc/framework/cpp/spec", "(../spec/"},
                                                            {"../../doc/framework/cpp/internals", "("}};
    for (const auto &[scope_dir, link_prefix] : scopes) {
        for (const auto &entry : std::filesystem::directory_iterator (root / scope_dir)) {
            if (!entry.is_regular_file () || entry.path ().extension () != ".md") {
                continue;
            }
            const auto filename = entry.path ().filename ().generic_string ();
            if (filename == "cpp-framework-overview.ko.md" || filename.size () < 6
                || filename.substr (filename.size () - 6) != ".ko.md") {
                continue;
            }
            const auto link = std::string (link_prefix) + filename + ")";
            if (tables.find (link) == std::string::npos) {
                std::cerr << "framework overview role tables do not describe: " << filename
                          << '\n';
                ok = false;
            }
        }
    }
    return ok;
}

bool implementation_plan_expands_label_wildcards (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("| wildcard label | concrete label |");
    const auto end = text.find ("## 8. Goal 실행용 문구");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks label wildcard expansion table: " << path << '\n';
        return false;
    }
    const auto table = text.substr (start, end - start);

    bool ok = true;
    const std::string rows[] = {
      "| `http-client-*` | `http-client-contract`, `http-client-unit`, `http-client-e2e`, "
      "`http-client-https`, "
      "`http-client-regression` |",
      "| `connector-*` | `connector-unit`, `connector-integration`, `connector-e2e`, "
      "`connector-contract`, "
      "`connector-protocol`, `connector-transport`, `connector-typed`, `connector-package` |",
      "| `connector-unreal-*` | `connector-unreal-contract`, `connector-unreal-compile`, "
      "`connector-unreal-smoke` |",
      "| `framework-sample-*` | `framework-sample-smoke`, `framework-sample-parity`, "
      "`framework-sample-api`, "
      "`framework-sample-bingo`, `framework-sample-play`, "
      "`framework-sample-registry`, "
      "`framework-sample-session`, `framework-sample-tictactoe` |"};
    for (const auto &row : rows) {
        if (table.find (row) == std::string::npos) {
            std::cerr << "implementation plan lacks label wildcard expansion: " << row << '\n';
            ok = false;
        }
    }
    return ok;
}

bool implementation_plan_goal20_covers_connector_labels (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 20. Stream Connectors");
    const auto end = text.find ("### Goal 21. Review Samples");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 20 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L connector-unit",
      "ctest --test-dir framework/languages/cpp/build -L connector-integration",
      "ctest --test-dir framework/languages/cpp/build -L connector-e2e",
      "ctest --test-dir framework/languages/cpp/build -L connector-contract",
      "ctest --test-dir framework/languages/cpp/build -L connector-protocol",
      "ctest --test-dir framework/languages/cpp/build -L connector-transport",
      "ctest --test-dir framework/languages/cpp/build -L connector-typed",
      "ctest --test-dir framework/languages/cpp/build -L connector-package",
      "ctest --test-dir framework/languages/cpp/build -L connector-unreal-contract",
      "ctest --test-dir framework/languages/cpp/build -L connector-unreal-compile",
      "ctest --test-dir framework/languages/cpp/build -L connector-unreal-smoke"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 20 verification commands lack: " << command << '\n';
            ok = false;
        }
    }
    return ok;
}

bool implementation_plan_goal6_covers_parity_model (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 6. Application Framework Parity Model");
    const auto end = text.find ("### Goal 7. Runtime Integration And Execution");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 6 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "`.NET Core` / `ASP.NET Core` benchmark mapping",
      "app host, DI, configuration, logging, lifecycle 통합",
      "HTTP hosting, zlink messaging, timer, hosted service 통합",
      "handler, middleware/filter, validation, error mapping 공통 모델",
      "security/auth extension point",
      "scheduling/background work model",
      "developer convenience model",
      "regression label taxonomy",
      "HTTP, zlink, timer, hosted service가 서로 다른 framework처럼 보이지 않는다",
      "모든 handler 계열은 DTO, `dependency_types`, DI scope, `task_t<T>`, logging, error",
      "mapping 규칙을 공유한다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 6 parity model lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-contract",
      "ctest --test-dir framework/languages/cpp/build -L framework-host",
      "ctest --test-dir framework/languages/cpp/build -L framework-regression -R parity"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 6 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal7_covers_runtime_integration (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 7. Runtime Integration And Execution");
    const auto end = text.find ("### Goal 8. Handler Registry And Serializer");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 7 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "`zlink::context_t` lifecycle owner",
      "channel/stream/discovery/registry/spot lifecycle owner",
      "CAPI dispatch callback 등록과 recv 처리",
      "typed handler projection",
      "CAPI timer event projection",
      "core ordering 보존",
      "framework offload executor",
      "runtime drain",
      "transport abstraction",
      "endpoint URI validation",
      "TCP, IPC, TLS, WebSocket transport support through zlink core",
      "public API가 CAPI handle, socket recv, poller slot을 노출하지 않는다",
      "모든 handler는 coroutine executor를 통과한다",
      "CPU-bound handler는 offload executor에서 실행 가능하다",
      "offload executor는 shutdown에서 drain된다",
      "별도 event loop",
      "public API로"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 7 runtime integration lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-unit -R runtime",
      "ctest --test-dir framework/languages/cpp/build -L framework-integration"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 7 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal8_covers_handler_serializer_model (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 8. Handler Registry And Serializer");
    const auto end = text.find ("### Goal 9. Channel Messaging");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 8 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "`handler_registry_t`",
      "options.handlers().add<THandler>(group)",
      "handler alias: `request_type`, `reply_type`, `spot_type`, `actor_type`",
      "handler `topic_name`",
      "typed deserialize/serialize",
      "handler DI resolve",
      "handler exception mapping",
      "`serializer_registry_t`",
      "JSON serializer 기본값",
      "custom serializer 등록",
      "channel, topic, spot, actor, request, reply type을 반복해서 나열하지 않는다",
      "type 정보는 handler class alias와 `topic_name`에서 읽는다",
      "decode 실패는 `payload_decode_failed`",
      "serializer registry 구현은 public header에 노출하지 않는다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 8 handler/serializer model lacks required contract: " << needle
                      << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-unit -R handler",
      "ctest --test-dir framework/languages/cpp/build -L framework-unit -R serializer"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 8 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal9_covers_channel_messaging (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 9. Channel Messaging");
    const auto end = text.find ("### Goal 10. Backpressure And Reliability");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 9 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "request/reply, send/event, pub/sub, route channel, outbound client",
      "`add_client_server_channel(...)`",
      "server/client/publisher/subscriber capability",
      "`bind(...)`",
      "`connect(...)`",
      "`use_discovery(...)`",
      "`request_client_t`",
      "`publisher_t`",
      "request timeout",
      "reply correlation",
      "outbound-only host",
      "route channel",
      "handler not found mapping",
      "disconnected result",
      "channel messaging 기본 호출은 channel name 기준",
      "같은 capability 안에서 discovery와 manual connection을 섞지 않는다",
      "pending queue 한도 초과는 `request_rejected`",
      "route handler가 없으면 request는 handler not found 계열 error로 닫힌다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 9 channel messaging lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-channel",
      "ctest --test-dir framework/languages/cpp/build -L framework-regression -R channel"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 9 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal10_covers_backpressure_reliability (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 10. Backpressure And Reliability");
    const auto end = text.find ("### Goal 11. SPOT Runtime");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 10 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "send readiness, bounded queue, timeout, retry/dead-letter extension point",
      "nonblocking send",
      "send-ready runtime integration",
      "HWM awareness",
      "bounded pending queue",
      "timeout 처리",
      "disconnected 처리",
      "shutdown 처리",
      "explicit retry hook",
      "dead-letter extension point",
      "idempotency key hook",
      "graceful close와 drain",
      "public non-blocking 옵션으로 책임을 사용자에게 넘기지 않는다",
      "timeout 전 send-ready가 오면 pending 작업을 drain한다",
      "shutdown 중 pending 작업은 graceful drain 또는 `shutdown` 실패",
      "slow subscriber나 disconnected subscriber"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 10 backpressure/reliability lacks required contract: " << needle
                      << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-unit -R backpressure",
      "ctest --test-dir framework/languages/cpp/build -L framework-regression -R reliability"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 10 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal11_covers_spot_runtime (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 11. SPOT Runtime");
    const auto end = text.find ("### Goal 12. SPOT Timer");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 11 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "SPOT node, Entry Spot, user Spot, actor handler",
      "`spot_node_builder_t`",
      "`spot_t`",
      "`entry_spot_t`",
      "`spot_context_t`",
      "`spot_context_t::publish(...)`",
      "`spot_context_t::request_to(...)`",
      "`spot_context_t::handlers()`",
      "`spot_handler_registry_t`",
      "`add_handler<&TSpot::method>()`",
      "`add_subscribe<&TSpot::method>(topic)`",
      "`add_actor_packet<&TSpot::method>()`",
      "`onDisconnectActor(actor)`",
      "`on_actor_join(actor, message_t)`",
      "`on_actor_joined(actor)`",
      "`onLeaveActor(actor)`",
      "`spot_context_t::close()`",
      "`spot_create_result_t`",
      "Entry Spot",
      "user Spot lifecycle",
      "Registry-backed Spot lookup",
      "SPOT node lifecycle은 app host가 관리한다",
      "core SPOT dispatch ordering",
      "Spot 등록부에서는 Spot member function만 나열한다",
      "일반 Spot은 `spot_t`, Entry Spot은 `entry_spot_t`를 상속한다",
      "compile-time으로 검증한다",
      "actor join admission은 registry handler가 아니라 user Spot member callback으로 처리하고",
      "request/reply는 DTO generic이 아니라 `message_t`를 사용한다",
      "actor join/post-join/left/disconnected lifecycle은 Spot member callback 이름이 계약이다",
      "Spot create result는 `existing`, `created`, `rejected` state와 optional reply message를",
      "Play sample smoke는 handler 객체 직접 호출만으로 통과하지 않는다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 11 SPOT runtime lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-spot",
      "ctest --test-dir framework/languages/cpp/build -L framework-regression -R spot"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 11 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool actor_model_documents_actor_destroy_lifecycle (const std::filesystem::path &root)
{
    const auto path = root.parent_path ().parent_path () / "doc/framework/common/actor-model.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    bool ok = true;
    const std::string required[] = {
      "`leaveActor`와 `destroyActor`는 서로 다른 책임이다",
      "`leaveActor`는 actor 위치를 user",
      "Entry Spot에 돌아온 actor의 수명을 끝내는 작업",
      "actor registry, actor-session",
      "native actor ref",
      "`onCreateActor` callback을 한 번 호출한다",
      "`destroyActor`는 위치 이동이 아니라 actor 수명 종료",
      "다른 lifecycle callback을 호출하지 않고",
      "stream disconnect는",
      "`onDisconnectActor`만 의미하며",
      "disconnect cleanup만으로 actor destroy가 실행되지 않는다",
      "| leaveActor | user Spot",
      "| destroyActor | Entry Spot actor 정리",
      "| disconnect | current stream binding 해제"};
    for (const auto &needle : required) {
        if (text.find (needle) == std::string::npos) {
            std::cerr << "actor model lacks actor destroy lifecycle contract: " << needle
                      << '\n';
            ok = false;
        }
    }
    return ok;
}

bool framework_api_documents_actor_destroy_lifecycle (const std::filesystem::path &root)
{
    const auto path = root.parent_path ().parent_path () / "doc/framework/common/framework-api.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    bool ok = true;
    const std::string required[] = {
      "actor를 완전히 제거하는 public API는 Entry Spot context에만 둔다",
      "user Spot",
      "`leaveActor` 의미의 API까지만",
      "Entry Spot handler 또는 lifecycle callback은",
      "언어별 Entry Spot destroy API를 호출한다",
      "actor registry, actor-session binding, native actor ref를",
      "`onLeaveActor`나 다른 lifecycle",
      "stream disconnect는 현재 session binding cleanup",
      "disconnect cleanup만으로 actor destroy가 실행되지 않는다"};
    for (const auto &needle : required) {
        if (text.find (needle) == std::string::npos) {
            std::cerr << "framework API spec lacks actor destroy lifecycle contract: "
                      << needle << '\n';
            ok = false;
        }
    }
    return ok;
}

bool session_actor_dispatch_documents_disconnect_destroy_boundary (
  const std::filesystem::path &root)
{
    const auto path =
      root.parent_path ().parent_path () / "doc/framework/common/session-actor-dispatch.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    bool ok = true;
    const std::string required[] = {
      "disconnect unbind는 actor-session binding만 정리한다",
      "room leave, Entry Spot 복귀,",
      "actor destroy는 disconnect cleanup에서 자동으로 실행하지 않는다",
      "user Spot에서",
      "`leaveActor`로 actor를 Entry Spot에 돌려보낸 뒤",
      "Entry Spot context의 destroy API를",
      "| disconnect does not destroy |",
      "room leave나 Entry Spot destroy를 자동으로 실행하지 않는다"};
    for (const auto &needle : required) {
        if (text.find (needle) == std::string::npos) {
            std::cerr
              << "session actor dispatch spec lacks disconnect/destroy boundary contract: "
              << needle << '\n';
            ok = false;
        }
    }
    return ok;
}

bool implementation_plan_goal12_covers_spot_timer (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 12. SPOT Timer");
    const auto end = text.find ("### Goal 13. STREAM Framework");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 12 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "CAPI timer",
      "`.NET` framework timer",
      "`timer_t`",
      "`timer_options_t`",
      "`timer_overrun_policy_t`",
      "`timer_tick_t`",
      "`spot_context_t::add_timer<THandler>(...)`",
      "CAPI timer lifecycle",
      "CAPI timer dispatch event projection",
      "CAPI SPOT dispatch event 후 timer recv 경계",
      "`fire_count` 기반 skipped tick 계산",
      "`scheduled_index`",
      "`skip_late_ticks`",
      "`catch_up_bounded`",
      "`delay_next_tick`",
      "same timer instance 재진입 금지",
      "timer handler exception monitoring",
      "native timer handle, poller slot, timer recv 순서",
      "user Spot timer는 같은 Spot의 packet/subscription/channel reply와 같은 CAPI SPOT dispatch",
      "Entry Spot timer는 Entry Spot 전체를 전역 직렬화하지 않는다",
      "별도 실행 직렬화 queue를 추가하지 않는다",
      "timer failure event는 snapshot interval을 기다리지 않고 발생한다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 12 SPOT timer lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-spot -R timer",
      "ctest --test-dir framework/languages/cpp/build -L framework-regression -R timer"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 12 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal13_covers_stream_framework (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 13. STREAM Framework");
    const auto end = text.find ("### Goal 14. ActorGateway Session Relay");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 13 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "Header 기반 packet stream",
      "`stream_builder_t`",
      "`stream_t`",
      "`packet_stream_session_t`",
      "`stream_header_t`",
      "`stream_error_t`",
      "STREAM bind",
      "packet session registration",
      "lifecycle callback",
      "packet callback",
      "packet reply",
      "`stream_t::write_packet(...)`",
      "header encode/decode",
      "semantic validation",
      "write backpressure",
      "session ordering",
      "close cleanup",
      "raw byte stream dispatch는 core public 표면에 넣지 않는다",
      "Header validation 실패 packet은 application handler로 넘기지 않는다",
      "같은 stream session lifecycle callback과 packet callback은 직렬",
      "pending write 중 disconnect"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 13 STREAM framework lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-stream",
      "ctest --test-dir framework/languages/cpp/build -L framework-regression -R stream"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 13 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal14_covers_actor_gateway (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 14. ActorGateway Session Relay");
    const auto end = text.find ("### Goal 15. Registry And Topology");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 14 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "STREAM session과 actor를 ActorGateway로 bind/relay",
      "`stream.attach_actor_gateway(spot_node_name)`",
      "`actor_ref_t`",
      "`session_actor_manager_t`",
      "`session_actor_t`",
      "`actor_context_t`",
      "`bound_session_t`",
      "`actor_join_result_t`",
      "actor factory 등록",
      "local actor handle bind",
      "remote actor ref bind",
      "session actor relay",
      "bound session push",
      "actor disconnect cleanup",
      "actor type mismatch error",
      "duplicate actor error",
      "remote ActorGateway locator codec 숨김",
      "application route mesh channel을 만들지 않는다",
      "node rid, actor id, generation round-trip",
      "actor push는 `bound_session_t`를 통해 내려간다",
      "Registry나 sample-only metadata store에 저장하지 않는다",
      "`relay(...)`와 `send(...)`는 caller payload를 소비하지 않는다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 14 ActorGateway lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-actor-gateway",
      "ctest --test-dir framework/languages/cpp/build -L framework-regression -R actor"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 14 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal15_covers_registry_topology (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 15. Registry And Topology");
    const auto end = text.find ("### Goal 16. Monitoring, Health, Observability");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 15 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "embedded registry, remote query, topology 조회, Spot remote address lookup",
      "embedded registry bootstrap",
      "registry query client",
      "topology query",
      "service summary",
      "Spot remote address lookup 기본값",
      "custom Spot resolver",
      "duplicate resolver rejection",
      "ambiguous route channel validation",
      "stale address cleanup",
      "monitoring snapshot source",
      "Registry는 Spot remote address 조회 기본값",
      "session actor relay hot path의 actor route store로 Registry를 쓰지 않는다",
      "Spot discovery 없이 Registry Spot 기본값을 켜면 validation 오류",
      "route channel이 둘 이상이면 resolver channel 이름을 명시해야 한다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 15 registry/topology lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-registry",
      "ctest --test-dir framework/languages/cpp/build -L framework-regression -R registry"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 15 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal16_covers_observability (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 16. Monitoring, Health, Observability");
    const auto end = text.find ("### Goal 17. Module System And Hosted Services");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 16 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "monitoring builder",
      "runtime event enum",
      "typed event payload structs",
      "socket/discovery/registry/spot/stream/actor/session event projection",
      "timer immediate failure event",
      "correlation id",
      "health status",
      "readiness/liveness",
      "metrics/tracing hook",
      "logging integration",
      "handler exception과 transport error가 구분된다",
      "timer handler failure는 snapshot diff interval을 기다리지 않는다",
      "zlink channel, registry, STREAM endpoint, hosted service 상태",
      "public callback payload에 exception 객체 자체를 직접 싣지 않는다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 16 observability lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-observability",
      "ctest --test-dir framework/languages/cpp/build -L framework-unit -R monitoring"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 16 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal17_covers_module_hosting (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 17. Module System And Hosted Services");
    const auto end = text.find ("### Goal 18. ZLink HTTP Client");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 17 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "`module_t`",
      "`framework_module_contract_t`",
      "`hosted_service_t`",
      "`app_t::add_zlink_framework(...)`",
      "`zlink_framework_options_t`",
      "`options.services()`",
      "`options.handlers()`",
      "`options.codecs().add_json()`",
      "discovery/channel/spot/stream fluent options builders",
      "module service registration",
      "module handler registration",
      "stage wrapper module pattern",
      "hosted service start/stop lifecycle",
      "`app_t::add_zlink_framework(options_callback)`",
      "JSON codec 사용만 선언하고 message type을 모두 나열하지",
      "`dependency_list_t<Dep...>`와 DI 생성자 주입",
      "handler용 DI factory, serializer smoke 검증",
      "낮은 수준 zlink builder 람다를 두지 않는다",
      "hosted service는 app startup/shutdown과 함께 시작하고 종료한다"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 17 module/hosted lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-unit -R module",
      "ctest --test-dir framework/languages/cpp/build -L framework-integration -R hosted"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 17 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal18_covers_http_client (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 18. ZLink HTTP Client");
    const auto end = text.find ("### Goal 19. HTTP Hosting");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 18 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "`zlink::http_client` namespace",
      "`zlink/http_client.hpp`",
      "`http-client/include/zlink/http_client/contracts/*`",
      "`http-client/src/runtime/*`",
      "`zlink::http_client` CMake target",
      "`client_t::create()`",
      "`base_url(endpoint)`",
      "`timeout(duration)`",
      "`default_header(name, value)`",
      "`json()`",
      "`get(path)`, `post(path)`, `put(path)`, `delete_(path)`",
      "typed request body: `body(dto)`",
      "raw coroutine submit",
      "typed coroutine submit",
      "typed JSON response: `submit<TReply>()`",
      "HTTP status와 transport error를 client result/error kind로 매핑",
      "HTTP client regression test suite",
      "`Boost.Beast` runtime private 구현",
      "HTTP와 HTTPS endpoint",
      "TLS verification option",
      "certificate authority bundle 또는 test certificate trust option",
      "framework sample 전용 helper나 framework target이 아니다",
      "`Boost.Beast`, `Boost.Asio`, OpenSSL, socket, resolver",
      "`base_url(...)`은 `http://`와 `https://` endpoint를 모두 받는다",
      "TLS handshake, server certificate verification, hostname verification",
      "묵시적으로 TLS verification을 끄지 않는다",
      "`co_await submit<T>()`",
      "`message_t` 또는 DTO serializer hook",
      "`nlohmann::json::parse`로 field를 직접 꺼내지 않는다",
      "retry, redirect, cookie, proxy, multipart, streaming download",
      "TicTacToe client sample은 이 HTTP client로 `POST /games`를 호출한다",
      "TLS verification 실패, test certificate trust 성공"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 18 HTTP client lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L http-client-contract",
      "ctest --test-dir framework/languages/cpp/build -L http-client-unit",
      "ctest --test-dir framework/languages/cpp/build -L http-client-e2e",
      "ctest --test-dir framework/languages/cpp/build -L http-client-https",
      "ctest --test-dir framework/languages/cpp/build -L http-client-regression"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 18 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool implementation_plan_goal19_covers_http_hosting (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 19. HTTP Hosting");
    const auto end = text.find ("### Goal 20. Stream Connector");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 19 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string required[] = {
      "`contracts/http/*`",
      "`http_options_builder_t`",
      "`options.http().listen(endpoint)`",
      "`options.http().configure_tls(...)`",
      "`options.http().configure_server(...)`",
      "`map_get<THandler>(path)`",
      "`map_post<THandler>(path)`",
      "`map_put<THandler>(path)`",
      "`map_delete<THandler>(path)`",
      "`http_request_t`",
      "`http_response_t`",
      "`use<TMiddleware>()`",
      "typed DTO HTTP handler shape",
      "raw `http_request_t` to `http_response_t` handler shape",
      "HTTP handler shape resolution algorithm",
      "response precedence rules",
      "JSON body binding",
      "raw HTTP body/header binding",
      "route parameter binding",
      "query string binding",
      "correlation id propagation",
      "HTTP handler e2e test through `zlink::http_client`",
      "HTTP hosted service",
      "`Boost.Beast` runtime private 구현",
      "HTTPS endpoint",
      "TLS certificate/private key option",
      "keep-alive request loop",
      "request header/body timeout",
      "request body/header size limit",
      "max connections와 overload response",
      "graceful shutdown drain",
      "Drogon/Oat++급 C++ backend API framework 성능 gate",
      "public header에 나타나지 않는다",
      "MVC controller, template rendering, Razor page, WebSocket transport는 포함하지 않는다",
      "`zlink::http_client`로 `GET`, `POST`, `PUT`, `DELETE` route를 호출한다",
      "raw request sync",
      "raw content type",
      "content length",
      "metrics/logging"};
    for (const auto &needle : required) {
        if (goal.find (needle) == std::string::npos) {
            std::cerr << "Goal 19 HTTP hosting lacks required contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-http",
      "ctest --test-dir framework/languages/cpp/build -L framework-http-e2e",
      "ctest --test-dir framework/languages/cpp/build -L framework-integration -R http"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 19 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    return ok;
}

bool registry_draft_matches_monitoring_contract (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/spec/cpp-registry.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    bool ok = true;
    const std::string required[] = {"Registry snapshot event는 등록된 monitoring source에만 전달",
                                    "topology나 service summary", "typed monitoring event"};
    for (const auto &needle : required) {
        if (text.find (needle) == std::string::npos) {
            std::cerr << "registry draft no longer documents implemented "
                      << "monitoring contract: " << needle << '\n';
            ok = false;
        }
    }

    const std::string stale[] = {"Registry snapshot diff event는 설정된 interval을 따르고",
                                 "monitoring 통합 단계에서 별도 regression으로 고정한다"};
    for (const auto &needle : stale) {
        if (text.find (needle) != std::string::npos) {
            std::cerr << "registry draft still treats implemented monitoring "
                      << "contract as pending: " << needle << '\n';
            ok = false;
        }
    }
    return ok;
}

bool implementation_plan_goal21_covers_sample_labels (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 21. Review Samples");
    const auto end = text.find ("### Goal 22. Final Regression, Package, Extension Boundary");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 21 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-parity",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-api",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-bingo",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-play",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-registry",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-session",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-tictactoe"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 21 verification commands lack: " << command << '\n';
            ok = false;
        }
    }
    return ok;
}

bool implementation_plan_goal22_covers_final_label_axes (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-implementation-plan.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const auto start = text.find ("### Goal 22. Final Regression, Package, Extension Boundary");
    const auto end = text.find ("## 6. 참고 문서 추적표");
    if (start == std::string::npos || end == std::string::npos || start >= end) {
        std::cerr << "implementation plan lacks bounded Goal 22 section: " << path << '\n';
        return false;
    }
    const auto goal = text.substr (start, end - start);

    bool ok = true;
    const std::string commands[] = {
      "ctest --test-dir framework/languages/cpp/build --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-channel "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-spot --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-stream "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-actor-gateway "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-zlink-registry "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-http --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-http-e2e --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-config --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-observability "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L http-client-contract --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L http-client-unit --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L http-client-regression "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L http-client-e2e --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L http-client-https --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L parity --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-smoke "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-parity "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-api --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-bingo "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-registry "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-play --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-session "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-sample-tictactoe "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-unit --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-integration --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-contract --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-protocol --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-transport --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-typed --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-e2e --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-package --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-unreal-contract "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-unreal-compile "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L connector-unreal-smoke "
      "--output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-package --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-tooling --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build -L framework-extension --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build-coverage --output-on-failure",
      "ctest --test-dir framework/languages/cpp/build-coverage -L framework-coverage "
      "--output-on-failure"};
    for (const auto &command : commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 22 verification commands lack: " << command << '\n';
            ok = false;
        }
    }

    const std::string non_ctest_commands[] = {
      "cmake --build framework/languages/cpp/build",
      "cmake -S framework/languages/cpp",
      "-B framework/languages/cpp/build-coverage",
      "-DZLINK_FRAMEWORK_CPP_ENABLE_COVERAGE=ON",
      "-DZLINK_FRAMEWORK_CPP_COVERAGE_THRESHOLD=80",
      "cmake --build framework/languages/cpp/build-coverage",
      "git diff --check -- framework/languages/cpp bindings/cpp"};
    for (const auto &command : non_ctest_commands) {
        if (goal.find (command) == std::string::npos) {
            std::cerr << "Goal 22 verification commands lack non-CTest gate: " << command << '\n';
            ok = false;
        }
    }
    return ok;
}

bool posd_log_has_current_goal_mapping (const std::filesystem::path &root)
{
    const auto path = root / "../../doc/framework/cpp/internals/cpp-framework-posd-refactoring-log.ko.md";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    bool ok = true;
    const auto refactor_count = count_occurrences (text, "### 적용한 리팩토링");
    if (refactor_count < 22) {
        std::cerr << "POSD refactoring log has only " << refactor_count
                  << " refactoring sections; expected at least 22: " << path << '\n';
        ok = false;
    }

    const std::string rows[] = {"| Goal 1. Repository Skeleton And Tooling |",
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
                                "| Goal 22. Final Regression, Package, Extension Boundary |"};
    for (const auto &row : rows) {
        const auto offset = text.find (row);
        if (offset == std::string::npos) {
            std::cerr << "POSD refactoring log lacks current goal mapping row: " << row << '\n';
            ok = false;
            continue;
        }
        const auto line_end = text.find ('\n', offset);
        const auto line = text.substr (offset, line_end == std::string::npos ? std::string::npos
                                                                             : line_end - offset);
        if (line.find ("잔여 POSD 위험 신호와 리팩토링 이슈 0개") == std::string::npos) {
            std::cerr << "POSD refactoring log mapping row lacks zero-issue "
                         "recheck status: "
                      << row << '\n';
            ok = false;
        }
    }

    const std::string stale_headings[] = {
      "\n## Goal 17. C++ Stream Connector", "\n## Goal 18. Unreal Stream Connector",
      "\n## Goal 20. Final Parity And Regression Gate", "\n## Goal 21. Extension Boundaries"};
    for (const auto &heading : stale_headings) {
        if (text.find (heading) != std::string::npos) {
            std::cerr << "POSD refactoring log uses stale plan goal heading: " << heading << '\n';
            ok = false;
        }
    }
    return ok;
}

bool cmake_extension_boundaries_hold (const std::filesystem::path &root)
{
    const auto path = root / "CMakeLists.txt";
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    bool ok = true;
    const auto extension_count = count_occurrences (text, "add_zlink_framework_extension(");
    if (extension_count != 11) {
        std::cerr << "expected 11 framework extension targets, got " << extension_count << ": "
                  << path << '\n';
        ok = false;
    }
    if (text.find ("target_link_libraries(${target_name} INTERFACE zlink::framework)")
        == std::string::npos) {
        std::cerr << "framework extension helper must depend on core only: " << path << '\n';
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
      "find_package(FlatBuffers"};
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

bool contract_headers_have_compile_coverage (const std::filesystem::path &root,
                                             const std::filesystem::path &include_dir,
                                             const std::string &include_prefix)
{
    const auto coverage_file =
      root / "tests/Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp";
    std::ifstream coverage_input (coverage_file);
    std::ostringstream coverage_buffer;
    coverage_buffer << coverage_input.rdbuf ();
    const auto coverage_text = coverage_buffer.str ();

    bool ok = true;
    for (const auto &entry : std::filesystem::recursive_directory_iterator (root / include_dir)) {
        if (!entry.is_regular_file () || entry.path ().extension () != ".hpp") {
            continue;
        }
        const auto relative = std::filesystem::relative (entry.path (), root / include_dir);
        const auto include =
          std::string ("#include <") + include_prefix + relative.generic_string () + ">";
        if (coverage_text.find (include) == std::string::npos) {
            std::cerr << "public contract header lacks direct compile coverage: " << entry.path ()
                      << '\n';
            ok = false;
        }
    }
    return ok;
}

bool sample_application_code_uses_message_codec (const std::filesystem::path &root)
{
    bool ok = true;
    const auto samples_root = root / "samples";
    for (const auto &entry : std::filesystem::recursive_directory_iterator (samples_root)) {
        if (!entry.is_regular_file ()) {
            continue;
        }
        const auto ext = entry.path ().extension ();
        if (ext != ".hpp" && ext != ".cpp") {
            continue;
        }

        const auto relative =
          std::filesystem::relative (entry.path (), samples_root).generic_string ();
        const bool dto_contract_file = relative.find ("/Shared/Contracts/") != std::string::npos;

        std::ifstream input (entry.path ());
        std::string line;
        std::size_t line_no = 0;
        while (std::getline (input, line)) {
            ++line_no;
            if (!dto_contract_file && line.find ("nlohmann::json::parse") != std::string::npos) {
                std::cerr << "sample application code must use message_t/serializer "
                             "instead of direct JSON parse: "
                          << entry.path () << ':' << line_no << '\n';
                ok = false;
            }
            if (!dto_contract_file && line.find ("json.at") != std::string::npos) {
                std::cerr << "sample application code must not extract JSON fields "
                             "outside DTO serializer hooks: "
                          << entry.path () << ':' << line_no << '\n';
                ok = false;
            }
        }
    }
    return ok;
}

bool sample_server_code_does_not_block_on_task_result (const std::filesystem::path &root)
{
    bool ok = true;
    const auto samples_root = root / "samples";
    for (const auto &entry : std::filesystem::recursive_directory_iterator (samples_root)) {
        if (!entry.is_regular_file ()) {
            continue;
        }
        const auto ext = entry.path ().extension ();
        if (ext != ".hpp" && ext != ".cpp") {
            continue;
        }

        const auto relative =
          std::filesystem::relative (entry.path (), samples_root).generic_string ();
        if (relative.find ("/Server/") == std::string::npos
            && relative.find ("/Shared/") == std::string::npos) {
            continue;
        }

        std::ifstream input (entry.path ());
        std::string line;
        std::size_t line_no = 0;
        while (std::getline (input, line)) {
            ++line_no;
            if (line.find (".result (") != std::string::npos
                || line.find (".result(") != std::string::npos) {
                std::cerr << "sample server/shared code must use task_t await or "
                             "callback completion instead of blocking result(): "
                          << entry.path () << ':' << line_no << '\n';
                ok = false;
            }
        }
    }
    return ok;
}

bool client_sample_uses_e2e_connector (const std::filesystem::path &root,
                                       const std::filesystem::path &client_file)
{
    const auto path = root / client_file;
    bool ok = true;
    ok &= file_contains (path, "zlink/stream_connector.hpp");
    ok &= file_contains (path, "zlink/stream_e2e_client");
    ok &= file_contains (path, "stream_e2e_client::use");
    ok &= file_contains (path, "connector_factory_t::create");
    ok &= file_contains (path, "dispatch_mode_t::immediate");
    if (!ok) {
        std::cerr << "client sample does not wrap the stream connector with e2e client: "
                  << path << '\n';
    }
    return ok;
}

bool client_sample_does_not_include_server_implementation (const std::filesystem::path &root,
                                                           const std::filesystem::path &client_root)
{
    bool ok = true;
    const std::string forbidden[] = {"../Server/",
                                     "Server/",
                                     "../Shared/E2E/",
                                     "Shared/E2E/",
                                     "zlink/framework",
                                     "framework::",
                                     "app_t",
                                     "config_builder_t",
                                     "configuration_section_t",
                                     "zlink/Contracts/Sockets",
                                     "zlink/Contracts/Service",
                                     "zlink::context_t",
                                     "zlink::stream_socket_t",
                                     "run_client_e2e_stream_server",
                                     "use_embedded_server"};
    const auto path = root / client_root;
    for (const auto &entry : std::filesystem::recursive_directory_iterator (path)) {
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
            for (const auto &needle : forbidden) {
                if (line.find (needle) != std::string::npos) {
                    std::cerr << "client sample references server/test harness or "
                                 "low-level zlink implementation: "
                              << entry.path () << ':' << line_no << " contains " << needle << '\n';
                    ok = false;
                }
            }
        }
    }
    return ok;
}

bool sample_client_targets_do_not_link_framework (const std::filesystem::path &root)
{
    const auto path = root / "CMakeLists.txt";
    std::ifstream input (path);
    std::string text ((std::istreambuf_iterator<char> (input)),
                      std::istreambuf_iterator<char> ());

    bool ok = true;
    const std::string targets[] = {"sample_cpp_framework_bingo_client",
                                   "sample_cpp_framework_tictactoe_client"};
    for (const auto &target : targets) {
        std::istringstream lines (text);
        std::string line;
        std::string block;
        bool found = false;
        while (std::getline (lines, line)) {
            if (!found && line.find ("target_link_libraries(" + target) == std::string::npos) {
                continue;
            }
            found = true;
            block += line;
            block += '\n';
            if (line.find (')') != std::string::npos) { break; }
        }
        if (!found) {
            std::cerr << "missing client target link declaration: " << target << '\n';
            ok = false;
            continue;
        }
        if (block.find ("zlink::framework") != std::string::npos) {
            std::cerr << "client target must not link zlink::framework: " << target << '\n';
            ok = false;
        }
    }
    return ok;
}

bool file_does_not_contain (const std::filesystem::path &path,
                            const std::string &needle,
                            const std::string &message)
{
    if (!file_contains_quiet (path, needle)) {
        return true;
    }
    std::cerr << message << ": " << path << '\n';
    return false;
}

bool http_client_public_surface_declares_general_client_features (
  const std::filesystem::path &root)
{
    bool ok = true;
    const auto contract_header = root / "http-client/include/zlink/http_client/contracts/client.hpp";

    std::ifstream input (contract_header);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    const auto text = buffer.str ();

    const std::string required[] = {
      "follow_redirects", "retry (",         "cookies ()", "proxy (",  "compression ()",
      "query (",          "form (",          "multipart (", "multipart_file (",
      "patch (",          "head (",          "options (",   "download ("};
    for (const auto &needle : required) {
        if (text.find (needle) == std::string::npos) {
            std::cerr << "HTTP client public surface lacks general client feature: "
                      << contract_header << " misses " << needle << '\n';
            ok = false;
        }
    }
    return ok;
}

bool stream_connector_public_surface_hides_runtime_internals (const std::filesystem::path &root)
{
    bool ok = true;
    const auto include_root = root / "connector/core/include";
    const std::string forbidden[] = {
      "connector_state_t",      "connector_runtime_t",    "pending_request_t", "pending_requests",
      "transport_connection_t", "stream_connection_t",    "frame_codec_t",     "header_codec_t",
      "metadata_codec_t",       "lz4_compression_codec_t"};

    for (const auto &entry : std::filesystem::recursive_directory_iterator (include_root)) {
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
                    std::cerr << "Stream Connector public surface exposes runtime "
                                 "implementation type: "
                              << entry.path () << ':' << line_no << " contains " << needle << '\n';
                    ok = false;
                }
            }
        }
    }
    return ok;
}

bool http_hosting_public_surface_excludes_non_goal_features (const std::filesystem::path &root)
{
    bool ok = true;
    const std::filesystem::path include_roots[] = {
      root / "framework/include/zlink/framework/contracts/http"};
    const std::string forbidden[] = {
      "mvc",        "MVC",         "controller",        "Controller",
      "razor",      "Razor",       "websocket",         "WebSocket",
      "web_socket", "view_engine", "template_renderer", "render_template"};

    for (const auto &include_root : include_roots) {
        if (std::filesystem::is_regular_file (include_root)) {
            std::ifstream input (include_root);
            std::string line;
            std::size_t line_no = 0;
            while (std::getline (input, line)) {
                ++line_no;
                for (const auto &needle : forbidden) {
                    if (line.find (needle) != std::string::npos) {
                        std::cerr << "HTTP hosting public surface exposes non-goal "
                                     "feature: "
                                  << include_root << ':' << line_no << " contains " << needle
                                  << '\n';
                        ok = false;
                    }
                }
            }
            continue;
        }

        for (const auto &entry : std::filesystem::recursive_directory_iterator (include_root)) {
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
                        std::cerr << "HTTP hosting public surface exposes non-goal "
                                     "feature: "
                                  << entry.path () << ':' << line_no << " contains " << needle
                                  << '\n';
                        ok = false;
                    }
                }
            }
        }
    }
    return ok;
}

} // namespace

int main ()
{
    const std::filesystem::path root{ZLINK_FRAMEWORK_CPP_SOURCE_DIR};

    bool ok = true;
    ok &= require_exists (root / "framework/include/zlink/framework/contracts");
    ok &= require_exists (root / "framework/include/zlink/framework/contracts/assembly");
    ok &=
      require_exists (root / "framework/include/zlink/framework/contracts/detail/message_name.hpp");
    const std::string removed_framework_facades[] = {
      "actors.hpp", "app.hpp",           "assembly.hpp",  "call.hpp",       "channels.hpp",
      "config.hpp", "error.hpp",         "execution.hpp", "handlers.hpp",   "health.hpp",
      "http.hpp",   "logging.hpp",       "module.hpp",    "monitoring.hpp", "registry.hpp",
      "result.hpp", "serialization.hpp", "services.hpp",  "spots.hpp",      "streams.hpp",
      "task.hpp",   "timers.hpp",        "transport.hpp"};
    for (const auto &header : removed_framework_facades) {
        ok &= require_absent (root / "framework/include/zlink/framework" / header,
                              "one-line facade wrappers are dead compatibility surface; use "
                              "zlink/framework.hpp or contracts/*");
    }
    ok &= require_exists (root / "framework/src/runtime");
    ok &= require_exists (root / "framework/src/runtime/backend/contracts");
    ok &= require_exists (root / "framework/src/runtime/backend/native_route_backend.cpp");
    ok &= require_exists (root / "framework/src/runtime/backend/native_route_backend.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_packet_dispatcher.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_packet_dispatcher.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_pending_requests.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_pending_requests.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_reply_writer.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_reply_writer.hpp");
    ok &= require_exists (root / "framework/src/runtime/execution");
    ok &= require_exists (root / "framework/src/runtime/execution/serial_execution_queue.cpp");
    ok &= require_exists (root / "framework/src/runtime/execution/serial_execution_queue.hpp");
    ok &= require_exists (root / "framework/src/runtime/messaging");
    ok &= require_exists (root / "framework/src/runtime/messaging/client_call_codec.cpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/client_call_codec.hpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/envelope_codec.cpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/envelope_codec.hpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/pending_operation.cpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/pending_operation_state.hpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/pending_submit.cpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/pending_submit.hpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/request_failure_mapper.cpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/request_failure_mapper.hpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/submit_queue.cpp");
    ok &= require_exists (root / "framework/src/runtime/messaging/submit_queue.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_runtime_bundle.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_runtime_bundle.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_bundle_factory.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_bundle_factory.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_runtime_manager.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_runtime_manager.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_message_pump.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_message_pump.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_receive_loop.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/channel_receive_loop.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_connection_set.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_connection_set.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_channel_registration.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_channel_registration.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_channel_runtime.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_channel_runtime.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_handler_registry.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_handler_registry.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_handler_invoker.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_handler_invoker.hpp");
    ok &=
      require_exists (root / "framework/src/runtime/channels/route_internal_packet_dispatcher.cpp");
    ok &=
      require_exists (root / "framework/src/runtime/channels/route_internal_packet_dispatcher.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_packet.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_packet_dispatcher.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_packet_dispatcher.hpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_receive_pump.cpp");
    ok &= require_exists (root / "framework/src/runtime/channels/route_receive_pump.hpp");
    ok &= require_exists (root / "framework/src/runtime/configuration/builders");
    ok &= require_exists (
      root / "framework/src/runtime/configuration/builders/configuration_builder.cpp");
    ok &= require_exists (root / "connector/core/include/zlink/stream_connector/contracts");
    ok &= require_exists (root / "connector/core/include/zlink/stream_connector/contracts/calls");
    ok &= require_exists (
      root / "connector/core/include/zlink/stream_connector/contracts/calls/zlink_stream_calls.hpp");
    ok &= require_exists (
      root
      / "connector/core/include/zlink/stream_connector/contracts/zlink_stream_connector_options.hpp");
    ok &= require_exists (
      root / "connector/core/include/zlink/stream_connector/contracts/zlink_stream_models.hpp");
    ok &= require_exists (root / "connector/core/src/runtime");
    ok &= require_exists (root / "connector/core/src/runtime/calls");
    ok &= require_exists (root / "connector/core/src/runtime/protocol");
    ok &= require_exists (root / "connector/core/src/runtime/protocol/compression");
    ok &= require_exists (root / "connector/core/src/runtime/protocol/framing");
    ok &= require_exists (root / "connector/core/src/runtime/transport");
    ok &= require_exists (root / "connector/core/src/runtime/connector_lifecycle.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/heartbeat_monitor.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/calls/zlink_stream_calls.cpp");
    ok &= require_exists (root
                          / "connector/core/src/runtime/protocol/compression/lz4_compression_codec.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/protocol/framing/frame_codec.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/protocol/framing.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/protocol/header_codec.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/protocol/metadata_codec.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/protocol/packet_name_resolver.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/transport/stream_connection.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/transport/stream_transport_factory.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/transport/websocket_connection.cpp");
    ok &= require_exists (root / "connector/core/src/runtime/backend/contracts");
    ok &= require_exists (root / "http-client/include/zlink/http_client.hpp");
    ok &= require_exists (root / "http-client/include/zlink/http_client/contracts/client.hpp");
    ok &= require_exists (root / "http-client/src/runtime");
    ok &= require_exists (root / "http-client/src/runtime/http_client_runtime.hpp");
    ok &= require_exists (root / "http-client/src/runtime/http_client_runtime.cpp");
    ok &= require_exists (root / "tests/Zlink.Framework.UnitTests");
    ok &= require_exists (root / "tests/Zlink.Framework.ContractTests");
    ok &= require_absent (root / "tests/Zlink.Framework.E2ETests",
                          "sample e2e must not rely on separate fake process runners");
    ok &= require_exists (root / "tests/Systems.Zlink.Stream.Connector.Tests");
    ok &= require_exists (root / "tests/Zlink.Unreal.Stream.Connector.Tests");
    ok &= require_exists (
      root / "tests/Zlink.Framework.ContractTests/test_cpp_framework_contract_headers.cpp");
    ok &= require_exists (
      root / "tests/Zlink.Framework.UnitTests/test_cpp_framework_handler_registry.cpp");
    ok &= require_exists (
      root / "tests/Systems.Zlink.Stream.Connector.Tests/test_cpp_stream_connector.cpp");
    ok &= require_exists (
      root / "tests/Zlink.Unreal.Stream.Connector.Tests/test_unreal_stream_connector.cpp");
    const auto old_unreal_connector_dir = std::string ("unreal") + "-connector";
    ok &= require_absent (root / old_unreal_connector_dir,
                          "Unreal connector belongs under connector/engines/unreal");
    ok &= require_exists (root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public");
    ok &= require_exists (root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private");
    ok &= require_exists (root
                          / "connector/engines/unreal/Source/ZLinkStreamConnectorTests/Private/"
                            "ZLinkStreamConnectorAutomationTests.cpp");
    ok &= require_exists (
      root
      / "connector/engines/unreal/Source/ZLinkStreamConnectorTests/"
        "ZLinkStreamConnectorTests.Build.cs");
    ok &= require_exists (root / "samples/Bingo/Server/Configuration/sample_names.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Configuration/sample_topology.hpp");
    ok &= require_exists (root / "samples/Bingo/Client/Configuration/sample_topology.hpp");
    ok &= require_exists (root / "samples/Bingo/run_sample.sh");
    ok &= require_exists (root / "samples/Bingo/run_sample.ps1");
    ok &= require_exists (root / "samples/Bingo/Shared/Contracts/messages.hpp");
    ok &= require_exists (root / "samples/Bingo/Shared/Contracts/bingo_messages.proto");
    ok &= require_absent (root / "samples/Bingo/Shared" / "Configuration",
                          "Bingo Shared must contain message contracts only");
    ok &= require_absent (root / "samples/Bingo/Shared" / "sample.hpp",
                          "Bingo sample umbrella belongs to server code, not Shared");
    ok &= require_absent (root / "samples/Bingo/Shared" / "host_support.hpp",
                          "Bingo host support belongs to server code, not Shared");
    ok &= require_absent (root / "samples/Bingo/Server" / "sample.hpp",
                          "Bingo server code should include role-local headers directly");
    ok &= require_absent (root / "samples/Bingo/Server/E2E",
                          "Bingo sample e2e must run the public sample server roles");
    ok &= require_absent (root / "samples/Bingo/Shared/E2E",
                          "Bingo sample tree must not contain test-only stream harnesses");
    ok &=
      require_exists (root / "samples/Bingo/Server/Api/Handlers/authenticate_player_handler.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Api/api_server_host_factory.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Api/api_server_framework.hpp");
    ok &= file_contains (root / "samples/Bingo/Server/Api/api_server_host_factory.hpp",
                         "app.logging ().use_console ().set_level (\"info\")");
    ok &= file_does_not_contain (
      root / "samples/Bingo/Server/Api/api_server_framework.hpp", "app.logging ().use_console ()",
      "reusable framework setup must not force a host-level logging sink");
    ok &= require_exists (root / "samples/Bingo/Server/Api/Handlers/match_bingo_handler.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Play/Domain/Bingo/bingo_card.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Play/Domain/Bingo/bingo_game.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Play/Domain/Bingo/bingo_room_game.hpp");
    ok &= require_exists (
      root / "samples/Bingo/Server/Play/Application/RoomAllocation/bingo_room_allocator.hpp");
    ok &=
      require_exists (root / "samples/Bingo/Server/Play/Adapters/ZLink/Actors/player_actor.hpp");
    ok &= require_exists (
      root / "samples/Bingo/Server/Play/Adapters/ZLink/Actors/player_actor_factory.hpp");
    ok &= require_exists (root
                          / "samples/Bingo/Server/Play/Adapters/ZLink/Notifications/"
                            "bingo_notification_publisher.hpp");
    ok &=
      require_exists (root / "samples/Bingo/Server/Play/Adapters/ZLink/Spots/bingo_room_spot.hpp");
    ok &= require_exists (root
                          / "samples/Bingo/Server/Play/Adapters/ZLink/Spots/Handlers/"
                            "bingo_room_timer_handler.hpp");
    ok &=
      require_exists (root / "samples/Bingo/Server/Play/Adapters/ZLink/Spots/bingo_entry_spot.hpp");
    ok &= require_absent (root
                            / "samples/Bingo/Server/Play/Adapters/ZLink/Spots/Handlers/"
                              "match_bingo_actor_handler.hpp",
                          "SPOT actor packets must be registered as spot member functions");
    ok &= require_exists (
      root / "samples/Bingo/Server/Play/Adapters/ZLink/Handlers/allocate_bingo_room_handler.hpp");
    ok &= require_exists (
      root / "samples/Bingo/Server/Play/Adapters/ZLink/Handlers/ensure_player_actor_handler.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Play/play_server_host_factory.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Registry/registry_host_factory.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Session/main.cpp");
    ok &= require_exists (root / "samples/Bingo/Server/Session/session_server_host_factory.hpp");
    ok &= require_exists (root / "samples/Bingo/Server/Session/Sessions/bingo_session.hpp");
    ok &= require_exists (
      root / "samples/Bingo/Server/Session/Sessions/Handlers/authenticate_session_handler.hpp");
    ok &= require_exists (root / "samples/Bingo/Client/bingo_client_options.hpp");
    ok &= require_exists (root / "samples/Bingo/Client/bingo_client_scenario.hpp");
    ok &= require_absent (root / "samples/Bingo/Client/bingo_notification_inbox.hpp",
                          "Bingo client scenario waits on connector messages directly");
    ok &= require_absent (root / "samples/Bingo/Client/bingo_player_client.hpp",
                          "Bingo client scenario must not hide connector calls in a wrapper");
    ok &= require_absent (root / "samples/Bingo/Client/bingo_client_app.hpp",
                          "Bingo client scenario is kept in bingo_client_scenario.hpp");
    ok &= require_exists (root / "samples/TicTacToe/Server/Configuration/sample_names.hpp");
    ok &= require_exists (root / "samples/TicTacToe/Server/Configuration/sample_topology.hpp");
    ok &= require_exists (root / "samples/TicTacToe/Client/Configuration/sample_names.hpp");
    ok &= require_exists (root / "samples/TicTacToe/Client/Configuration/sample_topology.hpp");
    ok &= require_exists (root / "samples/TicTacToe/run_sample.sh");
    ok &= require_exists (root / "samples/TicTacToe/run_sample.ps1");
    ok &= require_exists (root / "samples/run_samples.sh");
    ok &= require_exists (root / "samples/run_samples.ps1");
    ok &= require_exists (root / "samples/TicTacToe/Shared/Contracts/messages.hpp");
    ok &= require_absent (root / "samples/TicTacToe/Shared" / "Configuration",
                          "TicTacToe Shared must contain message contracts only");
    ok &= require_absent (root / "samples/TicTacToe/Shared" / "sample.hpp",
                          "TicTacToe sample umbrella belongs to server code, not Shared");
    ok &= require_absent (root / "samples/TicTacToe/Shared" / "host_support.hpp",
                          "TicTacToe host support belongs to server code, not Shared");
    ok &= require_absent (root / "samples/TicTacToe/Server" / "sample.hpp",
                          "TicTacToe server code should include role-local headers directly");
    ok &= require_absent (root / "samples/TicTacToe/Server" / "sample_log.hpp",
                          "TicTacToe server logging should not need a separate helper header");
    ok &= require_absent (root / "samples/TicTacToe/Server/E2E",
                          "TicTacToe sample e2e must run the public sample server roles");
    ok &= require_absent (root / "samples/TicTacToe/Shared/E2E",
                          "TicTacToe sample tree must not contain test-only stream harnesses");
    ok &= require_absent (root / "samples/Shared/stream_frame_server.hpp",
                          "test-only stream frame server code must not live under samples");
    ok &= require_exists (
      root / "samples/TicTacToe/Server/Api/Handlers/authenticate_player_handler.hpp");
    ok &= require_exists (root / "samples/TicTacToe/Server/Api/api_server_host_factory.hpp");
    ok &=
      require_exists (root / "samples/TicTacToe/Server/Api/Handlers/create_game_http_handler.hpp");
    ok &= require_absent (root / "samples/TicTacToe/Server/Api/api_server_framework.hpp",
                          "TicTacToe API framework setup belongs in api_server_host_factory.hpp");
    ok &= require_absent (
      root / "samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/Handlers/join_game_handler.hpp",
      "SPOT actor packets must be registered as spot member functions");
    ok &=
      require_exists (root / "samples/TicTacToe/Server/Play/Domain/TicTacToe/tictactoe_match.hpp");
    ok &= require_exists (
      root / "samples/TicTacToe/Server/Play/Application/GameCreation/tictactoe_game_creator.hpp");
    ok &= require_exists (root
                          / "samples/TicTacToe/Server/Play/Adapters/ZLink/Actors/player_actor.hpp");
    ok &= require_exists (root
                          / "samples/TicTacToe/Server/Play/Adapters/ZLink/Notifications/"
                            "game_notification_publisher.hpp");
    ok &= require_exists (
      root
      / "samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/tictactoe_game_contract_mapper.hpp");
    ok &= require_exists (
      root / "samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/tictactoe_game_models.hpp");
    ok &= require_exists (
      root / "samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/tictactoe_game_spot.hpp");
    ok &= require_absent (root
                            / "samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/Handlers/"
                              "tictactoe_game_join_handler.hpp",
                          "SPOT actor joins must be registered as spot member functions");
    ok &= require_absent (
      root / "samples/TicTacToe/Server/Play/Adapters/ZLink/Spots/Handlers/place_mark_handler.hpp",
      "SPOT actor packets must be registered as spot member functions");
    ok &= require_exists (
      root
      / "samples/TicTacToe/Server/Play/Adapters/ZLink/Handlers/ensure_player_actor_handler.hpp");
    ok &= require_exists (
      root / "samples/TicTacToe/Server/Play/Adapters/ZLink/Sessions/play_session.hpp");
    ok &= require_exists (root
                          / "samples/TicTacToe/Server/Play/Adapters/ZLink/Sessions/Handlers/"
                            "authenticate_play_session_handler.hpp");
    ok &= require_exists (root / "samples/TicTacToe/Server/Play/play_server_host_factory.hpp");
    ok &= require_absent (root / "samples/TicTacToe/Server/Registry",
                          "TicTacToe uses manual endpoints and has no Registry role");
    ok &= require_absent (root / "samples/TicTacToe/Server/Session",
                          "TicTacToe Play owns the stream session");
    ok &= require_exists (root / "samples/TicTacToe/Client/tictactoe_client_options.hpp");
    ok &= require_exists (root / "samples/TicTacToe/Client/tictactoe_client_scenario.hpp");
    ok &= require_absent (root / "samples/TicTacToe/Client/session_actor_notification_inbox.hpp",
                          "TicTacToe client scenario waits on connector messages directly");
    ok &= require_absent (root / "samples/TicTacToe/Client/tictactoe_player_client.hpp",
                          "TicTacToe client scenario must not hide connector calls in a wrapper");
    ok &= require_absent (root / "samples/TicTacToe/Client/tictactoe_client.hpp",
                          "TicTacToe client scenario is kept in tictactoe_client_scenario.hpp");

    ok &= client_sample_uses_e2e_connector (root, "samples/Bingo/Client/main.cpp");
    ok &=
      client_sample_uses_e2e_connector (root, "samples/TicTacToe/Client/tictactoe_client_scenario.hpp");
    ok &= client_sample_does_not_include_server_implementation (root, "samples/Bingo/Client");
    ok &= client_sample_does_not_include_server_implementation (root, "samples/TicTacToe/Client");
    ok &= sample_client_targets_do_not_link_framework (root);
    ok &= file_does_not_contain (root / "connector/core/src/runtime/connector_runtime.hpp", "socket_fd",
                                 "C++ connector runtime must not use raw fd state");
    ok &= file_does_not_contain (root / "connector/core/src/runtime/connector_runtime.hpp", "recv(",
                                 "C++ connector runtime must not expose raw recv state");
    ok &= file_contains (root / "framework/src/runtime/handlers/handler_registry.cpp",
                         "runtime::handler_coroutine_executor ().submit");
    ok &= file_contains (root / "framework/src/runtime/handlers/handler_registry.cpp",
                         "co_await runtime::await_task_result");
    ok &= file_contains (root / "framework/src/runtime/http/http_request_pipeline.cpp",
                         "handler_coroutine_executor ().submit");
    ok &= file_contains (root / "framework/src/runtime/http/http_request_pipeline.cpp",
                         "co_await await_task_result");
    ok &= file_contains (root / "framework/src/runtime/spots/spot_runtime.cpp",
                         "runtime::serial_execution_queue_t");
    ok &= file_contains (root / "framework/src/runtime/spots/spot_runtime.cpp",
                         "try_post_serial_async");
    ok &= file_contains (root / "framework/src/runtime/streams/stream_runtime.cpp",
                         "handler_coroutine_executor ()");
    ok &= file_contains (root / "framework/src/runtime/streams/stream_runtime.cpp",
                         "co_await runtime::await_task_result");
    ok &= file_does_not_contain (
      root / "framework/src/runtime/channels/route_handler_invoker.cpp", ".result (",
      "route handler dispatch must await task_t instead of blocking with result()");
    ok &= file_does_not_contain (
      root / "framework/src/runtime/channels/route_handler_invoker.cpp", ".result(",
      "route handler dispatch must await task_t instead of blocking with result()");
    ok &= file_does_not_contain (
      root / "connector/core/include/zlink/stream_connector/contracts/calls/zlink_stream_calls.hpp",
      ".result", "connector callback submit must observe task completion instead of blocking");
    ok &= file_does_not_contain (
      root / "connector/core/include/zlink/stream_connector/codecs/auto_codec.hpp", ".result",
      "connector auto codec callback submit must observe task completion instead of blocking");
    ok &= file_does_not_contain (
      root / "connector/core/include/zlink/stream_connector/codecs/auto_codec.hpp", "on_completed ([&",
      "connector auto codec must not depend on immediate callback completion");
    ok &= file_does_not_contain (
      root / "connector/core/include/zlink/stream_connector/codecs/auto_codec.hpp",
      "std::optional<result_t", "connector auto codec must let task_t own result storage details");
    ok &= file_does_not_contain (
      root / "connector/core/include/zlink/stream_connector.hpp", "stream_e2e_client",
      "core connector umbrella must not include the e2e client surface");
    ok &= file_does_not_contain (
      root / "connector/core/include/zlink/stream_connector.hpp", "<coroutine>",
      "core connector umbrella must not include coroutine support");
    ok &= file_does_not_contain (
      root / "connector/core/include/zlink/stream_connector.hpp", "task_t",
      "core connector umbrella must not expose e2e task_t");
    ok &= file_contains (root / "CMakeLists.txt", "add_library(zlink_stream_e2e_client INTERFACE)");
    ok &= file_contains (root / "CMakeLists.txt", "add_library(zlink::stream_e2e_client ALIAS zlink_stream_e2e_client)");
    ok &= file_contains (root / "CMakeLists.txt",
                         "option(ZLINK_STREAM_CONNECTOR_BUILD_E2E_CLIENT");
    ok &= file_contains (root / "CMakeLists.txt",
                         "option(ZLINK_STREAM_CONNECTOR_BUILD_UNREAL");
    ok &= file_contains (root / "CMakeLists.txt",
                         "option(ZLINK_STREAM_CONNECTOR_BUILD_GODOT");
    ok &= file_contains (root / "CMakeLists.txt",
                         "option(ZLINK_STREAM_CONNECTOR_BUILD_AXMOL");
    ok &= file_contains (root / "CMakeLists.txt",
                         "if(ZLINK_STREAM_CONNECTOR_BUILD_E2E_CLIENT)");
    ok &= file_contains (root / "CMakeLists.txt", "add_library(zlink_stream_connector_throwing INTERFACE)");
    ok &= require_exists (
      root / "connector/throwing-adapter/include/zlink/stream_connector_throwing.hpp");
    ok &= file_does_not_contain (
      root / "connector/core/include/zlink/stream_connector.hpp",
      "stream_connector_throwing",
      "core connector umbrella must not include the throwing adapter surface");
    ok &= file_does_not_contain (
      root / "connector/core/src/runtime/connector_runtime.cpp", ".result",
      "connector send callback submit must observe task completion instead of blocking");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h",
      "zlink/stream_connector",
      "Unreal public API must not include the general C++ connector surface");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h", "task_t",
      "Unreal public API must not expose connector coroutine task_t");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h",
      "<coroutine>", "Unreal public API must not include coroutine support");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h",
      "co_await", "Unreal public API must not expose coroutine await syntax");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public/ZLinkStreamConnector.h", "submit",
      "Unreal public API must use delegates instead of connector submit calls");
    ok &= file_does_not_contain (
      root / "CMakeLists.txt",
      "target_link_libraries(zlink_unreal_stream_connector PUBLIC zlink::stream_connector)",
      "Unreal connector target must not expose the general C++ connector publicly");
    ok &= file_contains (root / "CMakeLists.txt",
                         "target_link_libraries(zlink_unreal_stream_connector PRIVATE\n"
                         "    zlink::stream_connector\n"
                         "    zlink::stream_connector_codecs)");
    ok &= file_does_not_contain (
      root / "CMakeLists.txt",
      "target_include_directories(zlink_unreal_stream_connector PRIVATE\n  "
      "${ZLINK_FRAMEWORK_CPP_DIR}/connector/core/src)",
      "Unreal connector target must not include general connector runtime internals");
    ok &= file_contains (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp",
      "#include <zlink/stream_connector.hpp>");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp",
      "enum class frame_",
      "Unreal adapter must not define its own STREAM frame enums");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp",
      "encode_frame",
      "Unreal adapter must delegate frame encoding to the core connector");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp",
      "decode_frame",
      "Unreal adapter must delegate frame decoding to the core connector");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp",
      "connector/core/src/runtime",
      "Unreal adapter must not include core connector runtime internals");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/ZLinkStreamConnector.Build.cs",
      "\"Sockets\"",
      "Unreal adapter must not use Unreal socket APIs when wrapping the non-Unreal core");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/ZLinkStreamConnector.Build.cs",
      "\"Networking\"",
      "Unreal adapter must not use Unreal networking APIs when wrapping the non-Unreal core");
    ok &= file_does_not_contain (
      root / "CMakeLists.txt", "ZLINK_STREAM_CONNECTOR_WITH_JSON",
      "Stream connector JSON helper is always included and must not expose a fake option");
    ok &= file_does_not_contain (
      root / "CMakePresets.json", "ZLINK_STREAM_CONNECTOR_WITH_JSON",
      "CMake presets must not set the removed Stream Connector JSON helper option");
    ok &= file_contains (root / "CMakeLists.txt",
                         "set(ZLINK_STREAM_CONNECTOR_MESSAGEPACK_ENABLED OFF)");
    ok &= file_contains (
      root / "CMakeLists.txt",
      "if(ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK AND TARGET zlink::cpp_codec_messagepack)");
    ok &=
      file_contains (root / "CMakeLists.txt", "$<$<BOOL:${ZLINK_STREAM_CONNECTOR_MESSAGEPACK_"
                                              "ENABLED}>:ZLINK_STREAM_CONNECTOR_WITH_MESSAGEPACK>");
    ok &=
      file_contains (root / "CMakeLists.txt", "set(ZLINK_STREAM_CONNECTOR_PROTOBUF_ENABLED OFF)");
    ok &= file_contains (
      root / "CMakeLists.txt",
      "if(ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF AND TARGET zlink::cpp_codec_protobuf)");
    ok &= file_contains (
      root / "CMakeLists.txt",
      "$<$<BOOL:${ZLINK_STREAM_CONNECTOR_PROTOBUF_ENABLED}>:ZLINK_STREAM_CONNECTOR_WITH_PROTOBUF>");
    ok &= file_does_not_contain (
      root / "cmake/zlink_framework_cppConfig.cmake.in",
      "@ZLINK_STREAM_CONNECTOR_EXPORT_MESSAGEPACK_DEPENDENCY@",
      "Framework package config must not restore connector MessagePack dependency");
    ok &= file_does_not_contain (
      root / "cmake/zlink_framework_cppConfig.cmake.in",
      "@ZLINK_STREAM_CONNECTOR_EXPORT_PROTOBUF_DEPENDENCY@",
      "Framework package config must not restore connector Protobuf dependency");
    ok &= file_contains (root / "cmake/zlink_stream_connector_cppConfig.cmake.in",
                         "@ZLINK_STREAM_CONNECTOR_EXPORT_OPENSSL_DEPENDENCY@");
    ok &= file_contains (root / "cmake/zlink_stream_connector_cppConfig.cmake.in",
                         "@ZLINK_STREAM_CONNECTOR_EXPORT_MESSAGEPACK_DEPENDENCY@");
    ok &= file_contains (root / "cmake/zlink_stream_connector_cppConfig.cmake.in",
                         "@ZLINK_STREAM_CONNECTOR_EXPORT_PROTOBUF_DEPENDENCY@");
    ok &= file_contains (root / "../../../bindings/cpp/CMakeLists.txt",
                         "install(TARGETS ${target_name}\n    EXPORT zlink_cppTargets)");
    ok &= file_contains (
      root / "CMakeLists.txt",
      "option(ZLINK_STREAM_CONNECTOR_WITH_LZ4 \"Enable Stream Connector LZ4 compression\" ON)");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/ZLinkStreamConnector.Build.cs",
      "\"Sockets\"",
      "Unreal plugin must not depend on Unreal socket APIs when core owns transport");
    ok &= file_does_not_contain (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/ZLinkStreamConnector.Build.cs",
      "\"Networking\"",
      "Unreal plugin must not depend on Unreal networking APIs when core owns transport");
    ok &= file_contains (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp",
      "TWeakObjectPtr<UZLinkStreamConnector>");
    ok &= file_contains (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Private/ZLinkStreamConnector.cpp",
      "DetachOwner");
    ok &= require_absent (
      root
        / "connector/engines/unreal/Source/ZLinkStreamConnector/Private/"
          "ZLinkStreamConnectorAutomationTests.cpp",
      "Unreal Automation Tests must live in the Rider/Editor-visible test module");
    ok &= file_contains (root / "connector/engines/unreal/ZLinkStreamConnector.uplugin",
                         "\"Name\": \"ZLinkStreamConnectorTests\"");
    ok &= file_contains (root / "connector/engines/unreal/ZLinkStreamConnector.uplugin",
                         "\"Type\": \"DeveloperTool\"");
    ok &= file_contains (
      root
        / "connector/engines/unreal/Source/ZLinkStreamConnectorTests/"
          "ZLinkStreamConnectorTests.Build.cs",
      "\"ZLinkStreamConnector\"");
    ok &= file_contains (root
                           / "connector/engines/unreal/Source/ZLinkStreamConnectorTests/Private/"
                             "ZLinkStreamConnectorAutomationTests.cpp",
                         "IMPLEMENT_SIMPLE_AUTOMATION_TEST");
    ok &= file_does_not_contain (root
                                   / "connector/engines/unreal/Source/ZLinkStreamConnectorTests/Private/"
                                     "ZLinkStreamConnectorAutomationTests.cpp",
                                 "FSocket",
                                 "Unreal Automation Test must not reimplement STREAM loopback protocol");
    ok &= require_exists (root / "connector/engines/godot/CMakeLists.txt");
    ok &= require_exists (
      root / "connector/engines/godot/include/zlink_godot_stream_connector.hpp");
    ok &= require_exists (
      root / "connector/engines/godot/src/zlink_godot_stream_connector.cpp");
    ok &= file_does_not_contain (
      root / "connector/engines/godot/include/zlink_godot_stream_connector.hpp",
      "zlink/stream_connector",
      "Godot public API must not expose the general C++ connector surface");
    ok &= file_contains (
      root / "connector/engines/godot/src/zlink_godot_stream_connector.cpp",
      "#include <zlink/stream_connector.hpp>");
    ok &= file_does_not_contain (
      root / "connector/engines/godot/src/zlink_godot_stream_connector.cpp",
      "enum class frame_",
      "Godot adapter must not define its own STREAM frame enums");
    ok &= file_contains (
      root / "connector/engines/godot/src/zlink_godot_stream_connector.cpp",
      "main_thread_dispatcher");
    ok &= file_contains (
      root / "connector/engines/godot/src/zlink_godot_stream_connector.cpp",
      "emit_request");
    ok &= require_exists (
      root / "connector/engines/godot/extension/zlink_stream_connector.gdextension");
    ok &= require_exists (
      root / "connector/engines/godot/tests/zlink_stream_connector_tests.gd");
    ok &= file_contains (
      root / "connector/engines/godot/tests/zlink_stream_connector_tests.gd",
      "engine-required");
    ok &= require_exists (root / "connector/engines/axmol/CMakeLists.txt");
    ok &= require_exists (
      root / "connector/engines/axmol/include/zlink_axmol_stream_connector.hpp");
    ok &= require_exists (
      root / "connector/engines/axmol/src/zlink_axmol_stream_connector.cpp");
    ok &= file_does_not_contain (
      root / "connector/engines/axmol/include/zlink_axmol_stream_connector.hpp",
      "zlink/stream_connector",
      "Axmol public API must not expose the general C++ connector surface");
    ok &= file_contains (
      root / "connector/engines/axmol/src/zlink_axmol_stream_connector.cpp",
      "#include <zlink/stream_connector.hpp>");
    ok &= file_does_not_contain (
      root / "connector/engines/axmol/src/zlink_axmol_stream_connector.cpp",
      "enum class frame_",
      "Axmol adapter must not define its own STREAM frame enums");
    ok &= file_contains (
      root / "connector/engines/axmol/src/zlink_axmol_stream_connector.cpp",
      "axmol_thread_dispatcher");
    ok &= file_contains (
      root / "connector/engines/axmol/src/zlink_axmol_stream_connector.cpp",
      "emit_request");
    ok &= require_exists (root / "connector/engines/axmol/tests/test_app.cpp");
    ok &= file_contains (root / "connector/engines/axmol/tests/test_app.cpp",
                         "engine-required");
    ok &= require_exists (root / "connector/core/packaging/vcpkg/vcpkg.json");
    ok &= require_exists (root / "connector/core/packaging/vcpkg/portfile.cmake");
    ok &= require_exists (root / "connector/core/packaging/conan/conanfile.py");
    ok &= require_exists (root / "connector/e2e-client/packaging/vcpkg/vcpkg.json");
    ok &= require_exists (root / "connector/e2e-client/packaging/vcpkg/portfile.cmake");
    ok &= require_exists (root / "connector/e2e-client/packaging/conan/conanfile.py");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/vcpkg.json",
                         "\"name\": \"zlink-stream-connector\"");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/vcpkg.json",
                         "\"boost-beast\"");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/vcpkg.json",
                         "\"vcpkg-cmake\"");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/vcpkg.json",
                         "\"msgpack-cxx\"");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/vcpkg.json",
                         "\"protobuf\"");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/vcpkg.json",
                         "\"openssl\"");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/vcpkg.json",
                         "\"lz4\"");
    ok &= file_contains (root / "connector/e2e-client/packaging/vcpkg/vcpkg.json",
                         "\"name\": \"zlink-stream-e2e-client\"");
    ok &= file_contains (root / "connector/core/packaging/conan/conanfile.py",
                         "name = \"zlink-stream-connector\"");
    ok &= file_contains (root / "connector/e2e-client/packaging/conan/conanfile.py",
                         "name = \"zlink-stream-e2e-client\"");
    ok &= file_contains (root / "connector/core/packaging/conan/conanfile.py",
                         "version = \"0.1.0\"");
    ok &= file_contains (root / "connector/e2e-client/packaging/conan/conanfile.py",
                         "version = \"0.1.0\"");
    ok &= file_contains (root / "connector/core/packaging/conan/conanfile.py",
                         "\"shared\": [True, False]");
    ok &= file_contains (root / "connector/e2e-client/packaging/conan/conanfile.py",
                         "package_type = \"header-library\"");
    ok &= file_contains (root / "connector/e2e-client/packaging/conan/conanfile.py",
                         "requires = \"zlink-stream-connector/0.1.0\"");
    ok &= file_does_not_contain (root / "connector/e2e-client/packaging/conan/conanfile.py",
                                 "CMake(self).install()",
                                 "e2e Conan package must not reinstall the core connector package");
    ok &= file_contains (root / "CMakeLists.txt",
                         "option(ZLINK_FRAMEWORK_CPP_INSTALL_FRAMEWORK");
    ok &= file_contains (root / "connector/core/packaging/conan/conanfile.py",
                         "ZLINK_FRAMEWORK_CPP_INSTALL_FRAMEWORK");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/portfile.cmake",
                         "-DZLINK_FRAMEWORK_CPP_INSTALL_FRAMEWORK=OFF");
    ok &= file_contains (root / "connector/e2e-client/packaging/vcpkg/portfile.cmake",
                         "zlink_stream_e2e_clientConfig.cmake");
    ok &= file_contains (root / "connector/e2e-client/packaging/vcpkg/portfile.cmake",
                         "find_dependency(zlink_stream_connector_cpp CONFIG)");
    ok &= file_does_not_contain (root / "connector/e2e-client/packaging/vcpkg/portfile.cmake",
                                 "vcpkg_cmake_install",
                                 "e2e vcpkg package must not reinstall the core connector package");
    ok &= file_does_not_contain (root / "connector/core/packaging/vcpkg/vcpkg.json",
                                 "unreal",
                                 "engine adapters must not be part of the core vcpkg package");
    ok &= file_does_not_contain (root / "connector/e2e-client/packaging/vcpkg/vcpkg.json",
                                 "unreal",
                                 "engine adapters must not be part of the e2e vcpkg package");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/portfile.cmake",
                         "-DZLINK_STREAM_CONNECTOR_BUILD_UNREAL=OFF");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/portfile.cmake",
                         "-DZLINK_STREAM_CONNECTOR_BUILD_GODOT=OFF");
    ok &= file_contains (root / "connector/core/packaging/vcpkg/portfile.cmake",
                         "-DZLINK_STREAM_CONNECTOR_BUILD_AXMOL=OFF");
    ok &= file_contains (root / "connector/doc/guide/01-overview.ko.md",
                         "TypeScript connector 사용");
    ok &= file_does_not_contain (root / "CMakeLists.txt",
                                 "cocos-connector",
                                 "C++ connector package names must not use ambiguous cocos-connector");
    ok &= file_does_not_contain (root / "connector/core/packaging/vcpkg/vcpkg.json",
                                 "cocos-connector",
                                 "core vcpkg package must not use ambiguous cocos-connector");
    ok &= file_does_not_contain (root / "connector/e2e-client/packaging/vcpkg/vcpkg.json",
                                 "cocos-connector",
                                 "e2e vcpkg package must not use ambiguous cocos-connector");
    ok &= http_client_public_surface_declares_general_client_features (root);
    ok &= stream_connector_public_surface_hides_runtime_internals (root);
    ok &= http_hosting_public_surface_excludes_non_goal_features (root);
    ok &= file_does_not_contain (
      root / "framework/include/zlink/framework/contracts/http/http.hpp",
      "http_options_builder_t &tls",
      "HTTP hosting public API must use configure_tls, not tls compatibility aliases");
    ok &= file_does_not_contain (
      root / "tests/Zlink.Framework.UnitTests/test_cpp_framework_app_host.cpp", ".tls (",
      "HTTP hosting regression tests must use configure_tls final API");
    ok &= file_contains (root / "framework/src/runtime/http/http_listener.cpp",
                         "asio::thread_pool _io_workers");
    ok &= file_does_not_contain (root / "framework/src/runtime/http/http_listener.cpp",
                                 "std::thread connection_thread",
                                 "HTTP hosting must not create one OS thread per connection");
    ok &=
      file_contains (root / "framework/src/runtime/http/http_listener.cpp", "_tls_context.emplace");
    ok &= file_contains (root / "framework/src/runtime/http/http_listener.cpp", "*_tls_context");
    ok &= file_does_not_contain (
      root / "framework/src/runtime/http/http_listener.cpp",
      "asio::ssl::context context (asio::ssl::context::tls_server)",
      "HTTP hosting must reuse listener TLS context instead of creating it per connection");

    ok &= public_headers_do_not_include_runtime (root / "framework/include");
    ok &= public_headers_do_not_include_runtime (root / "connector/core/include");
    ok &= public_headers_do_not_include_runtime (root / "http-client/include");
    ok &= public_headers_do_not_include_runtime (root / "extensions/include");
    ok &= public_headers_do_not_include_runtime (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public");
    ok &= public_headers_do_not_include_runtime (root / "connector/engines/godot/include");
    ok &= public_headers_do_not_include_runtime (root / "connector/engines/axmol/include");
    ok &= public_headers_do_not_expose_runtime_dependencies (root / "framework/include");
    ok &= public_headers_do_not_expose_runtime_dependencies (root / "connector/core/include");
    ok &= public_headers_do_not_expose_runtime_dependencies (root / "http-client/include");
    ok &= public_headers_do_not_expose_runtime_dependencies (root / "extensions/include");
    ok &= public_headers_do_not_expose_runtime_dependencies (
      root / "connector/engines/unreal/Source/ZLinkStreamConnector/Public");
    ok &= public_headers_do_not_expose_runtime_dependencies (
      root / "connector/engines/godot/include");
    ok &= public_headers_do_not_expose_runtime_dependencies (
      root / "connector/engines/axmol/include");
    ok &= sample_application_code_uses_message_codec (root);
    ok &= sample_server_code_does_not_block_on_task_result (root);
    ok &= contract_headers_have_compile_coverage (root, "framework/include", "");
    ok &= contract_headers_have_compile_coverage (root, "connector/core/include", "");
    ok &= contract_headers_have_compile_coverage (root, "http-client/include", "");
    ok &= contract_headers_have_compile_coverage (root, "extensions/include", "");
    ok &= draft_tracking_table_matches_files (root);
    ok &= non_empty_directories_do_not_keep_gitkeep (root);
    ok &= framework_overview_role_tables_cover_files (root);
    ok &= implementation_plan_expands_label_wildcards (root);
    ok &= implementation_plan_goal6_covers_parity_model (root);
    ok &= implementation_plan_goal7_covers_runtime_integration (root);
    ok &= implementation_plan_goal8_covers_handler_serializer_model (root);
    ok &= implementation_plan_goal9_covers_channel_messaging (root);
    ok &= implementation_plan_goal10_covers_backpressure_reliability (root);
    ok &= implementation_plan_goal11_covers_spot_runtime (root);
    ok &= actor_model_documents_actor_destroy_lifecycle (root);
    ok &= framework_api_documents_actor_destroy_lifecycle (root);
    ok &= session_actor_dispatch_documents_disconnect_destroy_boundary (root);
    ok &= implementation_plan_goal12_covers_spot_timer (root);
    ok &= implementation_plan_goal13_covers_stream_framework (root);
    ok &= implementation_plan_goal14_covers_actor_gateway (root);
    ok &= implementation_plan_goal15_covers_registry_topology (root);
    ok &= implementation_plan_goal16_covers_observability (root);
    ok &= implementation_plan_goal17_covers_module_hosting (root);
    ok &= implementation_plan_goal18_covers_http_client (root);
    ok &= implementation_plan_goal19_covers_http_hosting (root);
    ok &= implementation_plan_goal20_covers_connector_labels (root);
    ok &= implementation_plan_goal21_covers_sample_labels (root);
    ok &= implementation_plan_goal22_covers_final_label_axes (root);
    ok &= registry_draft_matches_monitoring_contract (root);
    ok &= posd_log_has_current_goal_mapping (root);
    ok &= cmake_extension_boundaries_hold (root);

    return ok ? 0 : 1;
}
