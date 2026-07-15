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
    const auto live_location_reader =
      read_file (root / "framework/src/runtime/locations/live_location_reader.hpp");
    const auto app_runtime = read_file (root / "framework/src/runtime/host/app.cpp");
    const auto actor_gateway_spot_bridge =
      read_file (root / "framework/src/runtime/host/actor_gateway_spot_bridge.cpp");
    const auto channel_outbound_exchange =
      read_file (root / "framework/src/runtime/channels/channel_outbound_exchange.cpp");
    const auto pubsub_client_root = e2e_root / "PubSub/Client";
    const auto pubsub_client_support =
      read_file (pubsub_client_root / "Support/client_support.hpp");
    const auto pubsub_fanout_scenario =
      read_file (pubsub_client_root / "Scenarios/fanout_basic_delivery_scenario.hpp");
    const auto pubsub_slow_scenario =
      read_file (pubsub_client_root / "Scenarios/slow_subscriber_scenario.hpp");
    const auto runtime_monitoring_runner = read_file (e2e_root / "RuntimeMonitoring/run_e2e.sh");
    const auto runtime_monitoring_a1 = read_file (
      e2e_root / "RuntimeMonitoring/Client/Scenarios/mon_a1_socket_events_scenario.hpp");
    const auto runtime_monitoring_a4 = read_file (
      e2e_root / "RuntimeMonitoring/Client/Scenarios/mon_a4_availability_transition_scenario.hpp");
    const auto runtime_monitoring_d1 = read_file (
      e2e_root / "RuntimeMonitoring/Client/Scenarios/mon_d1_failure_recovery_scenario.hpp");
    const auto runtime_monitoring_recorders = read_file (
      e2e_root / "RuntimeMonitoring/Server/Shared/monitoring_event_recorders.hpp");
    const auto store_failure_client =
      read_file (e2e_root / "DiscoveryRegistryHa/Client/main.cpp");
    const auto store_failure_support =
      read_file (e2e_root / "DiscoveryRegistryHa/Client/Support/client_support.hpp");
    const auto store_failure_runner =
      read_file (e2e_root / "DiscoveryRegistryHa/run_e2e.sh");
    const auto store_failure_consumer =
      read_file (e2e_root / "DiscoveryRegistryHa/Server/Consumer/main.cpp");
    const auto store_failure_provider =
      read_file (e2e_root / "DiscoveryRegistryHa/Server/Provider/main.cpp");
    const auto store_failure_provider_lifecycle = read_file (
      e2e_root
      / "DiscoveryRegistryHa/Server/Provider/Infrastructure/provider_lifecycle_control.hpp");
    const auto store_failure_consumer_endpoints = read_file (
      e2e_root / "DiscoveryRegistryHa/Server/Consumer/Endpoints/consumer_endpoints.hpp");
    const auto store_failure_location_store = read_file (
      e2e_root / "DiscoveryRegistryHa/Server/Shared/location_store.hpp");
    const auto store_failure_contracts =
      read_file (e2e_root / "DiscoveryRegistryHa/Shared/store_failure_contracts.hpp");
    const std::vector<std::string> pubsub_client_scenarios{
      pubsub_fanout_scenario,
      read_file (pubsub_client_root / "Scenarios/topic_filter_scenario.hpp"),
      read_file (pubsub_client_root / "Scenarios/late_subscriber_scenario.hpp"),
      read_file (pubsub_client_root / "Scenarios/subscriber_reconnect_scenario.hpp"),
      pubsub_slow_scenario,
      read_file (pubsub_client_root / "Scenarios/publisher_restart_scenario.hpp"),
      read_file (pubsub_client_root / "Scenarios/missing_message_name_scenario.hpp")};
    gate_t gate;

    for (const auto &required :
         {include_root, e2e_root, root / "framework/src", root / "connector/core"}) {
        if (!std::filesystem::exists (required)) {
            std::cerr << "target contract scan root is missing: " << required << '\n';
            return 1;
        }
    }

    const auto actor_hpp = read_file (include_root / "zlink/framework/contracts/actors/actor.hpp");
    const auto channel_hpp =
      read_file (include_root / "zlink/framework/contracts/channels/channel.hpp");
    const auto zlink_builder_hpp =
      read_file (include_root / "zlink/framework/contracts/configuration/zlink_builder.hpp");
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
    const auto registration_codec_client =
      read_file (e2e_root / "RegistrationCodec/Client/main.cpp");
    const auto registration_codec_a6 = read_file (
      e2e_root / "RegistrationCodec/Client/Scenarios/rc_a6_invalid_registration_scenario.hpp");
    const auto pubsub_runner = read_file (e2e_root / "PubSub/run_e2e.sh");
    const auto transfer_runner = read_file (e2e_root / "SpotActorTransfer/run_e2e.sh");
    const auto transfer_feature_map =
      read_file (e2e_root / "SpotActorTransfer/feature-map.ko.md");
    const auto observability_runner = read_file (e2e_root / "ObservabilityOps/run_e2e.sh");
    const auto observability_feature_map =
      read_file (e2e_root / "ObservabilityOps/feature-map.ko.md");
    const auto resilience_client =
      read_file (e2e_root / "ResilienceLifecycle/Client/main.cpp");
    const auto resilience_feature_map =
      read_file (e2e_root / "ResilienceLifecycle/feature-map.ko.md");
    const auto resilience_runner = read_file (e2e_root / "ResilienceLifecycle/run_e2e.sh");
    const auto resilience_b2 = read_file (
      e2e_root / "ResilienceLifecycle/Client/Scenarios/rl_b2_crash_during_inflight_scenario.hpp");
    const auto messaging_test =
      read_file (root / "tests/Zlink.Framework.UnitTests/test_cpp_framework_messaging.cpp");
    const auto transfer_client = read_file (e2e_root / "SpotActorTransfer/Client/main.cpp");
    const auto transfer_server = read_file (e2e_root / "SpotActorTransfer/Server/ActorNode/main.cpp");
    const auto spot_service_f5 = read_file (
      e2e_root / "SpotService/Client/Scenarios/sm_f5_scenario.hpp");
    const auto spot_service_f4 = read_file (
      e2e_root / "SpotService/Client/Scenarios/sm_f4_scenario.hpp");

    /* E2E-CP-13 — every common E2E configuration has an implementation map. */
    gate.require (
      std::filesystem::exists (e2e_root / "SpotActorTransfer/feature-map.ko.md"),
      "E2E-CP-13",
      "Config 10 SpotActorTransfer is missing feature-map.ko.md");

    /* D5 / E2E-CP-56 — actor creation and join begin on the node that owns the
     * actor. Config 10 must not recreate the removed remote-controller API. */
    gate.require (transfer_runner.find ("start_role actor actor-a") != std::string::npos
                    && transfer_runner.find ("start_role actor actor-b") != std::string::npos
                    && transfer_runner.find ("start_role session session-a")
                         != std::string::npos
                    && transfer_runner.find ("start_role session session-b")
                         != std::string::npos
                    && transfer_runner.find ("start_role controller") == std::string::npos
                    && transfer_runner.find ("CONTROLLER_URL") == std::string::npos
                    && transfer_runner.find ("actor-c") == std::string::npos,
                  "E2E-CP-56", "Config 10 still uses a remote actor controller");
    gate.require (transfer_runner.find ("\"nodeAStream\"")
                         != std::string::npos
                    && transfer_runner.find ("\"nodeBStream\"")
                         != std::string::npos
                    && transfer_runner.find ("ZLINK_CPP_E2E_NODE_A_STREAM")
                         == std::string::npos
                    && transfer_runner.find ("ZLINK_CPP_E2E_NODE_B_STREAM")
                         == std::string::npos,
                  "E2E-CP-56", "Config 10 does not pass the session gateway endpoints");
    gate.require (transfer_client.find ("create_actor (_nodes.a")
                    != std::string::npos
                    && transfer_client.find ("join_actor (_nodes.a")
                         != std::string::npos
                    && transfer_client.find (
                         "join_actor (_nodes.b, actor_id, {\"ST-F5\", spot_a_final})")
                         != std::string::npos
                    && transfer_client.find ("probe_actor (_nodes.a")
                         != std::string::npos,
                  "E2E-CP-56", "Config 10 does not create and join actors on their owner node");
    gate.require (transfer_feature_map.find ("`deferred`") == std::string::npos
                    && transfer_feature_map.find ("transfer controller") == std::string::npos,
                  "E2E-CP-56", "Config 10 feature map still promises remote actor control");
    const auto bound_session_registration =
      actor_gateway_spot_bridge.find ("actor_gateway.on_bound_session");
    const auto local_session_route = actor_gateway_spot_bridge.find (
      "actor_ref.node_rid ().value () == binding.local_spot_node_rid",
      bound_session_registration);
    const auto remote_session_route = actor_gateway_spot_bridge.find (
      "register_bound_session_route_through_mesh", bound_session_registration);
    gate.require (bound_session_registration != std::string::npos
                    && local_session_route != std::string::npos
                    && remote_session_route != std::string::npos
                    && local_session_route < remote_session_route,
                  "E2E-CP-56",
                  "local actor session binding still depends on a remote mesh route");

    /* E2E-CP-09 — local E2E waits use the common named defaults. */
    for (const auto *candidate : {&transfer_runner, &observability_runner}) {
        gate.require (candidate->find ("LOCAL_READINESS_TIMEOUT_SECONDS=3")
                        != std::string::npos,
                      "E2E-CP-09",
                      "runner does not declare the 3s local readiness timeout");
        gate.require (candidate->find ("LOCAL_READINESS_POLL_SECONDS=0.1")
                        != std::string::npos,
                      "E2E-CP-09",
                      "runner does not declare the 0.1s readiness poll interval");
        gate.require (candidate->find ("ROUTE_SETTLE_SECONDS=5") != std::string::npos,
                      "E2E-CP-09",
                      "runner does not declare the 5s route settle interval");
        gate.require (candidate->find ("SCENARIO_SETTLE_SECONDS=3") != std::string::npos,
                      "E2E-CP-09",
                      "runner does not declare the 3s scenario settle interval");
        gate.require (candidate->find ("HTTP_PROBE_TIMEOUT_SECONDS=3")
                        != std::string::npos,
                      "E2E-CP-09",
                      "runner does not declare the 3s HTTP probe timeout");
        gate.require (candidate->find ("sleep 5") == std::string::npos
                        && candidate->find ("sleep 2") == std::string::npos
                        && candidate->find ("sleep 1") == std::string::npos,
                      "E2E-CP-09",
                      "runner still hides settle semantics behind a numeric sleep");
        gate.require (candidate->find ("--max-time \"$HTTP_PROBE_TIMEOUT_SECONDS\"")
                        != std::string::npos,
                      "E2E-CP-09",
                      "runner HTTP probes do not use the named 3s timeout");
    }

    /* E2E-CP-11 — feature-map status agrees with its documented gaps. */
    gate.require (observability_feature_map.find ("| OBS-B1 | `deferred` |")
                    != std::string::npos,
                  "E2E-CP-11",
                  "OBS-B1 reconnect gap is still reported as implemented");
    gate.require (observability_feature_map.find ("| OBS-B3 | `deferred` |")
                    != std::string::npos,
                  "E2E-CP-11",
                  "OBS-B3 lease-latency gap is still reported as implemented");
    gate.require (observability_feature_map.find ("| OBS-C2 | `deferred` |")
                    != std::string::npos,
                  "E2E-CP-11",
                  "OBS-C2 bound-session gap is still reported as implemented");
    gate.require (observability_feature_map.find ("구현") == std::string::npos
                    && observability_feature_map.find ("구현(부분)") == std::string::npos,
                  "E2E-CP-11",
                  "feature-map uses ambiguous non-standard status values");
    gate.require (observability_runner.find ("PENDING") == std::string::npos,
                  "E2E-CP-11",
                  "runner contradicts the feature-map with a PENDING status");

    /* E2E-CP-04 — each PubSub client scenario owns its bounded evidence oracle. */
    gate.require (pubsub_client_support.find ("/evidence/wait") != std::string::npos,
                  "E2E-CP-04",
                  "PubSub client support cannot perform a bounded subscriber evidence wait");
    bool every_pubsub_scenario_checks_evidence = true;
    for (const auto &scenario : pubsub_client_scenarios) {
        every_pubsub_scenario_checks_evidence =
          every_pubsub_scenario_checks_evidence
          && scenario.find ("wait_for_subscriber_evidence") != std::string::npos;
    }
    gate.require (every_pubsub_scenario_checks_evidence,
                  "E2E-CP-04",
                  "a PubSub client scenario prints PASS without checking subscriber evidence");

    /* E2E-CP-17 — SM-F5 closes the target Spot before proving channel independence. */
    gate.require (spot_service_f5.find (".base_url (play_b_http_endpoint)")
                    != std::string::npos
                    && spot_service_f5.find ("/spot/close") != std::string::npos,
                  "E2E-CP-17",
                  "SM-F5 does not close the target Spot through its owning node");
    gate.require (spot_service_f5.find ("closed spot route unexpectedly succeeded")
                    != std::string::npos,
                  "E2E-CP-17",
                  "SM-F5 does not require the closed Spot path to fail");
    gate.require (spot_service_f5.find ("channel-after-close-f5") != std::string::npos,
                  "E2E-CP-17",
                  "SM-F5 does not retry ordinary channel messaging after Spot close");

    /* E2E-CP-46 — PS-A1 uses observed warm-up and a shared ordered sequence. */
    gate.require (pubsub_fanout_scenario.find ("sleep_for (std::chrono::milliseconds (500))")
                    == std::string::npos
                    && pubsub_fanout_scenario.find ("try_wait_for_subscriber_evidence")
                         != std::string::npos,
                  "E2E-CP-46",
                  "PS-A1 still uses a fixed sleep instead of an observed warm-up barrier");
    gate.require (pubsub_fanout_scenario.find ("common_contiguous_sequence")
                    != std::string::npos,
                  "E2E-CP-46",
                  "PS-A1 still requires lossless delivery instead of a common sequence");
    gate.require (pubsub_client_support.find ("common_contiguous_sequence")
                    != std::string::npos,
                  "E2E-CP-46",
                  "PubSub client support cannot verify shared delivery order");

    /* IMP-CP-30 — application reliability policy is not a framework hook. */
    gate.require (zlink_builder_hpp.find ("on_retry") == std::string::npos
                    && zlink_builder_hpp.find ("on_dead_letter") == std::string::npos,
                  "IMP-CP-30",
                  "zlink builder still exposes C++-only reliability hooks");
    gate.require (channel_hpp.find ("channel_reliability_event_t") == std::string::npos
                    && channel_hpp.find ("retry_hook_t") == std::string::npos
                    && channel_hpp.find ("dead_letter_hook_t") == std::string::npos,
                  "IMP-CP-30",
                  "channel contract still exposes C++-only reliability event types");

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

    /* E2E-CP-49 — ST-E2 fails transfer before commit and preserves the source binding. */
    const auto st_e2_begin = transfer_client.find ("void bound_session_rebind_isolation ()");
    const auto st_e2_end = transfer_client.find ("void in_flight_handoff_order ()", st_e2_begin);
    const auto st_e2 = st_e2_begin != std::string::npos && st_e2_end != std::string::npos
                         ? transfer_client.substr (st_e2_begin, st_e2_end - st_e2_begin)
                         : std::string{};
    gate.require (st_e2.find ("actor_type_fail_transfer_out") != std::string::npos
                    && st_e2.find ("ST-E2 failed transfer was accepted")
                         != std::string::npos,
                  "E2E-CP-49", "ST-E2 does not inject and reject a pre-commit transfer failure");
    gate.require (st_e2.find ("after-failed-transfer") != std::string::npos
                    && st_e2.find ("push_reply.node_rid == \"actor-a\"")
                         != std::string::npos,
                  "E2E-CP-49", "ST-E2 does not prove the source bound session still receives");
    gate.require (st_e2.find ("ST-E2 target processed bound push after failed transfer")
                    != std::string::npos,
                  "E2E-CP-49", "ST-E2 does not reject a target bound-session route after failure");

    /* E2E-CP-50 — missing required Track-F markers fail the runner. */
    gate.require (transfer_client.find ("handoff_backlog") != std::string::npos
                    && transfer_client.find ("backlog_enqueued") != std::string::npos
                    && transfer_client.find ("mapping_evicted") != std::string::npos
                    && transfer_client.find ("stale_fail_fast") != std::string::npos
                    && transfer_runner.find ("timing-dependent") == std::string::npos,
                  "E2E-CP-50",
                  "Track-F required markers are not client assertions or remain warnings");

    /* E2E-CP-51 — remote transfer exposes the commit-ack and source-cleanup
     * boundaries, and ST-B1 requires their order instead of inferring it from
     * the completed join call. */
    gate.require (actor_gateway_spot_bridge.find (
                    "emit_actor_transfer_marker (\"commit_ack\"")
                    != std::string::npos
                    && spot_runtime.find (
                         "emit_actor_transfer_marker (\"source_cleanup\"")
                         != std::string::npos,
                  "E2E-CP-51",
                  "remote transfer emits no structured commit_ack/source_cleanup boundaries");
    const auto st_b1_begin = transfer_client.find ("void remote_stateful_transfer ()");
    const auto st_b1_end =
      transfer_client.find ("void source_cleanup_failure_after_success ()", st_b1_begin);
    const auto st_b1 = st_b1_begin != std::string::npos && st_b1_end != std::string::npos
                         ? transfer_client.substr (st_b1_begin, st_b1_end - st_b1_begin)
                         : std::string{};
    gate.require (st_b1.find ("commit_ack") != std::string::npos
                    && st_b1.find ("source_cleanup") != std::string::npos,
                  "E2E-CP-51",
                  "ST-B1 does not require commit_ack and source_cleanup evidence");
    const auto st_b3_begin = transfer_client.find ("void remote_missing_adapter ()");
    const auto st_b3_end =
      transfer_client.find ("void remote_empty_state_transfer ()", st_b3_begin);
    const auto st_b3 = st_b3_begin != std::string::npos && st_b3_end != std::string::npos
                         ? transfer_client.substr (st_b3_begin, st_b3_end - st_b3_begin)
                         : std::string{};
    gate.require (st_b3.find ("commit_ack") != std::string::npos
                    && st_b3.find ("source_cleanup") != std::string::npos,
                  "E2E-CP-51",
                  "ST-B3 does not require commit_ack and source_cleanup evidence");

    /* E2E-CP-52 — named scenarios must open and observe their contract
     * boundary instead of relying on sleeps or impossible negative checks. */
    const auto st_b2_begin =
      transfer_client.find ("void source_cleanup_failure_after_success ()");
    const auto st_b2_end = transfer_client.find ("void remote_missing_adapter ()", st_b2_begin);
    const auto st_b2 = st_b2_begin != std::string::npos && st_b2_end != std::string::npos
                         ? transfer_client.substr (st_b2_begin, st_b2_end - st_b2_begin)
                         : std::string{};
    gate.require (st_b2.find ("commit_ack") != std::string::npos
                    && st_b2.find ("source_cleanup") != std::string::npos
                    && st_b2.find ("join_task") != std::string::npos,
                  "E2E-CP-52",
                  "ST-B2 does not stop the source between commit ack and source cleanup");
    const auto st_c1_begin = transfer_client.find ("void source_down_before_commit ()");
    const auto st_c1_end =
      transfer_client.find ("void source_down_after_target_commit ()", st_c1_begin);
    const auto st_c1 = st_c1_begin != std::string::npos && st_c1_end != std::string::npos
                         ? transfer_client.substr (st_c1_begin, st_c1_end - st_c1_begin)
                         : std::string{};
    gate.require (st_c1.find ("pending_admission_expired") != std::string::npos,
                  "E2E-CP-52",
                  "ST-C1 does not require pending-admission timeout cleanup evidence");
    const auto st_d2_begin = transfer_client.find ("void stale_source_release_fencing ()");
    const auto st_d2_end =
      transfer_client.find ("void bound_session_push_after_remote_transfer ()", st_d2_begin);
    const auto st_d2 = st_d2_begin != std::string::npos && st_d2_end != std::string::npos
                         ? transfer_client.substr (st_d2_begin, st_d2_end - st_d2_begin)
                         : std::string{};
    gate.require (st_d2.find ("source_cleanup") != std::string::npos
                    && st_d2.find ("before-delayed-cleanup") != std::string::npos
                    && st_d2.find ("after-delayed-cleanup") != std::string::npos,
                  "E2E-CP-52",
                  "ST-D2 does not delay, trigger, and route across stale source cleanup");
    const auto joined_failure_begin = transfer_client.find ("void joined_failure ()");
    const auto joined_failure_end =
      transfer_client.find ("void local_location_commit_timing ()", joined_failure_begin);
    const auto joined_failure =
      joined_failure_begin != std::string::npos && joined_failure_end != std::string::npos
        ? transfer_client.substr (joined_failure_begin,
                                  joined_failure_end - joined_failure_begin)
        : std::string{};
    gate.require (joined_failure.find ("after-joined-failure") != std::string::npos
                    && joined_failure.find ("probe_actor") != std::string::npos,
                  "E2E-CP-52",
                  "ST-C3 joined failure still asserts an actor packet nobody sends");
    const auto st_f5_begin = transfer_client.find ("void forwarding_mapping_eviction ()");
    const auto st_f5_end =
      transfer_client.find ("void in_flight_request_correlation_and_timeout ()", st_f5_begin);
    const auto st_f5 = st_f5_begin != std::string::npos && st_f5_end != std::string::npos
                         ? transfer_client.substr (st_f5_begin, st_f5_end - st_f5_begin)
                         : std::string{};
    gate.require (st_f5.find ("mapping_evicted") != std::string::npos
                    && st_f5.find ("forwarding_entries") != std::string::npos
                    && st_f5.find ("spot-map-chain-a-final-") != std::string::npos
                    && st_f5.find ("_nodes.c,") == std::string::npos,
                  "E2E-CP-52",
                  "ST-F5 does not use the two-node chained topology or observe mapping eviction");

    /* E2E-CP-53 — direct and bound-session packets cross the location publish
     * boundary before the join caller observes completion. */
    const auto st_f2_begin = transfer_client.find ("void direct_overtake_prevention ()");
    const auto st_f2_end =
      transfer_client.find ("void bound_session_cross_move_order ()", st_f2_begin);
    const auto st_f2 = st_f2_begin != std::string::npos && st_f2_end != std::string::npos
                         ? transfer_client.substr (st_f2_begin, st_f2_end - st_f2_begin)
                         : std::string{};
    const auto st_f2_publish = st_f2.find ("location_committed");
    const auto st_f2_follow_up = st_f2.find ("{\"ST-F2\", \"D1\"}");
    const auto st_f2_join_get = st_f2.find ("join_task.get ()");
    gate.require (st_f2_publish != std::string::npos
                    && st_f2_follow_up > st_f2_publish
                    && st_f2_follow_up < st_f2_join_get
                    && st_f2.find ("old_ref.generation + 1") != std::string::npos,
                  "E2E-CP-53",
                  "ST-F2 sends D1 only after join completion or an extra ref lookup");
    const auto st_f3_begin = st_f2_end;
    const auto st_f3_end =
      transfer_client.find ("void straggler_forward_then_fail_fast ()", st_f3_begin);
    const auto st_f3 = st_f3_begin != std::string::npos && st_f3_end != std::string::npos
                         ? transfer_client.substr (st_f3_begin, st_f3_end - st_f3_begin)
                         : std::string{};
    const auto st_f3_publish = st_f3.find ("location_committed");
    const auto st_f3_follow_up = st_f3.find ("{\"ST-F3\", \"S3\"}");
    const auto st_f3_join_get = st_f3.find ("join_task.get ()");
    gate.require (st_f3_publish != std::string::npos
                    && st_f3_follow_up > st_f3_publish
                    && st_f3_follow_up < st_f3_join_get,
                  "E2E-CP-53",
                  "ST-F3 sends S3/S4 only after the join caller observes completion");
    gate.require (actor_gateway_spot_bridge.find (".prepare = true") != std::string::npos
                    && actor_gateway_spot_bridge.find (".finalize = true") != std::string::npos
                    && spot_runtime.find ("prepare_remote_actor_to_spot") != std::string::npos
                    && spot_runtime.find ("finalize_remote_actor_to_spot") != std::string::npos
                    && st_f2.find ("assert_evidence_sequence") != std::string::npos
                    && st_f3.find ("assert_evidence_sequence") != std::string::npos,
                  "E2E-CP-53",
                  "remote transfer does not enqueue raced backlog before location publication");

    /* E2E-CP-54 — both sides of the forwarding window use the same one-way
     * send surface; an explicit stale ref is never silently re-resolved. */
    const auto st_f4_begin = transfer_client.find ("void straggler_forward_then_fail_fast ()");
    const auto st_f4_end = transfer_client.find ("void forwarding_mapping_eviction ()", st_f4_begin);
    const auto st_f4 = st_f4_begin != std::string::npos && st_f4_end != std::string::npos
                         ? transfer_client.substr (st_f4_begin, st_f4_end - st_f4_begin)
                         : std::string{};
    gate.require (st_f4.find ("send_ref") != std::string::npos
                    && st_f4.find ("{\"ST-F4\", \"G2\"}") != std::string::npos
                    && st_f4.find ("probe_ref") == std::string::npos
                    && st_f4.find ("stale_fail_fast") != std::string::npos,
                  "E2E-CP-54",
                  "ST-F4 still changes G2 from send to request or omits stale evidence");
    const auto actor_client_runtime =
      read_file (root / "framework/src/runtime/actors/actor_client.cpp");
    const auto send_begin = actor_client_runtime.find ("task_t<void> send_to_actor_erased");
    const auto send_end = actor_client_runtime.find (
      "task_t<message_t> request_to_actor_erased", send_begin);
    const auto actor_send = send_begin != std::string::npos && send_end != std::string::npos
                              ? actor_client_runtime.substr (send_begin, send_end - send_begin)
                              : std::string{};
    gate.require (actor_send.find ("stale_policy_t::location_stale") == std::string::npos
                    && actor_send.find ("retry") == std::string::npos,
                  "E2E-CP-54",
                  "explicit actor send still re-resolves and retries stale refs");

    /* E2E-CP-57 — Track-F evidence is consumed from role-server structured
     * message-flow events, never an environment-gated stderr grep. */
    gate.require (transfer_runner.find ("ZLINK_FRAMEWORK_CPP_ACTOR_HANDOFF_MARKERS")
                    == std::string::npos
                    && transfer_runner.find ("require_runtime_marker") == std::string::npos,
                  "E2E-CP-57",
                  "SpotActorTransfer runner still enables and greps stderr handoff markers");
    gate.require (spot_runtime.find ("ZLINK_FRAMEWORK_CPP_ACTOR_HANDOFF_MARKERS")
                    == std::string::npos
                    && spot_runtime.find ("emit_actor_handoff_marker") == std::string::npos,
                  "E2E-CP-57",
                  "spot runtime still emits environment-gated stderr handoff markers");
    gate.require (actor_gateway_spot_bridge.find (
                    "ZLINK_FRAMEWORK_CPP_ACTOR_HANDOFF_MARKERS") == std::string::npos
                    && actor_gateway_spot_bridge.find ("emit_backlog_enqueued_marker")
                         == std::string::npos,
                  "E2E-CP-57",
                  "actor bridge still emits environment-gated stderr handoff markers");
    const auto st_f1_begin = transfer_client.find ("void in_flight_handoff_order ()");
    const auto st_f1_end = transfer_client.find ("void direct_overtake_prevention ()", st_f1_begin);
    const auto st_f1 = st_f1_begin != std::string::npos && st_f1_end != std::string::npos
                         ? transfer_client.substr (st_f1_begin, st_f1_end - st_f1_begin)
                         : std::string{};
    gate.require (st_f1.find ("handoff_backlog") != std::string::npos
                    && st_f1.find ("backlog_enqueued") != std::string::npos
                    && st_f1.find ("location_committed") != std::string::npos,
                  "E2E-CP-57",
                  "ST-F1 does not assert structured backlog and publish-order evidence");
    gate.require (st_f2.find ("handoff_backlog") != std::string::npos
                    && st_f2.find ("backlog_enqueued") != std::string::npos,
                  "E2E-CP-57",
                  "ST-F2 does not assert structured handoff evidence");
    gate.require (transfer_client.find ("assert_request_handoff_frame")
                    != std::string::npos
                    && transfer_client.find ("handoff_request_frame")
                         != std::string::npos
                    && transfer_client.find ("backlog_request_frame")
                         != std::string::npos
                    && transfer_client.find ("handler_count == 1")
                         != std::string::npos,
                  "E2E-CP-57",
                  "ST-F6 does not compare request framing or exactly-once dispatch evidence");

    /* E2E-CP-55 — ST-D1 proves both sides of the local commit boundary. */
    const auto st_d1_local_begin = transfer_client.find ("void local_location_commit_timing ()");
    const auto st_d1_local_end =
      transfer_client.find ("void remote_location_commit_timing ()", st_d1_local_begin);
    const auto st_d1_local =
      st_d1_local_begin != std::string::npos && st_d1_local_end != std::string::npos
        ? transfer_client.substr (st_d1_local_begin, st_d1_local_end - st_d1_local_begin)
        : std::string{};
    gate.require (st_d1_local.find ("after.generation > before.generation")
                    != std::string::npos,
                  "E2E-CP-55",
                  "ST-D1 local commit does not require the published generation to advance");
    gate.require (st_d1_local.find ("{\"ST-D1\", \"during-joined-wait\"}")
                    != std::string::npos
                    && st_d1_local.find ("blocked_probe.wait_for") != std::string::npos
                    && st_d1_local.find ("probe.spot_rid == spot_rid") != std::string::npos,
                  "E2E-CP-55",
                  "ST-D1 does not observe actor packet routing across the delayed local commit");

    /* E2E-CP-15 — RC-A6 owns its startup-failure assertions in a client scenario. */
    gate.require (!registration_codec_a6.empty ()
                    && registration_codec_client.find (
                         "rc_a6_invalid_registration_scenario.hpp")
                         != std::string::npos
                    && registration_codec_client.find ("run_invalid_registration_scenario")
                         != std::string::npos,
                  "E2E-CP-15", "RC-A6 has no executable client scenario");
    gate.require (registration_codec_runner.find ("run_invalid()") == std::string::npos
                    && registration_codec_runner.find ("grep -q") == std::string::npos
                    && registration_codec_runner.find ("invalidServerExecutable")
                         != std::string::npos
                    && registration_codec_runner.find ("invalidEndpoint")
                         != std::string::npos
                    && registration_codec_runner.find ("ZLINK_CPP_E2E_INVALID_SERVER_EXE")
                         == std::string::npos,
                  "E2E-CP-15",
                  "RegistrationCodec runner still owns RC-A6 result assertions");

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

    /* E2E-CP-19 — SM-F4 executes request + one-way send from a source Spot to
     * a closed target route and proves the dispatch failure count grows. */
    gate.require (spot_service_f4.find ("/spot/to-spot/negative") != std::string::npos
                    && spot_service_f4.find ("source_spot_rid") != std::string::npos
                    && spot_service_f4.find ("target_spot_rid") != std::string::npos,
                  "E2E-CP-19", "SM-F4 does not route request and send from a source Spot");
    gate.require (spot_service_f4.find ("dispatch_failures_before") != std::string::npos
                    && spot_service_f4.find ("dispatch_failures_after") != std::string::npos,
                  "E2E-CP-19", "SM-F4 does not assert an increased failure counter");
    gate.require (spot_service_runner.find (
                    "packet=DirectSpotReq.*reason=handler_missing.*action=reply_error")
                      != std::string::npos
                    && spot_service_runner.find (
                         "packet=DirectSpotMsg.*reason=handler_missing.*action=drop")
                         != std::string::npos,
                  "E2E-CP-19", "SM-F4 does not assert both message-flow classifications");

    /* E2E-CP-30 — the explicit client selector includes every RC-A scenario. */
    gate.require (registration_codec_runner.find ("== rc-a*") == std::string::npos,
                  "E2E-CP-30", "RegistrationCodec routes RC-A scenarios through a broad glob");
    gate.require (registration_codec_runner.find ("== rc-a[1-6]") != std::string::npos,
                  "E2E-CP-30", "RegistrationCodec client selector does not name RC-A1 through A6");

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
    gate.require (pubsub_slow_scenario.find ("std::async (std::launch::async")
                    != std::string::npos,
                  "E2E-CP-47", "PS-B1 fast-subscriber waits are not concurrent");
    gate.require (pubsub_slow_scenario.find ("expected, 2000") != std::string::npos,
                  "E2E-CP-47", "PS-B1 fast-subscriber wait has no isolation bound");
    gate.require (pubsub_slow_scenario.find ("fast subscriber evidence exceeded 2500 ms")
                    != std::string::npos,
                  "E2E-CP-47", "PS-B1 cannot fail when fast delivery is head-of-line blocked");

    /* E2E-CP-32 — the outer HTTP deadline must not preempt the 1s provider handler. */
    gate.require (resilience_b2.find (".timeout (std::chrono::milliseconds (500))")
                    == std::string::npos,
                  "E2E-CP-32", "RL-B2 still times out before the slow provider can reply");
    gate.require (resilience_b2.find (".timeout (std::chrono::milliseconds (5000))")
                    != std::string::npos,
                  "E2E-CP-32", "RL-B2 has no outer deadline longer than its channel deadline");

    /* E2E-CP-33 — RL-D4 owns raw error-envelope proof; RL-D5 must not report a burst as soak. */
    gate.require (messaging_test.find ("\"errorCode\":\"handler_not_found\"")
                    != std::string::npos,
                  "E2E-CP-33", "RL-D4 has no raw camelCase errorCode assertion");
    gate.require (messaging_test.find ("\"errorMessage\":\"missing handler\"")
                    != std::string::npos,
                  "E2E-CP-33", "RL-D4 has no raw camelCase errorMessage assertion");
    gate.require (resilience_feature_map.find ("| `RL-D5` | deferred |")
                    != std::string::npos,
                  "E2E-CP-33", "RL-D5 still reports a sequential burst as implemented soak");
    gate.require (resilience_client.find ("rl-d5") == std::string::npos,
                  "E2E-CP-33", "RL-D5 fake soak remains selectable by the client");
    gate.require (resilience_runner.find ("should_run RL-D5") == std::string::npos,
                  "E2E-CP-33", "RL-D5 fake soak remains in the config runner");

    /* E2E-CP-35 — MON-A4/MON-D1 prove named transitions rather than event counts. */
    gate.require (runtime_monitoring_recorders.find ("|routes=") != std::string::npos,
                  "E2E-CP-35", "location evidence does not identify RID-to-endpoint routes");
    gate.require (runtime_monitoring_runner.find ("MON_D1_CYCLES=2") != std::string::npos,
                  "E2E-CP-35", "MON-D1 does not execute two crash/restart cycles");
    gate.require (runtime_monitoring_a4.find ("kind=Disconnected") != std::string::npos
                    && runtime_monitoring_a4.find ("kind=Connected") != std::string::npos
                    && runtime_monitoring_a4.find ("kind=ConnectionReady") != std::string::npos,
                  "E2E-CP-35", "MON-A4 does not assert the socket failover transitions");
    gate.require (runtime_monitoring_a4.find ("old_service_channel_endpoint")
                    != std::string::npos
                    && runtime_monitoring_a4.find ("new_service_channel_endpoint")
                         != std::string::npos,
                  "E2E-CP-35", "MON-A4 does not tie evidence to old and new endpoints");
    gate.require (runtime_monitoring_d1.find ("verify_down_up_cycles") != std::string::npos,
                  "E2E-CP-35", "MON-D1 does not verify each ordered down/up transition");
    gate.require (runtime_monitoring_a1.find ("wait_for_new_evidence") != std::string::npos
                    && runtime_monitoring_a1.find ("kind=PeerAdmissionChanged")
                         != std::string::npos,
                  "E2E-CP-35",
                  "MON-A1 does not verify drain and restore admission transitions");
    gate.require (runtime_monitoring_a1.find ("before_restore_topology_count")
                    == std::string::npos,
                  "E2E-CP-35", "MON-A1 still substitutes topology counts for admission events");

    /* E2E-CP-37 — store outage scenarios stop and restart Redis instead of pausing it. */
    gate.require (store_failure_client.find ("docker (\"pause\")") == std::string::npos
                    && store_failure_client.find ("docker (\"unpause\")")
                         == std::string::npos,
                  "E2E-CP-37", "StoreFailure still uses pause/unpause outage simulation");
    gate.require (store_failure_support.find ("stop_store") != std::string::npos
                    && store_failure_support.find ("restart_store") != std::string::npos
                    && store_failure_support.find ("stop -t 0") != std::string::npos,
                  "E2E-CP-37", "StoreFailure has no stop/restart process-control boundary");
    gate.require (store_failure_runner.find ("127.0.0.1:${redis_port}:6379")
                    != std::string::npos,
                  "E2E-CP-37", "Redis restart can change the published host port");

    /* IMP-CP-06 — recovery re-registers local rows before applying disconnect diff. */
    gate.require (location_auto_connect.find ("reconcile_after") != std::string::npos
                    && location_auto_connect.find ("heartbeat_interval") != std::string::npos,
                  "IMP-CP-06", "auto-connect recovery has no heartbeat defer boundary");
    gate.require (location_auto_connect.find ("republish_after_store_recovery")
                    != std::string::npos,
                  "IMP-CP-06", "auto-connect recovery does not republish local rows");
    gate.require (location_auto_connect.find (
                    "_runtime->options ().heartbeat_interval\n              + _runtime->options ().polling_interval")
                    != std::string::npos,
                  "IMP-CP-06", "recovery diff races the first provider heartbeat");
    gate.require (location_auto_connect.find ("_runtime->options ().polling_interval")
                    != std::string::npos
                    && location_auto_connect.find ("sleep_for (std::chrono::milliseconds (100))")
                         == std::string::npos,
                  "IMP-CP-06", "auto-connect still ignores the configured polling interval");

    /* E2E-CP-38 — grace is consumed and SF-B2 introduces a replacement target. */
    gate.require (location_auto_connect.find ("store_failure_started_at") != std::string::npos
                    && location_auto_connect.find ("store_failure_grace") != std::string::npos
                    && location_auto_connect.find ("retry_pending_targets")
                         != std::string::npos,
                  "E2E-CP-38", "store_failure_grace is not consumed by auto-connect");
    gate.require (store_failure_runner.find ("SF_B2_REPLACEMENT") != std::string::npos
                    && store_failure_runner.find ("API_B_REPLACEMENT") != std::string::npos,
                  "E2E-CP-38", "SF-B2 does not restart a provider on a replacement endpoint");
    gate.require (store_failure_client.find ("replacement_provider_url")
                    != std::string::npos
                    && store_failure_client.find ("SF-B2 replacement provider served before recovery")
                         != std::string::npos
                    && store_failure_client.find ("SF-B2 replacement provider was not used after recovery")
                         != std::string::npos,
                  "E2E-CP-38", "SF-B2 does not prove new outbound suppression and recovery");

    /* E2E-CP-39 — stores return raw rows; one runtime view owns the lease join. */
    gate.require (redis_hpp.find ("owner_is_live") == std::string::npos,
                  "E2E-CP-39", "Redis store still filters rows by owner lease");
    gate.require (live_location_reader.find ("list_owner_leases") != std::string::npos
                    && live_location_reader.find ("lease_expires_at") != std::string::npos
                    && live_location_reader.find ("store_now") != std::string::npos,
                  "E2E-CP-39", "framework has no centralized live-row lease join");
    gate.require (app_runtime.find ("live_location_reader_t") != std::string::npos
                    && location_auto_connect.find ("get_required<live_location_reader_t>")
                         != std::string::npos,
                  "E2E-CP-39", "runtime consumers still bypass the live-row view");

    /* E2E-CP-40 — D1/D2 drive outage traffic and inspect socket transitions. */
    gate.require (store_failure_client.find ("drive_tolerant_requests")
                    != std::string::npos
                    && store_failure_client.find ("max_success_gap") != std::string::npos
                    && store_failure_client.find ("std::async") != std::string::npos,
                  "E2E-CP-40", "D1/D2 do not drive and bound traffic across recovery");
    gate.require (store_failure_consumer.find ("add_socket_events") != std::string::npos
                    && store_failure_consumer.find ("socket_event_payload_t")
                         != std::string::npos
                    && store_failure_consumer.find ("/query/connections") != std::string::npos
                    && store_failure_consumer_endpoints.find ("query_connections_handler_t")
                         != std::string::npos,
                  "E2E-CP-40", "consumer exposes no socket transition evidence");
    gate.require (store_failure_client.find ("SF-D1 survivor connection changed")
                    != std::string::npos
                    && store_failure_client.find ("SF-D2 survivor connection changed")
                         != std::string::npos,
                  "E2E-CP-40", "D1/D2 do not reject survivor disconnect/reconnect");
    gate.require (channel_outbound_exchange.find ("client topology changed; rotate transport")
                    == std::string::npos,
                  "E2E-CP-40", "topology diff still reconnects every surviving endpoint");

    /* E2E-CP-41 — one HTTP probe maps to one framework request attempt. */
    gate.require (store_failure_consumer_endpoints.find ("request_profile_with_retry")
                    == std::string::npos
                    && store_failure_consumer_endpoints.find ("std::chrono::seconds (30)")
                         == std::string::npos
                    && store_failure_consumer_endpoints.find (
                         "sleep_for (std::chrono::milliseconds (100))")
                         == std::string::npos,
                  "E2E-CP-41", "consumer still masks routing failures with an internal retry loop");

    /* E2E-CP-42 — A2 adds, routes to, and removes a polling-discovered provider. */
    gate.require (store_failure_runner.find ("API_C") != std::string::npos
                    && store_failure_runner.find ("start_provider api-c")
                         != std::string::npos,
                  "E2E-CP-42", "SF-A2 never starts an additional provider");
    gate.require (store_failure_client.find ("SF-A2 added provider never served traffic")
                    != std::string::npos
                    && store_failure_client.find ("SF-A2 removed provider still served traffic")
                         != std::string::npos,
                  "E2E-CP-42", "SF-A2 does not prove polling add/remove routing");

    /* E2E-CP-43 — C2 uses the public drain lifecycle and proves typed removal. */
    gate.require (store_failure_consumer_endpoints.find (".draining = peer.draining")
                    != std::string::npos
                    && store_failure_client.find ("SF-C2 api-b did not publish draining=true")
                         != std::string::npos,
                  "E2E-CP-43", "SF-C2 drops or never asserts the typed draining marker");
    gate.require (store_failure_provider_lifecycle.find ("_app.drain (deadline)")
                    != std::string::npos
                    && store_failure_provider.find ("drain_handler_t") != std::string::npos
                    && store_failure_client.find ("SF-C2 drain did not complete as drained")
                         != std::string::npos,
                  "E2E-CP-43", "SF-C2 has no framework drain terminal-result proof");
    gate.require (app_runtime.find ("drain propagation bound") != std::string::npos
                    && app_runtime.find ("std::chrono::seconds (5)") != std::string::npos,
                  "E2E-CP-43", "drain removes owner rows before the polling propagation bound");
    gate.require (store_failure_location_store.find (
                    "auto &locations = framework.configure_locations ()")
                    != std::string::npos,
                  "E2E-CP-43", "Config 6 discards its configured polling interval");
    gate.require (location_auto_connect.find ("peer.draining ? 0u : peer.weight")
                    != std::string::npos,
                  "E2E-CP-43", "draining channel peers remain eligible for new requests");

    /* E2E-CP-44 — status reports the runtime heartbeat transition and real timestamps. */
    gate.require (store_location_resolvers.find ("(void) _store->list_owner_leases ()")
                    == std::string::npos
                    && store_location_resolvers.find (
                         "value.last_refresh_at = std::chrono::system_clock::now ()")
                         == std::string::npos,
                  "E2E-CP-44", "get_status still manufactures health with an inline store probe");
    gate.require (store_failure_contracts.find ("owner_lease_renewed_at_unix_ms")
                    != std::string::npos
                    && store_failure_contracts.find ("last_refresh_at_unix_ms")
                         != std::string::npos
                    && store_failure_contracts.find ("has_last_refresh_at")
                         == std::string::npos,
                  "E2E-CP-44", "StoreFailure status still collapses timestamps to a presence bit");
    gate.require (store_failure_client.find (
                    "recovered.last_refresh_at_unix_ms > outage.last_refresh_at_unix_ms")
                    != std::string::npos
                    && store_failure_client.find (
                         "recovered.owner_lease_renewed_at_unix_ms > outage.owner_lease_renewed_at_unix_ms")
                         != std::string::npos,
                  "E2E-CP-44", "SF-D3 does not prove refresh and lease timestamps advance after recovery");

    /* E2E-CP-45 — E1 delays Redis replies at the harness network boundary. */
    gate.require (store_failure_location_store.find ("delayable_location_store_t")
                    == std::string::npos
                    && store_failure_location_store.find ("sleep_for (delay)")
                         == std::string::npos,
                  "E2E-CP-45", "SF-E1 still delays an application store decorator");
    gate.require (std::filesystem::exists (
                    e2e_root / "DiscoveryRegistryHa/Support/redis_latency_proxy.py")
                    && store_failure_runner.find ("redis_latency_proxy.py")
                         != std::string::npos
                    && store_failure_runner.find ("\"redisProxyAdminUrl\"")
                         != std::string::npos
                    && store_failure_runner.find ("ZLINK_CPP_SF_REDIS_PROXY_ADMIN_URL")
                         == std::string::npos,
                  "E2E-CP-45", "SF-E1 has no harness-owned Redis latency proxy");
    gate.require (store_failure_client.find ("redis_proxy_admin_url")
                    != std::string::npos
                    && store_failure_support.find ("/delay") != std::string::npos
                    && store_failure_support.find ("/admin/store-delay")
                         == std::string::npos,
                  "E2E-CP-45", "SF-E1 does not control latency outside the application process");

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

    /* IMP-CP-32 — runtime snapshots and the pending table are not public contracts. */
    gate.require (zlink_builder_hpp.find ("channels () const") == std::string::npos
                    && zlink_builder_hpp.find ("route_channels () const")
                         == std::string::npos
                    && zlink_builder_hpp.find ("spot_nodes () const") == std::string::npos
                    && zlink_builder_hpp.find ("streams () const") == std::string::npos,
                  "IMP-CP-32", "zlink_builder still exposes runtime snapshots");
    gate.require (channel_hpp.find ("pending_count () const") == std::string::npos
                    && channel_hpp.find ("pending_limit () const") == std::string::npos,
                  "IMP-CP-32", "message_bus still exposes its pending request table");

    /* TH-CP-01 — the C++ connector helper surface has a language contract. */
    const auto connector_contract_path =
      root / "../../doc/framework/spec/stream-connector/languages/cpp/03-stream-connector.ko.md";
    const auto connector_contract = std::filesystem::exists (connector_contract_path)
                                      ? read_file (connector_contract_path)
                                      : std::string{};
    gate.require (!connector_contract.empty (), "TH-CP-01",
                  "C++ stream connector language contract is missing");
    gate.require (connector_contract.find ("expect_none") != std::string::npos
                    && connector_contract.find ("wait_for_sequence") != std::string::npos
                    && connector_contract.find ("namespace zlink::stream_connector::assertions")
                         != std::string::npos,
                  "TH-CP-01",
                  "C++ stream connector contract omits the common test helper surface");

    if (gate.failures != 0) {
        std::cerr << "target contract gate failures: " << gate.failures << '\n';
        return 1;
    }
    std::cout << "target contract gate satisfied\n";
    return 0;
}
