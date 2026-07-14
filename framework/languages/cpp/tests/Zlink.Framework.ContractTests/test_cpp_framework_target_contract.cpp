/* SPDX-License-Identifier: MPL-2.0 */

/* G0 target-contract gate for the C++ public-contract gap plan.
 * Each check maps to a ledger row in
 * framework/doc/plan/log/framework-public-contract-gap-implementation/
 * cpp-g0-contract-ledger.ko.md and stays red until the gap is closed.
 * The checks scan installed public headers and e2e wiring textually so the
 * build keeps compiling while target signatures are still missing. */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ZLINK_FRAMEWORK_CPP_SOURCE_DIR
#error "ZLINK_FRAMEWORK_CPP_SOURCE_DIR must be defined"
#endif

namespace
{

std::string read_file (const std::filesystem::path &path)
{
    std::ifstream input (path);
    std::ostringstream buffer;
    buffer << input.rdbuf ();
    return buffer.str ();
}

bool tree_contains (const std::filesystem::path &root, const std::string &needle)
{
    if (!std::filesystem::exists (root)) {
        return false;
    }
    for (const auto &entry : std::filesystem::recursive_directory_iterator (root)) {
        if (!entry.is_regular_file ()) {
            continue;
        }
        const auto ext = entry.path ().extension ();
        if (ext != ".hpp" && ext != ".h" && ext != ".cpp") {
            continue;
        }
        if (read_file (entry.path ()).find (needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

struct gate_t
{
    int failures = 0;

    void require (bool condition, const std::string &ledger_id, const std::string &message)
    {
        if (condition) {
            return;
        }
        std::cerr << ledger_id << ": " << message << '\n';
        ++failures;
    }
};

} // namespace

int main ()
{
    const std::filesystem::path root = ZLINK_FRAMEWORK_CPP_SOURCE_DIR;
    const auto include_root = root / "framework/include";
    const auto e2e_root = root / "e2e";
    const auto cmake = read_file (root / "CMakeLists.txt");
    const auto redis_hpp = read_file (
      root / "extensions/framework-locations-redis/include/zlink/locations/redis.hpp");
    const auto spot_runtime = read_file (root / "framework/src/runtime/spots/spot_runtime.cpp");
    const auto stream_host =
      read_file (root / "framework/src/runtime/streams/stream_host_service.cpp");
    const auto location_auto_connect =
      read_file (root / "framework/src/runtime/locations/location_auto_connect_host_service.hpp");
    const auto store_location_resolvers =
      read_file (root / "framework/src/runtime/locations/store_location_resolvers.hpp");
    const auto app_runtime = read_file (root / "framework/src/runtime/host/app.cpp");
    gate_t gate;

    for (const auto &required :
         {include_root, e2e_root, root / "framework/src", root / "connector/core"}) {
        if (!std::filesystem::exists (required)) {
            std::cerr << "target contract scan root is missing: " << required << '\n';
            return 1;
        }
    }

    const auto actor_hpp = read_file (include_root / "zlink/framework/contracts/actors/actor.hpp");
    const auto spot_hpp = read_file (include_root / "zlink/framework/contracts/spots/spot.hpp");
    const auto app_hpp =
      read_file (include_root / "zlink/framework/contracts/configuration/app.hpp");
    const auto services_hpp =
      read_file (include_root / "zlink/framework/contracts/configuration/services.hpp");
    const auto framework_options_hpp =
      read_file (include_root / "zlink/framework/contracts/configuration/framework_options.hpp");
    const auto framework_options_validation_hpp = read_file (
      include_root
      / "zlink/framework/contracts/configuration/detail/framework_options_validation.hpp");
    const auto execution_hpp =
      read_file (include_root / "zlink/framework/contracts/dispatch/execution.hpp");
    const auto events_hpp =
      read_file (include_root / "zlink/framework/contracts/eventing/events.hpp");
    const auto stream_hpp =
      read_file (include_root / "zlink/framework/contracts/streams/stream.hpp");
    const auto rows_hpp =
      read_file (include_root / "zlink/framework/contracts/locations/rows.hpp");
    const auto error_hpp =
      read_file (include_root / "zlink/framework/contracts/errors/error.hpp");
    const auto runner = read_file (e2e_root / "run_e2e_all.sh");
    const auto spot_service_runner = read_file (e2e_root / "SpotService/run_e2e.sh");
    const auto registration_codec_runner =
      read_file (e2e_root / "RegistrationCodec/run_e2e.sh");
    const auto pubsub_runner = read_file (e2e_root / "PubSub/run_e2e.sh");
    const auto resilience_client =
      read_file (e2e_root / "ResilienceLifecycle/Client/main.cpp");
    const auto resilience_b2 = read_file (
      e2e_root / "ResilienceLifecycle/Client/Scenarios/rl_b2_crash_during_inflight_scenario.hpp");
    const auto transfer_client = read_file (e2e_root / "SpotActorTransfer/Client/main.cpp");
    const auto transfer_server = read_file (e2e_root / "SpotActorTransfer/Server/ActorNode/main.cpp");

    /* IMP-CP-08 — session-owned transport failures reach the session callback. */
    gate.require (stream_host.find ("stream_session_error_t::transport_error")
                    != std::string::npos,
                  "IMP-CP-08",
                  "STREAM host does not classify a session transport failure");
    gate.require (stream_host.find ("_runtime.dispatch_error (") != std::string::npos,
                  "IMP-CP-08",
                  "STREAM host does not dispatch a session transport failure callback");

    /* IMP-CP-05 — every RouteMesh store row uses the Router role. */
    const auto route_role_begin = location_auto_connect.find (
      "case location_auto_connect_type_t::route_mesh:");
    const auto route_role_end = location_auto_connect.find (
      "case location_auto_connect_type_t::client_server:", route_role_begin);
    const auto route_role_block =
      route_role_begin != std::string::npos && route_role_end != std::string::npos
        ? location_auto_connect.substr (route_role_begin, route_role_end - route_role_begin)
        : std::string ();
    gate.require (location_auto_connect.find (
                    "route.router_channel_id (), location_role_t::dealer")
                    == std::string::npos,
                  "IMP-CP-05", "endpointless RouteMesh member still publishes a dealer row");
    gate.require (!route_role_block.empty ()
                    && route_role_block.find ("location_role_t::dealer") == std::string::npos,
                  "IMP-CP-05", "RouteMesh discovery still accepts dealer rows");

    /* IMP-CP-04 — incomplete and duplicate STREAM declarations fail validation. */
    for (const std::string required : {"stream_nodes_with_bind", "stream_nodes_with_session"}) {
        gate.require (framework_options_validation_hpp.find (required) != std::string::npos,
                      "IMP-CP-04", "STREAM startup validation is missing " + required);
    }
    gate.require (framework_options_hpp.find ("STREAM node '") != std::string::npos
                    && framework_options_hpp.find ("' is already registered")
                         != std::string::npos,
                  "IMP-CP-04", "duplicate STREAM node names are not rejected");
    gate.require (framework_options_hpp.find ("STREAM packet session '") != std::string::npos,
                  "IMP-CP-04", "duplicate STREAM packet session names are not rejected");

    /* IMP-CP-07 — pending and regressed actor rows never resolve successfully. */
    gate.require (store_location_resolvers.find ("row.generation < observed")
                    != std::string::npos,
                  "IMP-CP-07", "actor resolver does not reject regressed generations");
    gate.require (store_location_resolvers.find (
                    "row.actor_ref && !row.actor_ref->empty ()")
                    != std::string::npos,
                  "IMP-CP-07", "actor resolver does not reject pending actor rows");
    gate.require (app_runtime.find ("actor_location_observer") != std::string::npos,
                  "IMP-CP-07", "actor resolver and runtime query do not share generation state");

    /* CPP-G0-ASYNC-001 — one-way terminators return void. */
    gate.require (!tree_contains (include_root, "result_t<void> submit ()"), "CPP-G0-ASYNC-001",
                  "one-way submit terminators still return result_t<void>");
    gate.require (actor_hpp.find ("void submit") != std::string::npos, "CPP-G0-ASYNC-001",
                  "actor one-way send does not expose the target `void submit()` terminator");

    /* CPP-G0-ASYNC-002 — relay/disconnect complete as task_t<void>. */
    gate.require (actor_hpp.find ("task_t<void> relay") != std::string::npos, "CPP-G0-ASYNC-002",
                  "session_actor_t::relay does not return task_t<void>");
    gate.require (actor_hpp.find ("task_t<void> notify_disconnected") != std::string::npos,
                  "CPP-G0-ASYNC-002", "session_actor_t::notify_disconnected does not return "
                                     "task_t<void>");
    gate.require (actor_hpp.find ("task_t<void> disconnect") != std::string::npos,
                  "CPP-G0-ASYNC-002", "bound_session_t::disconnect does not return task_t<void>");

    /* CPP-G0-ASYNC-003 — no public yield/callback execution-mode surface. */
    gate.require (!tree_contains (include_root, "yield"), "CPP-G0-ASYNC-003",
                  "public headers still expose yield-based execution surfaces");

    /* CPP-G0-CANCEL-001 — no framework-specific cancellation token. */
    gate.require (!tree_contains (include_root, "cancellation_token_t"), "CPP-G0-CANCEL-001",
                  "cancellation_token_t is still exported from public headers");

    /* CPP-G0-NAME-001 — snake_case lifecycle callbacks only. */
    for (const std::string forbidden : {"onCreateActor", "onLeaveActor", "onDisconnectActor",
                                        "destroyActor"}) {
        gate.require (!tree_contains (include_root, forbidden), "CPP-G0-NAME-001",
                      "camelCase lifecycle name is still public: " + forbidden);
        gate.require (!tree_contains (root / "framework/src", forbidden), "CPP-G0-NAME-001",
                      "camelCase lifecycle name survives in runtime: " + forbidden);
    }
    for (const std::string required : {"on_create_actor", "on_leave_actor", "on_disconnect_actor",
                                       "destroy_actor"}) {
        gate.require (tree_contains (include_root, required), "CPP-G0-NAME-001",
                      "snake_case lifecycle name is missing: " + required);
    }

    /* CPP-G0-ERROR-001 — enumerators outside the fixed contract set are gone. */
    const auto enum_begin = error_hpp.find ("enum class framework_error_kind_t");
    const auto enum_end = error_hpp.find ("};", enum_begin);
    const auto enum_block = enum_begin == std::string::npos
                              ? std::string ()
                              : error_hpp.substr (enum_begin, enum_end - enum_begin);
    for (const std::string forbidden : {"actor_stale_generation", "timeout", "shutdown",
                                        "disconnected", "closed", "cancelled"}) {
        gate.require (enum_block.find (forbidden) == std::string::npos, "CPP-G0-ERROR-001",
                      "framework_error_kind_t still exposes non-contract value: " + forbidden);
    }

    /* CPP-G0-SPOTHANDLE-001 — opaque handle replaces spot_ref_t. */
    gate.require (!tree_contains (include_root, "spot_ref_t"), "CPP-G0-SPOTHANDLE-001",
                  "public spot_ref_t address snapshot is still exported");
    for (const std::string required : {"spot_handle_t", "spot_handle_resolver_t",
                                       "actor_spot_handle_resolver_t", "send_to_spot",
                                       "request_to_spot"}) {
        gate.require (tree_contains (include_root, required), "CPP-G0-SPOTHANDLE-001",
                      "spot handle surface is missing: " + required);
    }

    /* CPP-G0-ACTOR-001 — nullable spot rid is the single membership source. */
    gate.require (actor_hpp.find ("is_joined") == std::string::npos, "CPP-G0-ACTOR-001",
                  "actor_context_t::is_joined is still public");
    gate.require (actor_hpp.find ("std::optional<spot_rid_t> spot_rid") != std::string::npos,
                  "CPP-G0-ACTOR-001", "actor_context_t::spot_rid() nullable accessor is missing");

    /* CPP-G0-ACTOR-002 — join result is an accepted/rejected variant. */
    for (const std::string required : {"actor_join_accepted_t", "actor_join_rejected_t"}) {
        gate.require (actor_hpp.find (required) != std::string::npos, "CPP-G0-ACTOR-002",
                      "variant join result type is missing: " + required);
    }

    /* CPP-G0-SPOTMGR-001 — async spot queries. */
    gate.require (spot_hpp.find ("task_t<std::optional<spot_info_t>> find_spot")
                    != std::string::npos,
                  "CPP-G0-SPOTMGR-001", "find_spot is not async");
    gate.require (spot_hpp.find ("task_t<std::vector<spot_info_t>> list_spots")
                    != std::string::npos,
                  "CPP-G0-SPOTMGR-001", "list_spots is not async");

    /* CPP-G0-CONN-001 — capability endpoint runtime handle. */
    gate.require (tree_contains (include_root, "endpoint_connections_t"), "CPP-G0-CONN-001",
                  "endpoint_connections_t runtime handle is missing");

    /* CPP-G0-DISPATCH-001 — no dispatch-mode surface, no typed packet-name override. */
    for (const std::string forbidden : {"dispatch_mode_t", "spot_dispatch_mode",
                                        "stream_dispatch_mode"}) {
        gate.require (!tree_contains (include_root, forbidden), "CPP-G0-DISPATCH-001",
                      "dispatch optimization surface is still public: " + forbidden);
    }
    for (const std::string forbidden :
         {"request_call_t &packet_name", "send_call_t &packet_name",
          "actor_send_call_t &packet_name", "actor_request_call_t &packet_name"}) {
        gate.require (!tree_contains (include_root, forbidden), "CPP-G0-DISPATCH-001",
                      "typed call still exposes packet_name override: " + forbidden);
    }

    /* CPP-G0-STREAM-001 — typed session handler surface. */
    gate.require (tree_contains (include_root, "typed_session_packet_handler"),
                  "CPP-G0-STREAM-001", "typed stream session handler contract is missing");

    /* CPP-G0-ROUTEMESH-001 — spec registration name and runtime options. */
    gate.require (!tree_contains (include_root, "add_route_mesh_channel"), "CPP-G0-ROUTEMESH-001",
                  "legacy add_route_mesh_channel registration name is still public");
    gate.require (tree_contains (include_root, "add_route_mesh"), "CPP-G0-ROUTEMESH-001",
                  "add_route_mesh registration entry point is missing");
    gate.require (tree_contains (include_root, "route_mesh_channel_runtime_options_t"),
                  "CPP-G0-ROUTEMESH-001", "route-mesh runtime options surface is missing");

    /* CPP-G0-FLOW-001 — flow correlation fields and wire marker. */
    gate.require (execution_hpp.find ("flow_id") != std::string::npos, "CPP-G0-FLOW-001",
                  "message_flow_event_t lacks flow_id");
    gate.require (execution_hpp.find ("flow_origin_t") != std::string::npos, "CPP-G0-FLOW-001",
                  "flow_origin_t enum is missing");
    gate.require (stream_hpp.find ("has_flow_id") != std::string::npos, "CPP-G0-FLOW-001",
                  "stream header flag has_flow_id is missing");
    gate.require (tree_contains (root / "framework/src", "0xF2")
                    || tree_contains (root / "framework/src", "0xf2"),
                  "CPP-G0-FLOW-001", "0xF2 envelope format marker is not encoded");

    /* CPP-G0-METRIC-001 — metric payload carries catalog fields. */
    for (const std::string required : {"metric_instrument_kind_t", "metric_temporality_t",
                                       "std::string unit;"}) {
        gate.require (events_hpp.find (required) != std::string::npos, "CPP-G0-METRIC-001",
                      "metric_event_payload_t catalog field is missing: " + required);
    }

    /* CPP-G0-DRAIN-001 — graceful drain surface. */
    for (const std::string required : {"drain_result_t", "await_drained", "is_ready"}) {
        gate.require (app_hpp.find (required) != std::string::npos, "CPP-G0-DRAIN-001",
                      "app_t drain surface is missing: " + required);
    }
    gate.require (rows_hpp.find ("draining") != std::string::npos, "CPP-G0-DRAIN-001",
                  "peer location row lacks the typed draining field");
    gate.require (tree_contains (include_root, "spot_drain_policy_t"), "CPP-G0-DRAIN-001",
                  "spot_drain_policy_t is missing");
    gate.require (tree_contains (include_root, "stream_close_reason_t"), "CPP-G0-DRAIN-001",
                  "stream_close_reason_t is missing");
    gate.require (tree_contains (root / "connector/core", "close_reason"), "CPP-G0-DRAIN-001",
                  "connector does not expose a session close reason");

    /* CPP-G0-DI-001 — optional service lookup. */
    gate.require (services_hpp.find ("std::optional<std::reference_wrapper") != std::string::npos,
                  "CPP-G0-DI-001", "service_provider_t::get<T>() optional lookup is missing");

    /* CPP-G0-E2E-001 — Config 8 fixture migrated to AutomaticTurnDispatch. */
    gate.require (!std::filesystem::exists (e2e_root / "YieldDispatch"), "CPP-G0-E2E-001",
                  "e2e/YieldDispatch fixture directory still exists");
    gate.require (std::filesystem::exists (e2e_root / "AutomaticTurnDispatch"), "CPP-G0-E2E-001",
                  "e2e/AutomaticTurnDispatch fixture directory is missing");
    gate.require (runner.find ("YieldDispatch") == std::string::npos, "CPP-G0-E2E-001",
                  "run_e2e_all.sh still registers YieldDispatch");
    gate.require (runner.find ("AutomaticTurnDispatch") != std::string::npos, "CPP-G0-E2E-001",
                  "run_e2e_all.sh does not register AutomaticTurnDispatch");

    /* CPP-G0-E2E-002 — Config 11 fixture exists. */
    gate.require (std::filesystem::exists (e2e_root / "ObservabilityOps"), "CPP-G0-E2E-002",
                  "e2e/ObservabilityOps fixture directory is missing");
    gate.require (runner.find ("ObservabilityOps") != std::string::npos, "CPP-G0-E2E-002",
                  "run_e2e_all.sh does not register ObservabilityOps");

    /* CPP-G0-E2E-003 — the integrated runner uses the eleven common E2E configs. */
    gate.require (runner.find ("SpotActorTransfer") != std::string::npos, "CPP-G0-E2E-003",
                  "run_e2e_all.sh does not register Config 10 SpotActorTransfer");
    gate.require (runner.find ("DeliveryDispatch") == std::string::npos, "CPP-G0-E2E-003",
                  "run_e2e_all.sh registers the non-contract DeliveryDispatch fork");
    gate.require (runner.find ("already bound") != std::string::npos, "CPP-G0-E2E-003",
                  "run_e2e_all.sh omits the common transient bind error token");

    /* CPP-G0-E2E-004 — ST-A1 verifies lifecycle evidence in contract order. */
    gate.require (transfer_client.find ("assert_evidence_sequence") != std::string::npos,
                  "CPP-G0-E2E-004", "ST-A1 has no cross-kind evidence order assertion");
    gate.require (transfer_server.find ("location_committed") != std::string::npos,
                  "CPP-G0-E2E-004", "SpotActorTransfer emits no location_committed evidence");

    /* E2E-CP-16 — the default SpotService gate includes the implemented SM-D2 P0 scenario. */
    const auto all_scenarios = spot_service_runner.find ("for scenario in");
    const auto all_scenarios_end = spot_service_runner.find ("; do", all_scenarios);
    const auto all_scenario_list =
      all_scenarios != std::string::npos && all_scenarios_end != std::string::npos
        ? spot_service_runner.substr (all_scenarios, all_scenarios_end - all_scenarios)
        : std::string{};
    gate.require (all_scenarios != std::string::npos && all_scenarios_end != std::string::npos
                    && all_scenario_list.find ("SM-D2") != std::string::npos,
                  "E2E-CP-16", "SpotService all mode omits the implemented SM-D2 P0 scenario");

    /* E2E-CP-05 — all mode follows the common Track F inventory only. */
    for (const auto *scenario : {"SM-F3", "SM-F4", "SM-F5"}) {
        gate.require (all_scenario_list.find (scenario) != std::string::npos, "E2E-CP-05",
                      std::string ("SpotService all mode omits ") + scenario);
    }
    gate.require (all_scenario_list.find ("SM-Q9") == std::string::npos, "E2E-CP-05",
                  "SpotService all mode includes non-contract SM-Q9");

    /* E2E-CP-18 — SM-E1 proves both missing-handler flow classifications. */
    const auto sm_e1_begin = spot_service_runner.find ("if [[ \"$SCENARIO\" == \"SM-E1\"");
    const auto sm_e1_end = spot_service_runner.find ("if [[ \"$SCENARIO\" == \"SM-E2\"", sm_e1_begin);
    const auto sm_e1_block =
      sm_e1_begin != std::string::npos && sm_e1_end != std::string::npos
        ? spot_service_runner.substr (sm_e1_begin, sm_e1_end - sm_e1_begin)
        : std::string{};
    gate.require (sm_e1_block.find (
                    "reason=handler_missing.*action=reply_error.*packet=MissingSpotReq")
                    != std::string::npos,
                  "E2E-CP-18", "SM-E1 does not assert request message-flow error evidence");
    gate.require (sm_e1_block.find (
                    "reason=handler_missing.*action=drop.*packet=MissingSpotMsg")
                    != std::string::npos,
                  "E2E-CP-18", "SM-E1 does not assert send message-flow error evidence");

    /* E2E-CP-30 — RC-A6 is a startup-negative path, not a client scenario. */
    gate.require (registration_codec_runner.find ("== rc-a*") == std::string::npos,
                  "E2E-CP-30", "RegistrationCodec routes RC-A6 through the client glob");
    gate.require (registration_codec_runner.find ("== rc-a[1-5]") != std::string::npos,
                  "E2E-CP-30", "RegistrationCodec client selector does not name RC-A1 through A5");

    /* E2E-CP-48 — submit-only publish produces no publisher dispatch-error marker. */
    gate.require (pubsub_runner.find ("publisher dispatch negative passed") != std::string::npos,
                  "E2E-CP-48", "PubSub PS-C1 does not report its publisher-side negative");
    gate.require (pubsub_runner.find ("publisher emitted a dispatch error for submit-only publish")
                    != std::string::npos,
                  "E2E-CP-48", "PubSub PS-C1 has no failing publisher dispatch assertion");

    /* E2E-CP-31 — runner-owned RL-C2/RL-D1 scenarios have no dead client duplicates. */
    gate.require (
      resilience_client.find ("rl_c2_topology_recovery_scenario.hpp") == std::string::npos,
      "E2E-CP-31", "ResilienceLifecycle client still includes the dead RL-C2 wrapper");
    gate.require (
      resilience_client.find ("rl_d1_high_fanout_scenario.hpp") == std::string::npos,
      "E2E-CP-31", "ResilienceLifecycle client still includes the dead RL-D1 wrapper");
    gate.require (
      !std::filesystem::exists (
        e2e_root / "ResilienceLifecycle/Client/Scenarios/rl_c2_topology_recovery_scenario.hpp"),
      "E2E-CP-31", "ResilienceLifecycle retains the dead RL-C2 scenario file");
    gate.require (
      !std::filesystem::exists (
        e2e_root / "ResilienceLifecycle/Client/Scenarios/rl_d1_high_fanout_scenario.hpp"),
      "E2E-CP-31", "ResilienceLifecycle retains the dead RL-D1 scenario file");

    /* IMP-CP-28 — unsupported extension placeholders are not public package surface. */
    gate.require (
      !std::filesystem::exists (
        root / "extensions/include/zlink/framework/extensions/extension_boundaries.hpp"),
      "IMP-CP-28", "unsupported extension_boundaries.hpp remains installable");
    gate.require (
      !std::filesystem::exists (root / "extensions/include/zlink/framework/extensions.hpp"),
      "IMP-CP-28", "unsupported framework extensions umbrella remains installable");
    gate.require (cmake.find ("add_zlink_framework_extension") == std::string::npos,
                  "IMP-CP-28", "unsupported no-op framework extension targets remain exported");
    gate.require (cmake.find ("zlink_framework_extension_metrics") == std::string::npos,
                  "IMP-CP-28", "unsupported metrics extension target remains public");

    /* IMP-CP-33 — do not accept a diagnostics option that has no runtime effect. */
    gate.require (execution_hpp.find ("include_native_diagnostics") == std::string::npos,
                  "IMP-CP-33", "no-op include_native_diagnostics remains public");

    /* E2E-CP-47 — fast subscribers must finish inside a bound shorter than slow HOL work. */
    gate.require (pubsub_runner.find ("ThreadPoolExecutor(max_workers=2)") != std::string::npos,
                  "E2E-CP-47", "PS-B1 fast-subscriber waits are not concurrent");
    gate.require (pubsub_runner.find ("timeout_ms=2000") != std::string::npos,
                  "E2E-CP-47", "PS-B1 fast-subscriber wait has no isolation bound");
    gate.require (pubsub_runner.find ("fast subscriber isolation exceeded 2500 ms")
                    != std::string::npos,
                  "E2E-CP-47", "PS-B1 cannot fail when fast delivery is head-of-line blocked");

    /* E2E-CP-32 — the outer HTTP deadline must not preempt the 1s provider handler. */
    gate.require (resilience_b2.find (".timeout (std::chrono::milliseconds (500))")
                    == std::string::npos,
                  "E2E-CP-32", "RL-B2 still times out before the slow provider can reply");
    gate.require (resilience_b2.find (".timeout (std::chrono::milliseconds (5000))")
                    != std::string::npos,
                  "E2E-CP-32", "RL-B2 has no outer deadline longer than its channel deadline");

    /* IMP-CP-38 — lease removal and snapshot each execute as one Redis script. */
    gate.require (redis_hpp.find ("eval<std::tuple<long long, long long>>")
                    != std::string::npos,
                  "IMP-CP-38", "owner lease removal is not a single scripted decision");
    gate.require (redis_hpp.find ("eval<std::tuple<long long, std::vector<std::string>>>")
                    != std::string::npos,
                  "IMP-CP-38", "owner lease snapshot is not returned by one Redis script");

    /* IMP-CP-37 — actor physical rows use the common four-field, global-stamp schema. */
    gate.require (redis_hpp.find (
                    "write_row (location_kind_t::actor, row_key, std::nullopt, actor.owner_id")
                    != std::string::npos,
                  "IMP-CP-37", "actor writes still add the C++-only mesh hash field");

    /* IMP-CP-36 — paged location lists preserve the Redis SSCAN cursor. */
    gate.require (redis_hpp.find ("\"SSCAN\"") != std::string::npos,
                  "IMP-CP-36", "Redis location paging does not use SSCAN");
    gate.require (redis_hpp.find ("parse_scan_state") != std::string::npos,
                  "IMP-CP-36", "Redis location paging does not preserve cursor state");
    gate.require (redis_hpp.find ("parse_offset") == std::string::npos,
                  "IMP-CP-36", "Redis location paging still treats continuation as an offset");

    /* E2E-CP-64 — the sample is not duplicated as a twelfth, non-contract config. */
    gate.require (!std::filesystem::exists (e2e_root / "DeliveryDispatch/run_e2e.sh"),
                  "E2E-CP-64", "stray DeliveryDispatch e2e fork remains tracked");
    gate.require (cmake.find ("zlink_cpp_e2e_delivery_dispatch") == std::string::npos,
                  "E2E-CP-64", "stray DeliveryDispatch e2e targets remain buildable");

    /* IMP-CP-01 — subscription dispatch key is topic plus decoded packet name. */
    gate.require (spot_runtime.find ("descriptor.packet_name == *packet_name")
                    != std::string::npos,
                  "IMP-CP-01", "spot subscription lookup ignores the wire packet name");

    if (gate.failures != 0) {
        std::cerr << "target contract gate failures: " << gate.failures << '\n';
        return 1;
    }
    std::cout << "target contract gate satisfied\n";
    return 0;
}
