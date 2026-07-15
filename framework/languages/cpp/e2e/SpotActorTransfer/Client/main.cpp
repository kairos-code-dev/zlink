/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../Shared/messages.hpp"

#include <zlink/http_client.hpp>
#include <zlink/stream_connector.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace e2e = zlink::e2e::spot_actor_transfer;
namespace http = zlink::http_client;
namespace sc = zlink::stream_connector;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

std::string require_env (const char *name)
{
    auto value = env_or (name);
    if (value.empty ()) {
        throw std::runtime_error (std::string (name) + " is required");
    }
    return value;
}

void require (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

std::string unique_suffix ()
{
    static std::mt19937_64 rng (std::random_device{}());
    static std::atomic<unsigned> counter{0};
    std::ostringstream out;
    out << std::hex << rng () << '-' << counter.fetch_add (1);
    return out.str ();
}

http::client_t make_http (const std::string &base_url)
{
    return http::client_t::create ()
      .base_url (base_url)
      .timeout (std::chrono::seconds (30))
      .build ();
}

struct nodes_t
{
    http::client_t a;
    http::client_t b;
    http::client_t c;
    std::string a_stream_endpoint;
    std::string b_stream_endpoint;
};

e2e::create_spot_res_t create_spot (http::client_t &node,
                                    const std::string &spot_rid,
                                    const std::string &mode = "accept")
{
    return node.post ("/spots")
      .body (e2e::create_spot_req_t{spot_rid, mode})
      .fetch<e2e::create_spot_res_t> ();
}

e2e::actor_create_res_t create_actor (http::client_t &node,
                                      const std::string &actor_id,
                                      const std::string &actor_type,
                                      int state_version)
{
    return node.post ("/actors")
      .body (e2e::actor_create_req_t{actor_id, actor_type, state_version})
      .fetch<e2e::actor_create_res_t> ();
}

e2e::gate_release_res_t release_joined_gate (http::client_t &node, const std::string &spot_rid)
{
    return node.post ("/joined-gates/" + spot_rid + "/release")
      .fetch<e2e::gate_release_res_t> ();
}

e2e::gate_release_res_t release_transfer_gate (http::client_t &node, const std::string &actor_id)
{
    return node.post ("/transfer-gates/" + actor_id + "/release")
      .fetch<e2e::gate_release_res_t> ();
}

e2e::actor_ref_snapshot_res_t get_actor_ref (http::client_t &node, const std::string &actor_id)
{
    return node.get ("/actors/" + actor_id + "/ref").fetch<e2e::actor_ref_snapshot_res_t> ();
}

std::vector<e2e::actor_evidence_t> get_evidence (http::client_t &node)
{
    return node.get ("/evidence").fetch<std::vector<e2e::actor_evidence_t>> ();
}

void shutdown_node (http::client_t &node)
{
    (void) node.post ("/shutdown").fetch<nlohmann::json> ();
}

e2e::join_target_res_t join_actor (http::client_t &node,
                                   const std::string &actor_id,
                                   const e2e::join_target_req_t &request)
{
    return node.post ("/actors/" + actor_id + "/join")
      .body (request)
      .fetch<e2e::join_target_res_t> ();
}

e2e::probe_res_t probe_actor (http::client_t &node,
                              const std::string &actor_id,
                              const e2e::probe_req_t &request)
{
    return node.post ("/actors/" + actor_id + "/probe").body (request).fetch<e2e::probe_res_t> ();
}

e2e::actor_ref_probe_res_t probe_ref (http::client_t &node,
                                      const std::string &actor_id,
                                      const e2e::actor_ref_snapshot_res_t &actor,
                                      const e2e::probe_req_t &request,
                                      std::chrono::milliseconds timeout =
                                        std::chrono::seconds (5))
{
    return node.post ("/actors/" + actor_id + "/probe-ref")
      .body (e2e::actor_ref_probe_req_t{request.scenario, request.marker, actor.node_rid,
                                        actor.generation, static_cast<int> (timeout.count ())})
      .fetch<e2e::actor_ref_probe_res_t> ();
}

void send_ref (http::client_t &node,
               const std::string &actor_id,
               const e2e::actor_ref_snapshot_res_t &actor,
               const e2e::handoff_packet_msg_t &packet)
{
    (void) node.post ("/actors/" + actor_id + "/send-ref")
      .body (e2e::actor_ref_probe_req_t{packet.scenario, packet.marker, actor.node_rid,
                                        actor.generation, 5000})
      .fetch<nlohmann::json> ();
}

e2e::bound_push_res_t bound_push (http::client_t &node,
                                  const std::string &actor_id,
                                  const e2e::bound_push_req_t &request)
{
    return node.post ("/actors/" + actor_id + "/bound-push")
      .body (request)
      .fetch<e2e::bound_push_res_t> ();
}

bool evidence_contains (const std::vector<e2e::actor_evidence_t> &evidence,
                        const std::string &expected)
{
    return std::any_of (evidence.begin (), evidence.end (), [&] (const auto &entry) {
        return e2e::evidence_text (entry).find (expected) != std::string::npos;
    });
}

std::vector<e2e::actor_evidence_t> wait_evidence (http::client_t &node,
                                                  const std::vector<std::string> &contains_all)
{
    auto evidence = node.post ("/evidence/wait")
                      .body (e2e::evidence_wait_req_t{contains_all, 10000})
                      .fetch<std::vector<e2e::actor_evidence_t>> ();
    for (const auto &expected : contains_all) {
        require (evidence_contains (evidence, expected),
                 "Expected evidence marker was not observed: " + expected);
    }
    return evidence;
}

void require_no_contains (const std::vector<e2e::actor_evidence_t> &evidence,
                          const std::string &expected,
                          const std::string &message)
{
    require (!evidence_contains (evidence, expected), message);
}

void assert_evidence_order (http::client_t &node,
                            const std::string &actor_id,
                            const std::string &kind,
                            const std::vector<std::string> &values)
{
    std::vector<std::string> expected;
    expected.reserve (values.size ());
    for (const auto &value : values) {
        expected.push_back (actor_id + "|" + kind + "|" + value);
    }
    wait_evidence (node, expected);
    std::vector<std::string> observed;
    for (const auto &entry : get_evidence (node)) {
        if (entry.actor_id == actor_id && entry.kind == kind) {
            observed.push_back (entry.value);
        }
    }
    std::ostringstream joined;
    for (const auto &value : observed) {
        joined << value << ',';
    }
    require (observed == values,
             "Actor '" + actor_id + "' " + kind + " order mismatch: " + joined.str ());
}

void assert_evidence_sequence (http::client_t &node,
                               const std::vector<std::string> &expected)
{
    const auto evidence = wait_evidence (node, expected);
    std::size_t next = 0;
    for (const auto &entry : evidence) {
        if (next < expected.size ()
            && e2e::evidence_text (entry).find (expected[next]) != std::string::npos) {
            ++next;
        }
    }
    require (next == expected.size (), "Lifecycle evidence order mismatch.");
}

void assert_correlated_transfer_markers (
  const std::vector<http::client_t *> &nodes,
  const std::string &actor_id,
  const std::vector<std::string> &kinds)
{
    std::optional<std::string> transfer_id;
    std::set<std::string> observed;
    for (auto *node : nodes) {
        for (const auto &entry : get_evidence (*node)) {
            if (entry.scenario != "message_flow" || entry.actor_id != actor_id
                || std::find (kinds.begin (), kinds.end (), entry.kind) == kinds.end ()) {
                continue;
            }
            require (!entry.transfer_id.empty () && !entry.correlation_id.empty ()
                       && !entry.flow_id.empty (),
                     "Transfer marker has no correlation fields: " + entry.kind);
            if (!transfer_id) {
                transfer_id = entry.transfer_id;
            }
            require (entry.transfer_id == *transfer_id
                       && entry.correlation_id == *transfer_id
                       && entry.flow_id == *transfer_id,
                     "Transfer marker correlation mismatch: " + entry.kind);
            observed.insert (entry.kind);
        }
    }
    require (transfer_id.has_value () && observed.size () == kinds.size (),
             "Correlated transfer marker set is incomplete.");
}

std::vector<e2e::actor_evidence_t> forwarding_entries (http::client_t &node,
                                                        const std::string &actor_id)
{
    std::vector<e2e::actor_evidence_t> entries;
    for (const auto &entry : get_evidence (node)) {
        if (entry.scenario == "message_flow" && entry.actor_id == actor_id
            && entry.kind == "forwarding_entry") {
            entries.push_back (entry);
        }
    }
    return entries;
}

class bound_session_t
{
  public:
    bound_session_t (const std::string &endpoint,
                     const std::string &scenario,
                     const e2e::actor_ref_snapshot_res_t &actor)
    {
        sc::connector_options_t options;
        options.endpoint = endpoint;
        options.connect_timeout = std::chrono::seconds (5);
        options.request_timeout = std::chrono::seconds (10);
        options.heartbeat.enabled = false;
        options.dispatch_mode = sc::dispatch_mode_t::immediate;
        options.max_received_messages = 1024;
        _connector.emplace (sc::connector_factory_t::create (options));
        auto connected = _connector->connect ();
        require (static_cast<bool> (connected), scenario + " bound session connect failed");
        auto bound = _connector
                       ->request (e2e::bind_actor_session_req_t{
                         scenario, actor.actor_id, actor.node_rid, actor.generation})
                       .packet_name (e2e::bind_actor_session_req_t::packet_name)
                       .submit<e2e::bind_actor_session_res_t> ();
        require (static_cast<bool> (bound), scenario + " session bind failed");
        require (bound.value ().actor_id == actor.actor_id,
                 scenario + " session bind actor mismatch");
    }

    ~bound_session_t ()
    {
        if (_connector) {
            _connector->close ();
        }
    }

    // Registers interest in a marker BEFORE the triggering call, mirroring the
    // dotnet WaitBoundPushAsync-then-trigger pattern.
    std::future<e2e::bound_push_notify_t> expect_push (std::string marker)
    {
        auto promise = std::make_shared<std::promise<e2e::bound_push_notify_t>> ();
        auto future = promise->get_future ();
        _connector->wait_for<e2e::bound_push_notify_t> (e2e::bound_push_notify_t::packet_name)
          .where ([marker] (const e2e::bound_push_notify_t &notify) {
              return notify.marker == marker;
          })
          .timeout (std::chrono::seconds (10))
          .submit ([promise] (sc::result_t<e2e::bound_push_notify_t> result) {
              if (result) {
                  promise->set_value (std::move (result.value ()));
              } else {
                  promise->set_exception (std::make_exception_ptr (std::runtime_error (
                    "bound push wait failed: code="
                    + std::to_string (static_cast<int> (result.error_code ())))));
              }
          });
        return future;
    }

    void send_packet (const e2e::handoff_packet_msg_t &packet)
    {
        _connector->send (packet)
          .packet_name (e2e::handoff_packet_msg_t::packet_name)
          .submit ();
    }

    // Issues a request over the bound session stream (mirrors dotnet
    // bound.Request). The server relays it to the actor through the bound
    // session route, so the actor learns where to push replies/notifies even
    // when it lives on a different node than the session (spot-actor §9).
    e2e::bound_push_res_t bound_push (const e2e::bound_push_req_t &request)
    {
        auto reply = _connector->request (request)
                       .packet_name (e2e::bound_push_req_t::packet_name)
                       .template submit<e2e::bound_push_res_t> ();
        require (static_cast<bool> (reply),
                 "bound session push request failed: code="
                   + std::to_string (static_cast<int> (reply.error_code ())));
        return reply.value ();
    }

  private:
    std::optional<sc::connector_t> _connector;
};

struct straggler_setup_t
{
    std::string actor_id;
    e2e::actor_ref_snapshot_res_t old_ref;
};

class scenario_runner_t
{
  public:
    explicit scenario_runner_t (nodes_t &nodes) : _nodes (nodes) {}

    void run (const std::string &name)
    {
        static const std::map<std::string, void (scenario_runner_t::*) ()> scenarios = {
          {"ST-A1", &scenario_runner_t::local_accept},
          {"ST-A2", &scenario_runner_t::local_reject},
          {"ST-A3", &scenario_runner_t::local_moving_dispatch_blocked},
          {"ST-B1", &scenario_runner_t::remote_stateful_transfer},
          {"ST-B2", &scenario_runner_t::source_cleanup_failure_after_success},
          {"ST-B3", &scenario_runner_t::remote_missing_adapter},
          {"ST-B4", &scenario_runner_t::remote_empty_state_transfer},
          {"ST-C1", &scenario_runner_t::source_down_before_commit},
          {"ST-C2", &scenario_runner_t::source_down_after_target_commit},
          {"ST-C3", &scenario_runner_t::callback_failure_classification},
          {"ST-D1", &scenario_runner_t::location_commit_timing},
          {"ST-D2", &scenario_runner_t::stale_source_release_fencing},
          {"ST-E1", &scenario_runner_t::bound_session_push_after_remote_transfer},
          {"ST-E2", &scenario_runner_t::bound_session_rebind_isolation},
          {"ST-F1", &scenario_runner_t::in_flight_handoff_order},
          {"ST-F2", &scenario_runner_t::direct_overtake_prevention},
          {"ST-F3", &scenario_runner_t::bound_session_cross_move_order},
          {"ST-F4", &scenario_runner_t::straggler_forward_then_fail_fast},
          {"ST-F5", &scenario_runner_t::forwarding_mapping_eviction},
          {"ST-F6", &scenario_runner_t::in_flight_request_correlation_and_timeout},
        };
        const auto found = scenarios.find (name);
        if (found == scenarios.end ()) {
            throw std::runtime_error ("Unknown scenario '" + name + "'.");
        }
        (this->*found->second) ();
        std::cout << "operation SpotActorTransfer." << name << " passed" << std::endl;
    }

  private:
    void local_accept ()
    {
        const auto actor_id = "actor-local-ok-" + unique_suffix ();
        const auto spot_rid = "spot-local-ok-" + unique_suffix ();
        create_spot (_nodes.a, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 11);

        const auto join = join_actor (_nodes.a, actor_id, {"ST-A1", spot_rid});
        require (join.accepted, "ST-A1 join was rejected.");

        const auto probe = probe_actor (_nodes.a, actor_id, {"ST-A1", "after-joined"});
        require (probe.node_rid == "actor-a", "ST-A1 probe expected actor-a, got " + probe.node_rid);
        require (probe.spot_rid == spot_rid, "ST-A1 probe did not reach target spot.");

        assert_evidence_sequence (
          _nodes.a, {"ST-A1|" + actor_id + "|admission|spot=" + spot_rid,
                     "transfer|" + actor_id + "|leave|11",
                     "transfer|" + actor_id + "|joined|" + spot_rid + ":11",
                     "ST-A1|" + actor_id + "|location_committed|" + spot_rid,
                     "ST-A1|" + actor_id + "|success_reply|" + spot_rid});
        wait_evidence (_nodes.a,
                       {"ST-A1|" + actor_id + "|packet_handler|after-joined"});
    }

    void local_reject ()
    {
        const auto actor_id = "actor-local-reject-" + unique_suffix ();
        const auto spot_rid = "spot-local-reject-" + unique_suffix ();
        create_spot (_nodes.a, spot_rid, "reject");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 12);

        const auto join = join_actor (_nodes.a, actor_id, {"ST-A2", spot_rid, "reject"});
        require (!join.accepted, "ST-A2 join should have been rejected.");

        const auto evidence =
          wait_evidence (_nodes.a, {"ST-A2|" + actor_id + "|admission|spot=" + spot_rid});
        require_no_contains (evidence, "transfer|" + actor_id + "|joined|" + spot_rid,
                             "ST-A2 joined side effect should not exist.");
    }

    void local_moving_dispatch_blocked ()
    {
        const auto actor_id = "actor-local-moving-" + unique_suffix ();
        const auto spot_rid = "spot-local-moving-" + unique_suffix ();
        create_spot (_nodes.a, spot_rid, "delay-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 13);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-A3", spot_rid});
        });
        const auto waiting = wait_evidence (_nodes.a, {
                                                        "ST-A3|" + actor_id + "|admission|spot="
                                                          + spot_rid,
                                                        "transfer|" + actor_id + "|leave|13",
                                                        "ST-A3|" + actor_id + "|joined_wait|"
                                                          + spot_rid,
                                                      });
        require_no_contains (waiting,
                             "ST-A3|" + actor_id + "|packet_handler|during-joined-wait",
                             "ST-A3 packet should not run before on_actor_joined is released.");

        auto blocked_probe = std::async (std::launch::async, [&] {
            return probe_actor (_nodes.a, actor_id, {"ST-A3", "during-joined-wait"});
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
        require (blocked_probe.wait_for (std::chrono::seconds (0)) != std::future_status::ready,
                 "ST-A3 actor packet completed while on_actor_joined was still blocked.");

        const auto release = release_joined_gate (_nodes.a, spot_rid);
        require (release.released, "ST-A3 joined gate was already released.");

        const auto join = join_task.get ();
        require (join.accepted, "ST-A3 join was rejected.");
        const auto probe = blocked_probe.get ();
        require (probe.spot_rid == spot_rid,
                 "ST-A3 blocked packet did not resume on the target spot.");

        wait_evidence (_nodes.a, {
                                   "ST-A3|" + actor_id + "|joined_released|" + spot_rid,
                                   "transfer|" + actor_id + "|joined|" + spot_rid + ":13",
                                   "ST-A3|" + actor_id + "|packet_handler|during-joined-wait",
                                   "ST-A3|" + actor_id + "|success_reply|" + spot_rid,
                                 });
    }

    void remote_stateful_transfer ()
    {
        const auto actor_id = "actor-remote-ok-" + unique_suffix ();
        const auto spot_rid = "spot-remote-ok-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 21);

        const auto join = join_actor (_nodes.a, actor_id, {"ST-B1", spot_rid});
        require (join.accepted, "ST-B1 join was rejected.");

        const auto probe = probe_actor (_nodes.b, actor_id, {"ST-B1", "after-transfer"});
        require (probe.node_rid == "actor-b", "ST-B1 probe expected actor-b, got " + probe.node_rid);
        require (probe.state_version == 21,
                 "ST-B1 state version expected 21, got " + std::to_string (probe.state_version));

        assert_evidence_sequence (
          _nodes.a, {"transfer|" + actor_id + "|transfer_out|21",
                     "transfer|" + actor_id + "|leave|21",
                     "message_flow|" + actor_id + "|commit_request|",
                     "message_flow|" + actor_id + "|commit_ack|",
                     "ST-B1|" + actor_id + "|success_reply|" + spot_rid});
        assert_evidence_sequence (
          _nodes.b, {"ST-B1|" + actor_id + "|admission|",
                     "transfer|" + actor_id + "|transfer_in|21",
                     "transfer|" + actor_id + "|joined|" + spot_rid + ":21",
                     "message_flow|" + actor_id + "|location_committed|",
                     "ST-B1|" + actor_id + "|packet_handler|after-transfer"});
        wait_evidence (_nodes.a,
                       {"message_flow|" + actor_id + "|source_cleanup|"});

        assert_correlated_transfer_markers (
          {&_nodes.a, &_nodes.b}, actor_id,
          {"commit_request", "location_committed", "commit_ack", "source_cleanup"});
    }

    void source_cleanup_failure_after_success ()
    {
        const auto actor_id = "actor-cleanup-after-success-" + unique_suffix ();
        const auto spot_rid = "spot-cleanup-after-success-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 22);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-B2", spot_rid});
        });
        wait_evidence (_nodes.a,
                       {"message_flow|" + actor_id + "|commit_ack|"});
        const auto join = join_task.get ();
        require (join.accepted, "ST-B2 join was rejected.");
        require_no_contains (get_evidence (_nodes.a),
                             "message_flow|" + actor_id + "|source_cleanup|",
                             "ST-B2 source cleanup completed before source shutdown.");
        const auto before = probe_actor (_nodes.b, actor_id, {"ST-B2", "before-source-cleanup-loss"});
        require (before.node_rid == "actor-b",
                 "ST-B2 probe expected actor-b, got " + before.node_rid);

        shutdown_node (_nodes.a);
        const auto after = probe_actor (_nodes.b, actor_id, {"ST-B2", "after-source-cleanup-loss"});
        require (after.node_rid == "actor-b",
                 "ST-B2 target ownership was lost after source shutdown: " + after.node_rid);
        require (after.state_version == 22,
                 "ST-B2 state changed after source cleanup loss: "
                   + std::to_string (after.state_version));
        wait_evidence (_nodes.b, {
                                   "transfer|" + actor_id + "|joined|" + spot_rid + ":22",
                                   "ST-B2|" + actor_id + "|packet_handler|after-source-cleanup-loss",
                                 });
    }

    void remote_missing_adapter ()
    {
        const auto actor_id = "actor-no-adapter-" + unique_suffix ();
        const auto spot_rid = "spot-no-adapter-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_no_adapter, 31);

        const auto join = join_actor (_nodes.a, actor_id, {"ST-B3", spot_rid});
        require (join.accepted, "ST-B3 join was rejected.");

        const auto probe = probe_actor (_nodes.b, actor_id, {"ST-B3", "after-default-empty-transfer"});
        require (probe.node_rid == "actor-b", "ST-B3 probe expected actor-b, got " + probe.node_rid);
        require (probe.state_version == 0,
                 "ST-B3 default empty target state expected 0, got "
                   + std::to_string (probe.state_version));
        assert_evidence_sequence (
          _nodes.a, {"transfer|" + actor_id + "|transfer_out_empty_default|no-adapter",
                     "transfer|" + actor_id + "|leave|31",
                     "message_flow|" + actor_id + "|commit_request|",
                     "message_flow|" + actor_id + "|commit_ack|",
                     "ST-B3|" + actor_id + "|success_reply|" + spot_rid});
        assert_evidence_sequence (
          _nodes.b, {"ST-B3|" + actor_id + "|admission|",
                     "transfer|" + actor_id + "|transfer_in_empty_default|actor-factory",
                     "transfer|" + actor_id + "|joined|" + spot_rid + ":0",
                     "message_flow|" + actor_id + "|location_committed|",
                     "ST-B3|" + actor_id + "|packet_handler|after-default-empty-transfer"});
        wait_evidence (_nodes.a,
                       {"message_flow|" + actor_id + "|source_cleanup|"});
        assert_correlated_transfer_markers (
          {&_nodes.a, &_nodes.b}, actor_id,
          {"commit_request", "location_committed", "commit_ack", "source_cleanup"});
    }

    void remote_empty_state_transfer ()
    {
        const auto actor_id = "actor-empty-state-" + unique_suffix ();
        const auto spot_rid = "spot-empty-state-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_empty_state, 41);

        const auto join = join_actor (_nodes.a, actor_id, {"ST-B4", spot_rid});
        require (join.accepted, "ST-B4 join was rejected.");

        const auto probe = probe_actor (_nodes.b, actor_id, {"ST-B4", "after-empty-state-transfer"});
        require (probe.node_rid == "actor-b", "ST-B4 probe expected actor-b, got " + probe.node_rid);
        require (probe.state_version == 41,
                 "ST-B4 loaded target state expected 41, got "
                   + std::to_string (probe.state_version));

        wait_evidence (_nodes.a, {
                                   "transfer|" + actor_id + "|transfer_out_empty|custom-adapter",
                                   "transfer|" + actor_id + "|leave|41",
                                 });
        wait_evidence (_nodes.b, {
                                   "transfer|" + actor_id + "|transfer_in_empty|custom-adapter",
                                   "transfer|" + actor_id + "|joined|" + spot_rid + ":0",
                                   "transfer|" + actor_id + "|domain_state_loaded|" + actor_id,
                                   "ST-B4|" + actor_id + "|packet_handler|after-empty-state-transfer",
                                 });
    }

    void source_down_before_commit ()
    {
        const auto actor_id = "actor-source-down-before-commit-" + unique_suffix ();
        const auto spot_rid = "spot-source-down-before-commit-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 62);

        auto join_task = std::async (std::launch::async, [&] () -> std::optional<e2e::join_target_res_t> {
            try {
                return join_actor (_nodes.a, actor_id, {"ST-C1", spot_rid});
            }
            catch (const std::exception &) {
                // Source shutdown may abort the HTTP request before the
                // endpoint can return a failure body.
                return std::nullopt;
            }
        });
        wait_evidence (_nodes.b, {"ST-C1|" + actor_id + "|admission|spot=" + spot_rid});
        wait_evidence (_nodes.a, {
                                   "transfer|" + actor_id + "|transfer_out|62",
                                   "ST-C1|" + actor_id + "|before_commit_gate|62",
                                 });

        shutdown_node (_nodes.a);
        if (join_task.wait_for (std::chrono::seconds (3)) == std::future_status::ready) {
            const auto response = join_task.get ();
            require (!response || !response->accepted,
                     "ST-C1 join should not be accepted after source shutdown before commit.");
        }

        const auto target_evidence = wait_evidence (
          _nodes.b, {"message_flow|" + actor_id + "|pending_admission_expired|"});
        require_no_contains (target_evidence, "transfer|" + actor_id + "|transfer_in|62",
                             "ST-C1 target should not transfer in without commit.");
        require_no_contains (target_evidence, "transfer|" + actor_id + "|joined|" + spot_rid,
                             "ST-C1 target should not join without commit.");
        require_no_contains (target_evidence, "ST-C1|" + actor_id + "|packet_handler|",
                             "ST-C1 target should not dispatch actor packets without commit.");
    }

    void source_down_after_target_commit ()
    {
        const auto actor_id = "actor-source-down-after-commit-" + unique_suffix ();
        const auto spot_rid = "spot-source-down-after-commit-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 61);
        const auto source_ref = get_actor_ref (_nodes.a, actor_id);
        bound_session_t bound (_nodes.b_stream_endpoint, "ST-C2", source_ref);
        auto before_push = bound.expect_push ("bound-before-transfer");
        const auto before_reply = bound.bound_push ({"ST-C2", "bound-before-transfer"});
        require (before_reply.node_rid == "actor-a",
                 "ST-C2 pre-transfer bound push expected actor-a, got " + before_reply.node_rid);
        before_push.get ();

        const auto join = join_actor (_nodes.a, actor_id, {"ST-C2", spot_rid});
        require (join.accepted, "ST-C2 join was rejected.");
        wait_evidence (_nodes.b, {
                                   "transfer|" + actor_id + "|transfer_in|61",
                                   "transfer|" + actor_id + "|joined|" + spot_rid + ":61",
                                 });
        const auto before_shutdown = get_actor_ref (_nodes.b, actor_id);
        require (before_shutdown.node_rid == "actor-b",
                 "ST-C2 target ref expected actor-b, got " + before_shutdown.node_rid);

        shutdown_node (_nodes.a);
        std::this_thread::sleep_for (std::chrono::seconds (2));

        const auto after_shutdown = get_actor_ref (_nodes.b, actor_id);
        require (after_shutdown.node_rid == "actor-b",
                 "ST-C2 target ref changed after source shutdown: " + after_shutdown.node_rid);
        require (after_shutdown.generation == before_shutdown.generation,
                 "ST-C2 target generation changed after source shutdown.");

        const auto probe = probe_actor (_nodes.b, actor_id, {"ST-C2", "after-source-down"});
        require (probe.node_rid == "actor-b", "ST-C2 probe expected actor-b, got " + probe.node_rid);
        require (probe.spot_rid == spot_rid,
                 "ST-C2 probe did not reach target spot after source shutdown.");
        auto pushed = bound.expect_push ("bound-after-source-down");
        const auto push_reply = bound_push (_nodes.b, actor_id, {"ST-C2", "bound-after-source-down"});
        const auto notify = pushed.get ();
        require (push_reply.node_rid == "actor-b",
                 "ST-C2 bound push reply expected actor-b, got " + push_reply.node_rid);
        require (notify.node_rid == "actor-b",
                 "ST-C2 bound push notify expected actor-b, got " + notify.node_rid);
        require (notify.marker == "bound-after-source-down",
                 "ST-C2 bound push notify marker mismatch.");
        wait_evidence (_nodes.b, {
                                   "ST-C2|" + actor_id + "|packet_handler|after-source-down",
                                   "ST-C2|" + actor_id + "|bound_push|bound-after-source-down",
                                 });
    }

    void callback_failure_classification ()
    {
        transfer_out_failure ();
        source_leave_failure ();
        transfer_in_failure ();
        joined_failure ();
    }

    void location_commit_timing ()
    {
        local_location_commit_timing ();
        remote_location_commit_timing ();
    }

    void stale_source_release_fencing ()
    {
        const auto actor_id = "actor-stale-release-" + unique_suffix ();
        const auto spot_rid = "spot-stale-release-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 81);

        const auto join = join_actor (_nodes.a, actor_id, {"ST-D2", spot_rid});
        require (join.accepted, "ST-D2 join was rejected.");
        const auto before = get_actor_ref (_nodes.b, actor_id);
        require (before.node_rid == "actor-b",
                 "ST-D2 target ref expected actor-b, got " + before.node_rid);

        (void) probe_actor (_nodes.b, actor_id, {"ST-D2", "before-delayed-cleanup"});
        wait_evidence (_nodes.a,
                       {"message_flow|" + actor_id + "|source_cleanup|"});
        const auto after = get_actor_ref (_nodes.b, actor_id);
        require (after.node_rid == "actor-b",
                 "ST-D2 target ref changed after delayed cleanup: " + after.node_rid);
        require (after.generation == before.generation,
                 "ST-D2 generation changed after delayed cleanup.");
        const auto probe = probe_actor (_nodes.b, actor_id, {"ST-D2", "after-delayed-cleanup"});
        require (probe.node_rid == "actor-b", "ST-D2 probe expected actor-b, got " + probe.node_rid);
    }

    void bound_session_push_after_remote_transfer ()
    {
        const auto actor_id = "actor-bound-session-" + unique_suffix ();
        const auto spot_rid = "spot-bound-session-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 91);
        const auto source_ref = get_actor_ref (_nodes.a, actor_id);
        bound_session_t bound (_nodes.a_stream_endpoint, "ST-E1", source_ref);
        auto before_push = bound.expect_push ("before-transfer");
        bound_push (_nodes.a, actor_id, {"ST-E1", "before-transfer"});
        before_push.get ();

        const auto join = join_actor (_nodes.a, actor_id, {"ST-E1", spot_rid});
        require (join.accepted, "ST-E1 join was rejected.");
        auto pushed = bound.expect_push ("after-remote-transfer");
        const auto push_reply = bound_push (_nodes.b, actor_id, {"ST-E1", "after-remote-transfer"});
        const auto notify = pushed.get ();
        require (push_reply.node_rid == "actor-b",
                 "ST-E1 bound push reply expected actor-b, got " + push_reply.node_rid);
        require (notify.node_rid == "actor-b",
                 "ST-E1 bound push notify expected actor-b, got " + notify.node_rid);
        require (notify.state_version == 91,
                 "ST-E1 bound push state expected 91, got "
                   + std::to_string (notify.state_version));
    }

    void bound_session_rebind_isolation ()
    {
        const auto actor_id = "actor-bound-session-rebind-" + unique_suffix ();
        const auto spot_rid = "spot-bound-session-rebind-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_fail_transfer_out, 92);
        const auto source_ref = get_actor_ref (_nodes.a, actor_id);
        bound_session_t old_session (_nodes.a_stream_endpoint, "ST-E2", source_ref);
        auto before_push = old_session.expect_push ("before-failed-transfer");
        bound_push (_nodes.a, actor_id, {"ST-E2", "before-failed-transfer"});
        before_push.get ();

        const auto join = join_actor (_nodes.a, actor_id, {"ST-E2", spot_rid});
        require (!join.accepted, "ST-E2 failed transfer was accepted.");

        auto source_push = old_session.expect_push ("after-failed-transfer");
        const auto push_reply =
          bound_push (_nodes.b, actor_id, {"ST-E2", "after-failed-transfer"});
        const auto notify = source_push.get ();
        require (push_reply.node_rid == "actor-a",
                 "ST-E2 follow-up push did not execute on the source actor.");
        require (notify.node_rid == "actor-a" && notify.marker == "after-failed-transfer",
                 "ST-E2 source bound session did not receive the follow-up notify.");

        const auto target_evidence = get_evidence (_nodes.b);
        require_no_contains (
          target_evidence, "ST-E2|" + actor_id + "|bound_push|after-failed-transfer",
          "ST-E2 target processed bound push after failed transfer");
        require_no_contains (target_evidence, "transfer|" + actor_id + "|joined|" + spot_rid,
                             "ST-E2 target actor joined after failed transfer");
    }

    void in_flight_handoff_order ()
    {
        const auto actor_id = "actor-inflight-order-" + unique_suffix ();
        const auto spot_rid = "spot-inflight-order-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid, "delay-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 101);
        const auto old_ref = get_actor_ref (_nodes.a, actor_id);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-F1", spot_rid});
        });
        wait_evidence (_nodes.b, {"ST-F1|" + actor_id + "|joined_wait|" + spot_rid});
        for (const auto *marker : {"P1", "P2", "P3"}) {
            send_ref (_nodes.a, actor_id, old_ref, {"ST-F1", marker});
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        const auto source_evidence = get_evidence (_nodes.a);
        require_no_contains (source_evidence, "ST-F1|" + actor_id + "|handoff_packet|",
                             "ST-F1 packet ran on the source node.");
        wait_evidence (_nodes.a,
                       {"message_flow|" + actor_id + "|handoff_backlog|"});

        release_joined_gate (_nodes.b, spot_rid);
        require (join_task.get ().accepted, "ST-F1 transfer was rejected.");
        wait_evidence (_nodes.b,
                       {"message_flow|" + actor_id + "|backlog_enqueued|",
                        "message_flow|" + actor_id + "|location_committed|"});
        assert_correlated_transfer_markers (
          {&_nodes.a, &_nodes.b}, actor_id,
          {"handoff_backlog", "backlog_enqueued", "location_committed"});
        assert_evidence_order (_nodes.b, actor_id, "handoff_packet", {"P1", "P2", "P3"});
    }

    void direct_overtake_prevention ()
    {
        const auto actor_id = "actor-inflight-overtake-" + unique_suffix ();
        const auto spot_rid = "spot-inflight-overtake-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid, "delay-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 102);
        const auto old_ref = get_actor_ref (_nodes.a, actor_id);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-F2", spot_rid});
        });
        wait_evidence (_nodes.b, {"ST-F2|" + actor_id + "|joined_wait|" + spot_rid});
        for (const auto *marker : {"B1", "B2"}) {
            send_ref (_nodes.a, actor_id, old_ref, {"ST-F2", marker});
        }
        wait_evidence (_nodes.a,
                       {"message_flow|" + actor_id + "|handoff_backlog|"});
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        release_joined_gate (_nodes.b, spot_rid);
        wait_evidence (_nodes.b,
                       {"message_flow|" + actor_id + "|backlog_enqueued|",
                        "message_flow|" + actor_id + "|location_committed|"});
        const auto target_ref = e2e::actor_ref_snapshot_res_t{
          actor_id, "actor-b", old_ref.generation + 1};
        send_ref (_nodes.b, actor_id, target_ref, {"ST-F2", "D1"});
        require (join_task.get ().accepted, "ST-F2 transfer was rejected.");
        assert_correlated_transfer_markers (
          {&_nodes.a, &_nodes.b}, actor_id,
          {"handoff_backlog", "backlog_enqueued", "location_committed"});
        assert_evidence_order (_nodes.b, actor_id, "handoff_packet", {"B1", "B2", "D1"});
    }

    void bound_session_cross_move_order ()
    {
        const auto actor_id = "actor-bound-order-" + unique_suffix ();
        const auto spot_rid = "spot-bound-order-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid, "delay-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 103);
        const auto old_ref = get_actor_ref (_nodes.a, actor_id);
        bound_session_t bound (_nodes.a_stream_endpoint, "ST-F3", old_ref);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-F3", spot_rid});
        });
        wait_evidence (_nodes.b, {"ST-F3|" + actor_id + "|joined_wait|" + spot_rid});
        bound.send_packet ({"ST-F3", "S1"});
        bound.send_packet ({"ST-F3", "S2"});
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        release_joined_gate (_nodes.b, spot_rid);
        wait_evidence (_nodes.b,
                       {"message_flow|" + actor_id + "|location_committed|"});
        bound.send_packet ({"ST-F3", "S3"});
        bound.send_packet ({"ST-F3", "S4"});
        require (join_task.get ().accepted, "ST-F3 transfer was rejected.");
        assert_evidence_order (_nodes.b, actor_id, "handoff_packet", {"S1", "S2", "S3", "S4"});
    }

    void straggler_forward_then_fail_fast ()
    {
        const auto setup = transfer_for_straggler ("ST-F4", 104);
        send_ref (_nodes.a, setup.actor_id, setup.old_ref, {"ST-F4", "G1"});
        wait_evidence (_nodes.a,
                       {"message_flow|" + setup.actor_id + "|straggler_forward|"});
        wait_evidence (_nodes.b, {"ST-F4|" + setup.actor_id + "|handoff_packet|G1"});

        wait_evidence (_nodes.a,
                       {"message_flow|" + setup.actor_id + "|mapping_evicted|"});
        send_ref (_nodes.a, setup.actor_id, setup.old_ref, {"ST-F4", "G2"});
        wait_evidence (_nodes.a,
                       {"message_flow|" + setup.actor_id + "|stale_fail_fast|"});
        require_no_contains (get_evidence (_nodes.b),
                             "ST-F4|" + setup.actor_id + "|handoff_packet|G2",
                             "ST-F4 stale G2 send was automatically re-resolved and delivered.");
    }

    void forwarding_mapping_eviction ()
    {
        const auto actor_id = "actor-map-chain-" + unique_suffix ();
        const auto spot_b = "spot-map-chain-b-" + unique_suffix ();
        const auto spot_c = "spot-map-chain-c-" + unique_suffix ();
        create_spot (_nodes.b, spot_b);
        create_spot (_nodes.c, spot_c);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 105);
        const auto old_ref_a = get_actor_ref (_nodes.a, actor_id);
        require (join_actor (_nodes.a, actor_id, {"ST-F5", spot_b}).accepted,
                 "ST-F5 first transfer was rejected.");
        wait_evidence (_nodes.a,
                       {"message_flow|" + actor_id + "|forwarding_entry|actor-b:" + spot_b});
        const auto old_ref_b = get_actor_ref (_nodes.b, actor_id);
        require (join_actor (_nodes.b, actor_id, {"ST-F5", spot_c}).accepted,
                 "ST-F5 chained transfer was rejected.");
        wait_evidence (_nodes.b,
                       {"message_flow|" + actor_id + "|forwarding_entry|actor-c:" + spot_c});
        require (forwarding_entries (_nodes.a, actor_id).size () == 1,
                 "ST-F5 actor-a retained more than one forwarding entry.");
        require (forwarding_entries (_nodes.b, actor_id).size () == 1,
                 "ST-F5 actor-b retained more than one forwarding entry.");

        send_ref (_nodes.a, actor_id, old_ref_a, {"ST-F5", "chain-to-final"});
        wait_evidence (_nodes.c, {"ST-F5|" + actor_id + "|handoff_packet|chain-to-final"});

        wait_evidence (_nodes.a,
                       {"message_flow|" + actor_id + "|mapping_evicted|"});
        wait_evidence (_nodes.b,
                       {"message_flow|" + actor_id + "|mapping_evicted|"});
        const auto stale = probe_ref (_nodes.a, actor_id, old_ref_a, {"ST-F5", "after-eviction"});
        require (!stale.succeeded && stale.error_kind == "ActorLocationStale",
                 "ST-F5 expected evicted mapping to fail stale, got '" + stale.error_kind + "'.");
        const auto stale_b = probe_ref (_nodes.b, actor_id, old_ref_b, {"ST-F5", "after-eviction-b"});
        require (!stale_b.succeeded && stale_b.error_kind == "ActorLocationStale",
                 "ST-F5 expected node-b mapping eviction, got '" + stale_b.error_kind + "'.");
        const auto evidence = get_evidence (_nodes.c);
        require_no_contains (evidence, "ST-F5|" + actor_id + "|packet_handler|after-eviction",
                             "ST-F5 evicted packet reached the target handler.");
    }

    void in_flight_request_correlation_and_timeout ()
    {
        in_flight_request_correlation ();
        in_flight_request_timeout ();
    }

    // §10.5: a request issued against a still-current ref while the actor is
    // moving follows the actor to its committed node, so the reply correlates
    // back to the original caller.
    void in_flight_request_correlation ()
    {
        const auto actor_id = "actor-inflight-req-" + unique_suffix ();
        const auto spot_rid = "spot-inflight-req-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid, "delay-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 106);
        const auto old_ref = get_actor_ref (_nodes.a, actor_id);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-F6", spot_rid});
        });
        wait_evidence (_nodes.b, {"ST-F6|" + actor_id + "|joined_wait|" + spot_rid});
        auto request_task = std::async (std::launch::async, [&] {
            return probe_ref (_nodes.a, actor_id, old_ref, {"ST-F6", "correlated-reply"},
                              std::chrono::seconds (5));
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (200));
        release_joined_gate (_nodes.b, spot_rid);

        require (join_task.get ().accepted, "ST-F6 correlation transfer was rejected.");
        const auto response = request_task.get ();
        require (response.succeeded && response.reply && response.reply->node_rid == "actor-b",
                 "ST-F6 reply did not correlate to the original caller: " + response.error_kind);
        require (response.reply->marker == "correlated-reply",
                 "ST-F6 correlated reply marker mismatch.");
        wait_evidence (_nodes.b, {"ST-F6|" + actor_id + "|packet_handler|correlated-reply"});
    }

    // §10.5: the caller times out before the move commits, but the preserved
    // request still reaches the target handler (late reply) once the actor is
    // admitted on the target node.
    void in_flight_request_timeout ()
    {
        const auto actor_id = "actor-inflight-req-timeout-" + unique_suffix ();
        const auto spot_rid = "spot-inflight-req-timeout-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid, "delay-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 107);
        const auto old_ref = get_actor_ref (_nodes.a, actor_id);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-F6", spot_rid});
        });
        wait_evidence (_nodes.b, {"ST-F6|" + actor_id + "|joined_wait|" + spot_rid});
        auto request_task = std::async (std::launch::async, [&] {
            return probe_ref (_nodes.a, actor_id, old_ref, {"ST-F6", "late-reply"},
                              std::chrono::milliseconds (250));
        });
        std::this_thread::sleep_for (std::chrono::milliseconds (400));
        const auto timeout = request_task.get ();
        require (!timeout.succeeded && timeout.error_kind == "TimeoutException",
                 "ST-F6 expected normal TimeoutException, got '" + timeout.error_kind + "'.");
        release_joined_gate (_nodes.b, spot_rid);
        require (join_task.get ().accepted, "ST-F6 timeout transfer was rejected.");
        wait_evidence (_nodes.b, {"ST-F6|" + actor_id + "|packet_handler|late-reply",
                                  "ST-F6|" + actor_id + "|late_reply_created|late-reply"});
    }

    straggler_setup_t transfer_for_straggler (const std::string &scenario, int state_version)
    {
        const auto actor_id = "actor-straggler-" + scenario + "-" + unique_suffix ();
        const auto spot_rid = "spot-straggler-" + scenario + "-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, state_version);
        const auto old_ref = get_actor_ref (_nodes.a, actor_id);
        require (join_actor (_nodes.a, actor_id, {scenario, spot_rid}).accepted,
                 scenario + " transfer was rejected.");
        return {actor_id, old_ref};
    }

    void transfer_out_failure ()
    {
        const auto actor_id = "actor-fail-transfer-out-" + unique_suffix ();
        const auto spot_rid = "spot-fail-transfer-out-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_fail_transfer_out, 71);

        const auto response = join_actor (_nodes.a, actor_id, {"ST-C3", spot_rid});
        require (!response.accepted, "ST-C3 transfer-out failure should not return accepted.");
        const auto source_evidence = wait_evidence (_nodes.a, {
                                                                "ST-C3|" + actor_id
                                                                  + "|transfer_out_failed|71",
                                                                "ST-C3|" + actor_id
                                                                  + "|join_failed|",
                                                              });
        require_no_contains (source_evidence, "transfer|" + actor_id + "|leave|71",
                             "ST-C3 transfer-out failure should not leave source.");
        const auto target_evidence = get_evidence (_nodes.b);
        require_no_contains (target_evidence, "transfer|" + actor_id + "|joined|" + spot_rid,
                             "ST-C3 transfer-out failure should not join target.");
    }

    void source_leave_failure ()
    {
        const auto actor_id = "actor-fail-leave-" + unique_suffix ();
        const auto spot_rid = "spot-fail-leave-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_fail_leave, 72);

        const auto response = join_actor (_nodes.a, actor_id, {"ST-C3", spot_rid});
        require (!response.accepted, "ST-C3 source leave failure should not return accepted.");
        wait_evidence (_nodes.a, {
                                   "transfer|" + actor_id + "|transfer_out|72",
                                   "ST-C3|" + actor_id + "|leave_failed|72",
                                   "ST-C3|" + actor_id + "|join_failed|",
                                 });
        const auto target_evidence = get_evidence (_nodes.b);
        require_no_contains (target_evidence, "transfer|" + actor_id + "|transfer_in|72",
                             "ST-C3 source leave failure should not transfer in target.");
        require_no_contains (target_evidence, "transfer|" + actor_id + "|joined|" + spot_rid,
                             "ST-C3 source leave failure should not join target.");
    }

    void transfer_in_failure ()
    {
        const auto actor_id = "actor-fail-transfer-in-" + unique_suffix ();
        const auto spot_rid = "spot-fail-transfer-in-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid);
        create_actor (_nodes.a, actor_id, e2e::actor_type_fail_transfer_in, 73);

        const auto response = join_actor (_nodes.a, actor_id, {"ST-C3", spot_rid});
        require (!response.accepted, "ST-C3 transfer-in failure should not return accepted.");
        wait_evidence (_nodes.b, {"ST-C3|" + actor_id + "|transfer_in_failed|73"});
        wait_evidence (_nodes.a, {
                                   "transfer|" + actor_id + "|transfer_out|73",
                                   "transfer|" + actor_id + "|leave|73",
                                   "ST-C3|" + actor_id + "|join_failed|",
                                 });
        const auto target_evidence = get_evidence (_nodes.b);
        require_no_contains (target_evidence, "transfer|" + actor_id + "|joined|" + spot_rid,
                             "ST-C3 transfer-in failure should not join target.");
    }

    void joined_failure ()
    {
        const auto actor_id = "actor-fail-joined-" + unique_suffix ();
        const auto spot_rid = "spot-fail-joined-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid, "fail-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 74);

        const auto response = join_actor (_nodes.a, actor_id, {"ST-C3", spot_rid});
        require (!response.accepted, "ST-C3 joined failure should not return accepted.");
        wait_evidence (_nodes.b, {"ST-C3|" + actor_id + "|joined_failed|" + spot_rid});
        wait_evidence (_nodes.a, {
                                   "transfer|" + actor_id + "|transfer_out|74",
                                   "transfer|" + actor_id + "|leave|74",
                                   "ST-C3|" + actor_id + "|join_failed|",
                                 });
        bool packet_failed = false;
        try {
            (void) probe_actor (_nodes.b, actor_id,
                                {"ST-C3", "after-joined-failure"});
        }
        catch (const std::exception &) {
            packet_failed = true;
        }
        require (packet_failed,
                 "ST-C3 actor packet succeeded after the joined callback failed.");
        const auto target_evidence = get_evidence (_nodes.b);
        require_no_contains (target_evidence,
                             "ST-C3|" + actor_id + "|packet_handler|after-joined-failure",
                             "ST-C3 joined failure should not dispatch as joined.");
    }

    void local_location_commit_timing ()
    {
        const auto actor_id = "actor-location-local-" + unique_suffix ();
        const auto spot_rid = "spot-location-local-" + unique_suffix ();
        create_spot (_nodes.a, spot_rid, "delay-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 51);
        const auto before = get_actor_ref (_nodes.a, actor_id);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-D1", spot_rid});
        });
        const auto waiting = wait_evidence (_nodes.a, {
                                                        "ST-D1|" + actor_id + "|admission|spot="
                                                          + spot_rid,
                                                        "ST-D1|" + actor_id + "|joined_wait|"
                                                          + spot_rid,
                                                      });
        require_no_contains (waiting, "ST-D1|" + actor_id + "|success_reply|" + spot_rid,
                             "ST-D1 local join returned success before on_actor_joined completed.");
        const auto during = get_actor_ref (_nodes.a, actor_id);
        require (during.generation == before.generation,
                 "ST-D1 local actor generation changed before joined completed.");

        auto blocked_probe = std::async (std::launch::async, [&] {
            return probe_actor (_nodes.a, actor_id, {"ST-D1", "during-joined-wait"});
        });
        require (blocked_probe.wait_for (std::chrono::milliseconds (500))
                   == std::future_status::timeout,
                 "ST-D1 actor packet completed before the local location commit.");
        require_no_contains (get_evidence (_nodes.a),
                             "ST-D1|" + actor_id + "|packet_handler|during-joined-wait",
                             "ST-D1 actor packet reached the target before commit.");

        release_joined_gate (_nodes.a, spot_rid);
        const auto join = join_task.get ();
        require (join.accepted, "ST-D1 local join was rejected.");
        const auto after = get_actor_ref (_nodes.a, actor_id);
        require (after.generation > before.generation,
                 "ST-D1 local actor generation did not advance after commit.");
        const auto probe = blocked_probe.get ();
        require (probe.spot_rid == spot_rid,
                 "ST-D1 delayed actor packet did not resume on the committed target spot.");

        wait_evidence (_nodes.a, {
                                   "ST-D1|" + actor_id + "|joined_released|" + spot_rid,
                                   "transfer|" + actor_id + "|joined|" + spot_rid + ":51",
                                   "ST-D1|" + actor_id
                                     + "|packet_handler|during-joined-wait",
                                   "ST-D1|" + actor_id + "|success_reply|" + spot_rid,
                                 });
    }

    void remote_location_commit_timing ()
    {
        const auto actor_id = "actor-location-remote-" + unique_suffix ();
        const auto spot_rid = "spot-location-remote-" + unique_suffix ();
        create_spot (_nodes.b, spot_rid, "delay-joined");
        create_actor (_nodes.a, actor_id, e2e::actor_type_stateful, 52);

        auto join_task = std::async (std::launch::async, [&] {
            return join_actor (_nodes.a, actor_id, {"ST-D1", spot_rid});
        });
        wait_evidence (_nodes.b, {
                                   "ST-D1|" + actor_id + "|admission|spot=" + spot_rid,
                                   "ST-D1|" + actor_id + "|joined_wait|" + spot_rid,
                                 });
        const auto source_during = get_actor_ref (_nodes.a, actor_id);
        require (source_during.node_rid == "actor-a",
                 "ST-D1 remote source ref moved before target joined completed. got="
                   + source_during.node_rid);

        release_joined_gate (_nodes.b, spot_rid);
        const auto join = join_task.get ();
        require (join.accepted, "ST-D1 remote join was rejected.");
        const auto target_after = get_actor_ref (_nodes.b, actor_id);
        require (target_after.node_rid == "actor-b",
                 "ST-D1 remote target ref was not committed after joined completed. got="
                   + target_after.node_rid);

        wait_evidence (_nodes.a, {
                                   "transfer|" + actor_id + "|leave|52",
                                   "ST-D1|" + actor_id + "|success_reply|" + spot_rid,
                                 });
        wait_evidence (_nodes.b, {
                                   "ST-D1|" + actor_id + "|joined_released|" + spot_rid,
                                   "transfer|" + actor_id + "|joined|" + spot_rid + ":52",
                                 });
    }

    nodes_t &_nodes;
};

std::vector<std::string> selected_scenarios (const std::string &raw)
{
    std::vector<std::string> names;
    std::stringstream stream (raw);
    std::string token;
    while (std::getline (stream, token, ',')) {
        if (!token.empty ()) {
            names.push_back (token);
        }
    }
    return names;
}

} // namespace

int main ()
{
    try {
        nodes_t nodes{make_http (require_env ("ZLINK_CPP_E2E_NODE_A_URL")),
                      make_http (require_env ("ZLINK_CPP_E2E_NODE_B_URL")),
                      make_http (require_env ("ZLINK_CPP_E2E_NODE_C_URL")),
                      require_env ("ZLINK_CPP_E2E_NODE_A_STREAM"),
                      require_env ("ZLINK_CPP_E2E_NODE_B_STREAM")};
        scenario_runner_t runner (nodes);
        for (const auto &name : selected_scenarios (
               env_or ("ZLINK_CPP_E2E_SCENARIO", "ST-A1"))) {
            runner.run (name);
        }
        std::cout << "spot-actor-transfer e2e partial result=passed" << std::endl;
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << "spot-actor-transfer e2e failed: " << error.what () << std::endl;
        return 1;
    }
}
