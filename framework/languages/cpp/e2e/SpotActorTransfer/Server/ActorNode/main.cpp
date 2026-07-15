/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../../Shared/messages.hpp"

#include <zlink/framework.hpp>
#include <zlink/locations/redis.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace e2e = zlink::e2e::spot_actor_transfer;
namespace fw = zlink::framework;

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

class evidence_store_t
{
  public:
    evidence_store_t (std::string node_rid, std::string evidence_file) :
        _node_rid (std::move (node_rid)), _evidence_file (std::move (evidence_file))
    {
    }

    const std::string &node_rid () const noexcept { return _node_rid; }

    void add (std::string scenario, std::string actor_id, std::string kind, std::string value)
    {
        e2e::actor_evidence_t entry{std::move (scenario), std::move (actor_id), std::move (kind),
                                    std::move (value), _node_rid};
        append (std::move (entry));
    }

    void add_flow (const fw::message_flow_event_t &event)
    {
        if (!event.packet_name || !event.actor_id || !event.correlation_id
            || !event.flow_id) {
            return;
        }
        static const std::set<std::string> transfer_markers{
          "commit_request", "location_committed", "commit_ack", "source_cleanup",
          "pending_admission_expired", "forwarding_entry", "mapping_evicted",
          "straggler_forward", "stale_fail_fast", "handoff_backlog",
          "backlog_enqueued"};
        if (!transfer_markers.contains (*event.packet_name)) {
            return;
        }
        auto value = *event.correlation_id;
        if (*event.packet_name == "forwarding_entry" && event.channel_name) {
            value = *event.channel_name;
            if (event.spot_rid) {
                value += ":" + *event.spot_rid;
            }
        }
        e2e::actor_evidence_t entry{"message_flow", *event.actor_id, *event.packet_name,
                                    std::move (value), _node_rid, *event.correlation_id,
                                    *event.correlation_id, *event.flow_id};
        append (std::move (entry));
    }

  private:
    void append (e2e::actor_evidence_t entry)
    {
        const auto line = e2e::evidence_text (entry);
        {
            std::lock_guard<std::mutex> lock (_mutex);
            _entries.push_back (std::move (entry));
            if (!_evidence_file.empty ()) {
                std::ofstream file (_evidence_file, std::ios::app);
                file << line << '\n';
            }
        }
        _changed.notify_all ();
    }

  public:

    std::vector<e2e::actor_evidence_t> snapshot () const
    {
        std::lock_guard<std::mutex> lock (_mutex);
        return _entries;
    }

    std::vector<e2e::actor_evidence_t> wait_until_contains_all (
      const std::vector<std::string> &expected, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock (_mutex);
        _changed.wait_for (lock, timeout, [&] { return contains_all_locked (expected); });
        return _entries;
    }

  private:
    bool contains_all_locked (const std::vector<std::string> &expected) const
    {
        for (const auto &needle : expected) {
            bool found = false;
            for (const auto &entry : _entries) {
                if (e2e::evidence_text (entry).find (needle) != std::string::npos) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }

    std::string _node_rid;
    std::string _evidence_file;
    mutable std::mutex _mutex;
    std::condition_variable _changed;
    std::vector<e2e::actor_evidence_t> _entries;
};

// Persists per-actor domain state so the empty-state transfer scenario can
// reload it on the target node (the state deliberately does not travel with
// the transfer message).
class domain_state_store_t
{
  public:
    explicit domain_state_store_t (std::string directory) : _directory (std::move (directory)) {}

    void save (const std::string &actor_id, int state_version)
    {
        std::ofstream file (path_for (actor_id), std::ios::trunc);
        file << state_version;
    }

    int load (const std::string &actor_id) const
    {
        std::ifstream file (path_for (actor_id));
        int state_version = 0;
        file >> state_version;
        return state_version;
    }

  private:
    std::string path_for (const std::string &actor_id) const
    {
        return _directory + "/domain-state-" + actor_id + ".txt";
    }

    std::string _directory;
};

// Named gates the client releases over HTTP. wait() intentionally blocks the
// calling (spot serial / adapter) thread: the scenarios use it to hold
// on_actor_joined or transfer_out open until the client observed the pending
// state.
class gate_store_t
{
  public:
    void wait (const std::string &key)
    {
        std::unique_lock<std::mutex> lock (_mutex);
        _changed.wait (lock, [&] { return _released.count (key) != 0; });
    }

    bool release (const std::string &key)
    {
        {
            std::lock_guard<std::mutex> lock (_mutex);
            if (!_released.insert (key).second) {
                return false;
            }
        }
        _changed.notify_all ();
        return true;
    }

  private:
    std::mutex _mutex;
    std::condition_variable _changed;
    std::set<std::string> _released;
};

class joined_gate_store_t : public gate_store_t
{
};

class transfer_gate_store_t : public gate_store_t
{
};

// Actor factories/adapters must be default constructible, so the e2e wires
// its singletons through file-scope pointers set once in main.
evidence_store_t *g_evidence = nullptr;
domain_state_store_t *g_domain_state = nullptr;
joined_gate_store_t *g_joined_gates = nullptr;
transfer_gate_store_t *g_transfer_gates = nullptr;
std::string g_initial_actor_node_rid;

class transfer_actor_t
{
  public:
    void set_actor_ref (const fw::actor_ref_t &actor_ref)
    {
        actor_id = std::string (actor_ref.actor_id ());
    }

    void set_actor_context (fw::actor_context_t actor_context)
    {
        context = std::move (actor_context);
    }

    std::string actor_id;
    std::string actor_type = e2e::actor_type_stateful;
    int state_version = 0;
    fw::actor_context_t context;
};

class transfer_actor_factory_t
{
  public:
    transfer_actor_t create (std::string actor_id)
    {
        if (g_evidence != nullptr && g_evidence->node_rid () == "actor-b"
            && actor_id.rfind ("actor-no-adapter-", 0) == 0) {
            g_evidence->add ("transfer", actor_id, "transfer_in_empty_default", "actor-factory");
        }
        transfer_actor_t actor;
        actor.actor_id = std::move (actor_id);
        return actor;
    }
};

class transfer_actor_adapter_t : public fw::actor_transfer_adapter_t<transfer_actor_t>
{
  public:
    fw::task_t<fw::message_t> transfer_out (const transfer_actor_t &actor) override
    {
        if (actor.actor_type == e2e::actor_type_fail_transfer_out) {
            g_evidence->add ("ST-C3", actor.actor_id, "transfer_out_failed",
                             std::to_string (actor.state_version));
            throw std::runtime_error ("injected transfer out failure");
        }
        if (actor.actor_type == e2e::actor_type_empty_state) {
            g_evidence->add ("transfer", actor.actor_id, "transfer_out_empty", "custom-adapter");
            return fw::task_t<fw::message_t> (
              fw::result_t<fw::message_t>::success (fw::message_t{}));
        }
        g_evidence->add ("transfer", actor.actor_id, "transfer_out",
                         std::to_string (actor.state_version));
        if (actor.actor_id.rfind ("actor-source-down-before-commit-", 0) == 0) {
            g_evidence->add ("ST-C1", actor.actor_id, "before_commit_gate",
                             std::to_string (actor.state_version));
            g_transfer_gates->wait (actor.actor_id);
        }
        return fw::task_t<fw::message_t> (fw::result_t<fw::message_t>::success (
          fw::message_t::from (e2e::transfer_state_dto_t{actor.actor_id, actor.state_version})));
    }

    fw::task_t<transfer_actor_t> transfer_in (std::string actor_id, fw::message_t state) override
    {
        transfer_actor_t actor;
        actor.actor_id = actor_id;
        if (state.empty ()) {
            g_evidence->add ("transfer", actor_id, "transfer_in_empty", "custom-adapter");
            actor.actor_type = e2e::actor_type_empty_state;
            return fw::task_t<transfer_actor_t> (
              fw::result_t<transfer_actor_t>::success (std::move (actor)));
        }
        const auto dto = state.decode<e2e::transfer_state_dto_t> ();
        if (actor_id.rfind ("actor-fail-transfer-in-", 0) == 0) {
            g_evidence->add ("ST-C3", actor_id, "transfer_in_failed",
                             std::to_string (dto.state_version));
            throw std::runtime_error ("injected transfer in failure");
        }
        actor.actor_type = e2e::actor_type_stateful;
        actor.state_version = dto.state_version;
        g_evidence->add ("transfer", actor_id, "transfer_in", std::to_string (actor.state_version));
        return fw::task_t<transfer_actor_t> (
          fw::result_t<transfer_actor_t>::success (std::move (actor)));
    }
};

e2e::join_target_res_t make_join_reply (const std::string &scenario,
                                        const std::string &actor_id,
                                        bool accepted,
                                        const std::string &target_spot_rid)
{
    return e2e::join_target_res_t{scenario, actor_id, accepted, std::string{}, target_spot_rid, 0,
                                  std::string{}};
}

class transfer_entry_spot_t : public fw::entry_spot_t
{
  public:
    void configure (fw::spot_context_t &context)
    {
        context.handlers ().add_actor_request<&transfer_entry_spot_t::join_target> (
          e2e::join_target_req_t::packet_name);
        context.handlers ().add_actor_request<&transfer_entry_spot_t::bound_push> (
          e2e::bound_push_req_t::packet_name);
        context.handlers ().add_actor_request<&transfer_entry_spot_t::probe> (
          e2e::probe_req_t::packet_name);
        _context = fw::entry_spot_context_t (context);
    }

    void on_create_actor (transfer_actor_t &actor, const fw::message_t &create_request)
    {
        if (!create_request.empty ()) {
            const auto request = create_request.decode<e2e::actor_create_req_t> ();
            actor.actor_type = request.actor_type;
            actor.state_version = request.state_version;
            if (actor.actor_type == e2e::actor_type_empty_state) {
                g_domain_state->save (actor.actor_id, actor.state_version);
            }
        }
        g_evidence->add ("create", actor.actor_id, "create",
                         actor.actor_type + ":" + std::to_string (actor.state_version));
    }

    fw::spot_actor_join_response_t on_actor_join (std::string_view actor_id,
                                                  const fw::message_t &request)
    {
        g_evidence->add ("local", std::string (actor_id), "admission", "actor-id-only");
        return fw::spot_actor_join_response_t::accept (request);
    }

    void on_actor_joined (transfer_actor_t &actor)
    {
        g_evidence->add ("local", actor.actor_id, "entry_joined",
                         std::to_string (actor.state_version));
    }

    void on_leave_actor (const transfer_actor_t &actor)
    {
        if (actor.actor_type == e2e::actor_type_no_adapter) {
            g_evidence->add ("transfer", actor.actor_id, "transfer_out_empty_default",
                             "no-adapter");
        }
        if (actor.actor_type == e2e::actor_type_fail_leave) {
            g_evidence->add ("ST-C3", actor.actor_id, "leave_failed",
                             std::to_string (actor.state_version));
            throw std::runtime_error ("injected source leave failure");
        }
        g_evidence->add ("transfer", actor.actor_id, "leave",
                         std::to_string (actor.state_version));
    }

    fw::task_t<e2e::join_target_res_t> join_target (const transfer_actor_t &actor,
                                                    fw::spot_actor_request_context_t &,
                                                    const e2e::join_target_req_t &request)
    {
        auto context = actor.context;
        auto joined = co_await context
                        .join_spot (fw::spot_rid_t::from_string (request.target_spot_rid), request)
                        .timeout (std::chrono::seconds (10))
                        .async<e2e::join_target_res_t> ();
        const auto *joined_accepted =
          std::get_if<fw::actor_join_accepted_t<e2e::join_target_res_t>> (&joined);
        if (joined_accepted != nullptr
            && joined_accepted->actor.node_rid ().value () == g_evidence->node_rid ()) {
            g_evidence->add (request.scenario, actor.actor_id, "location_committed",
                             request.target_spot_rid);
        }
        co_return e2e::join_target_res_t{request.scenario,
                                         actor.actor_id,
                                         joined_accepted != nullptr
                                           && joined_accepted->reply.accepted,
                                         g_evidence->node_rid (),
                                         request.target_spot_rid,
                                         actor.state_version,
                                         std::string{}};
    }

    e2e::bound_push_res_t bound_push (const transfer_actor_t &actor,
                                      fw::spot_actor_request_context_t &,
                                      const e2e::bound_push_req_t &request)
    {
        actor.context.bound_session ()
          .send (e2e::bound_push_notify_t{request.scenario, actor.actor_id,
                                          std::string (_context.spot_rid ().value ()),
                                          g_evidence->node_rid (), request.marker,
                                          actor.state_version})
          .submit ();
        g_evidence->add (request.scenario, actor.actor_id, "bound_push", request.marker);
        return e2e::bound_push_res_t{request.scenario,      actor.actor_id,
                                     std::string (_context.spot_rid ().value ()),
                                     g_evidence->node_rid (), request.marker,
                                     actor.state_version};
    }

    e2e::probe_res_t probe (const transfer_actor_t &actor,
                            fw::spot_actor_request_context_t &,
                            const e2e::probe_req_t &request)
    {
        g_evidence->add (request.scenario, actor.actor_id, "packet_handler", request.marker);
        return e2e::probe_res_t{request.scenario,
                                actor.actor_id,
                                std::string (_context.spot_rid ().value ()),
                                g_evidence->node_rid (),
                                actor.state_version,
                                request.marker};
    }

  private:
    fw::entry_spot_context_t _context;
};

class transfer_user_spot_t : public fw::spot_t
{
  public:
    void configure (fw::spot_context_t &context)
    {
        context.handlers ().add_actor_request<&transfer_user_spot_t::join_target> (
          e2e::join_target_req_t::packet_name);
        context.handlers ().add_actor_request<&transfer_user_spot_t::probe> (
          e2e::probe_req_t::packet_name);
        context.handlers ().add_actor_send<&transfer_user_spot_t::handoff_packet> (
          e2e::handoff_packet_msg_t::packet_name);
        context.handlers ().add_actor_request<&transfer_user_spot_t::bound_push> (
          e2e::bound_push_req_t::packet_name);
        _context = context;
    }

    fw::spot_create_response_t on_create (const fw::message_t &request)
    {
        if (!request.empty ()) {
            _mode = request.decode<e2e::create_spot_req_t> ().mode;
        }
        g_evidence->add ("create_spot", std::string (_context.spot_rid ().value ()), "spot_created", _mode);
        return fw::spot_create_response_t::accept ();
    }

    fw::spot_actor_join_response_t on_actor_join (std::string_view actor_id,
                                                  const fw::message_t &request)
    {
        const auto join = request.decode<e2e::join_target_req_t> ();
        const auto id = std::string (actor_id);
        {
            std::lock_guard<std::mutex> lock (_mutex);
            _join_scenarios[id] = join.scenario;
        }
        g_evidence->add (join.scenario, id, "admission",
                         "spot=" + std::string (_context.spot_rid ().value ()) + "|mode=" + _mode
                           + "|input=actor-id-only");
        if (_mode == "reject" || join.expected_mode == "reject") {
            return fw::spot_actor_join_response_t::reject (
              make_join_reply (join.scenario, id, false, std::string (_context.spot_rid ().value ())));
        }
        return fw::spot_actor_join_response_t::accept (
          make_join_reply (join.scenario, id, true, std::string (_context.spot_rid ().value ())));
    }

    void on_actor_joined (transfer_actor_t &actor)
    {
        if (_mode == "delay-joined") {
            const auto scenario = scenario_for (actor.actor_id);
            g_evidence->add (scenario, actor.actor_id, "joined_wait",
                             std::string (_context.spot_rid ().value ()));
            g_joined_gates->wait (std::string (_context.spot_rid ().value ()));
            g_evidence->add (scenario, actor.actor_id, "joined_released",
                             std::string (_context.spot_rid ().value ()));
            g_evidence->add ("transfer", actor.actor_id, "joined",
                             std::string (_context.spot_rid ().value ()) + ":"
                               + std::to_string (actor.state_version));
            return;
        }
        if (_mode == "fail-joined") {
            const auto scenario = scenario_for (actor.actor_id);
            g_evidence->add (scenario, actor.actor_id, "joined_failed",
                             std::string (_context.spot_rid ().value ()));
            throw std::runtime_error ("injected joined failure");
        }
        g_evidence->add ("transfer", actor.actor_id, "joined",
                         std::string (_context.spot_rid ().value ()) + ":"
                           + std::to_string (actor.state_version));
        if (actor.actor_type == e2e::actor_type_empty_state) {
            actor.state_version = g_domain_state->load (actor.actor_id);
            g_evidence->add ("transfer", actor.actor_id, "domain_state_loaded", actor.actor_id);
        }
    }

    void on_leave_actor (const transfer_actor_t &actor)
    {
        g_evidence->add ("transfer", actor.actor_id, "target_leave",
                         std::string (_context.spot_rid ().value ()));
    }

    fw::task_t<e2e::join_target_res_t> join_target (const transfer_actor_t &actor,
                                                    fw::spot_actor_request_context_t &,
                                                    const e2e::join_target_req_t &request)
    {
        auto context = actor.context;
        auto joined = co_await context
                        .join_spot (fw::spot_rid_t::from_string (request.target_spot_rid), request)
                        .timeout (std::chrono::seconds (10))
                        .async<e2e::join_target_res_t> ();
        const auto *joined_accepted =
          std::get_if<fw::actor_join_accepted_t<e2e::join_target_res_t>> (&joined);
        co_return e2e::join_target_res_t{request.scenario,
                                         actor.actor_id,
                                         joined_accepted != nullptr
                                           && joined_accepted->reply.accepted,
                                         g_evidence->node_rid (),
                                         request.target_spot_rid,
                                         actor.state_version,
                                         std::string{}};
    }

    e2e::probe_res_t probe (const transfer_actor_t &actor,
                            fw::spot_actor_request_context_t &,
                            const e2e::probe_req_t &request)
    {
        g_evidence->add (request.scenario, actor.actor_id, "packet_handler", request.marker);
        if (request.scenario == "ST-F6" && request.marker == "late-reply") {
            std::this_thread::sleep_for (std::chrono::seconds (1));
            g_evidence->add (request.scenario, actor.actor_id, "late_reply_created",
                             request.marker);
        }
        return e2e::probe_res_t{request.scenario,
                                actor.actor_id,
                                std::string (_context.spot_rid ().value ()),
                                g_evidence->node_rid (),
                                actor.state_version,
                                request.marker};
    }

    void handoff_packet (const transfer_actor_t &actor,
                         fw::spot_actor_send_context_t &,
                         const e2e::handoff_packet_msg_t &message)
    {
        g_evidence->add (message.scenario, actor.actor_id, "handoff_packet", message.marker);
    }

    e2e::bound_push_res_t bound_push (const transfer_actor_t &actor,
                                      fw::spot_actor_request_context_t &,
                                      const e2e::bound_push_req_t &request)
    {
        actor.context.bound_session ()
          .send (e2e::bound_push_notify_t{request.scenario, actor.actor_id,
                                          std::string (_context.spot_rid ().value ()),
                                          g_evidence->node_rid (), request.marker,
                                          actor.state_version})
          .submit ();
        g_evidence->add (request.scenario, actor.actor_id, "bound_push", request.marker);
        return e2e::bound_push_res_t{request.scenario,      actor.actor_id,
                                     std::string (_context.spot_rid ().value ()),
                                     g_evidence->node_rid (), request.marker,
                                     actor.state_version};
    }

  private:
    std::string scenario_for (const std::string &actor_id)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        const auto found = _join_scenarios.find (actor_id);
        return found != _join_scenarios.end () ? found->second : std::string ("unknown");
    }

    fw::spot_context_t _context;
    std::string _mode = "accept";
    std::mutex _mutex;
    std::map<std::string, std::string> _join_scenarios;
};

class transfer_session_t final : public fw::packet_stream_session_t
{
  public:
    using dependency_types =
      fw::dependency_list_t<fw::session_actor_manager_t, fw::actor_gateway_t,
                            fw::actor_directory_t>;

    transfer_session_t (fw::session_actor_manager_t &actors,
                        fw::actor_gateway_t &gateway,
                        fw::actor_directory_t &directory) :
        _actors (actors), _gateway (gateway), _directory (directory)
    {
    }

    fw::task_t<void> on_connected (fw::stream_t &) override { co_return; }

    fw::task_t<void> on_disconnected (fw::stream_t &) override
    {
        if (!_bound_actor_id.empty ()) {
            _gateway.unbind_session_stream (_bound_actor_id);
            _actors.unbind_session (_bound_actor_id);
        }
        co_return;
    }

    fw::task_t<void> on_error (fw::stream_t &, const fw::stream_error_t &) override { co_return; }

    fw::task_t<void> on_packet (fw::stream_t &stream,
                                const fw::stream_dispatch_context_t &dispatch,
                                const zlink::message_t &payload) override
    {
        if (dispatch.packet_name () == e2e::bind_actor_session_req_t::packet_name) {
            const auto request = payload.parse_json<e2e::bind_actor_session_req_t> ();
            auto actor_ref = co_await _directory.find (request.actor_id);
            fw::actor_ref_t resolved;
            if (actor_ref) {
                resolved = *actor_ref;
            } else if (!request.node_rid.empty () && request.generation) {
                resolved = fw::actor_ref_t (fw::node_rid_t::from_string (request.node_rid),
                                            e2e::actor_type_stateful, request.actor_id,
                                            static_cast<std::uint64_t> (*request.generation));
            } else {
                throw fw::framework_exception_t (
                  fw::framework_error_kind_t::actor_route_not_found,
                  "actor '" + request.actor_id + "' was not found");
            }
            auto bound = co_await _actors.bind_or_get (resolved).async ();
            _bound_actor_id = std::string (bound.actor_id ());
            _gateway.bind_session_stream (_bound_actor_id, stream, fw::stream_codec_t::json);
            g_evidence->add (request.scenario, request.actor_id, "session_bound", "stream");
            stream
              .reply_packet (zlink::message_t::from_json (e2e::bind_actor_session_res_t{
                request.scenario, std::string (resolved.actor_id ()),
                std::string (resolved.node_rid ().value ()),
                static_cast<std::int64_t> (resolved.generation ())}))
              .packet_name (e2e::bind_actor_session_res_t::packet_name)
              .submit ();
            co_return;
        }

        if (_bound_actor_id.empty ()) {
            throw fw::framework_exception_t (fw::framework_error_kind_t::request_protocol_error,
                                             "no actor is bound to this session");
        }
        auto actor = _actors.find (_bound_actor_id);
        if (!actor) {
            throw fw::framework_exception_t (fw::framework_error_kind_t::actor_route_not_found,
                                             "bound actor was not found");
        }
        if (dispatch.can_reply ()) {
            auto reply = co_await actor->relay_request (payload).async ();
            stream.reply_packet (reply).submit ();
            co_return;
        }
        co_await actor->relay (payload);
    }

  private:
    fw::session_actor_manager_t &_actors;
    fw::actor_gateway_t &_gateway;
    fw::actor_directory_t &_directory;
    std::string _bound_actor_id;
};

nlohmann::json parse_body (const fw::http_request_t &request)
{
    return request.body.empty () ? nlohmann::json::object ()
                                 : nlohmann::json::parse (request.body);
}

fw::http_response_t json_response (const nlohmann::json &body, int status = 200)
{
    fw::http_response_t response;
    response.status = status;
    response.body = body.dump ();
    return response;
}

std::string route_value (const fw::http_request_t &request, const char *key)
{
    const auto found = request.route_values.find (key);
    return found != request.route_values.end () ? found->second : std::string ();
}

class evidence_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<evidence_store_t>;

    explicit evidence_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    fw::http_response_t handle (const fw::http_request_t &)
    {
        return json_response (nlohmann::json (_evidence.snapshot ()));
    }

  private:
    evidence_store_t &_evidence;
};

class evidence_wait_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<evidence_store_t>;

    explicit evidence_wait_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    fw::http_response_t handle (const fw::http_request_t &request)
    {
        const auto wait = parse_body (request).get<e2e::evidence_wait_req_t> ();
        const auto timeout = std::chrono::milliseconds (
          std::clamp (wait.timeout_milliseconds, 1, 30000));
        return json_response (
          nlohmann::json (_evidence.wait_until_contains_all (wait.contains_all, timeout)));
    }

  private:
    evidence_store_t &_evidence;
};

class joined_gate_release_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<joined_gate_store_t>;

    explicit joined_gate_release_handler_t (joined_gate_store_t &gates) : _gates (gates) {}

    fw::http_response_t handle (const fw::http_request_t &request)
    {
        const auto spot_rid = route_value (request, "spotRid");
        return json_response (
          nlohmann::json (e2e::gate_release_res_t{spot_rid, _gates.release (spot_rid)}));
    }

  private:
    joined_gate_store_t &_gates;
};

class transfer_gate_release_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<transfer_gate_store_t>;

    explicit transfer_gate_release_handler_t (transfer_gate_store_t &gates) : _gates (gates) {}

    fw::http_response_t handle (const fw::http_request_t &request)
    {
        const auto actor_id = route_value (request, "actorId");
        return json_response (
          nlohmann::json (e2e::gate_release_res_t{actor_id, _gates.release (actor_id)}));
    }

  private:
    transfer_gate_store_t &_gates;
};

class create_spot_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<fw::spot_node_manager_t, evidence_store_t>;

    create_spot_handler_t (fw::spot_node_manager_t &spots, evidence_store_t &evidence) :
        _spots (spots), _evidence (evidence)
    {
    }

    fw::http_response_t handle (const fw::http_request_t &http_request)
    {
        const auto request = parse_body (http_request).get<e2e::create_spot_req_t> ();
        _spots.get_or_create_spot ("transfer-user",
                                   fw::spot_rid_t::from_string (request.spot_rid), request);
        return json_response (nlohmann::json (
          e2e::create_spot_res_t{request.spot_rid, _evidence.node_rid (), "created"}));
    }

  private:
    fw::spot_node_manager_t &_spots;
    evidence_store_t &_evidence;
};

class create_actor_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<fw::session_actor_manager_t, evidence_store_t>;

    create_actor_handler_t (fw::session_actor_manager_t &actors, evidence_store_t &evidence) :
        _actors (actors), _evidence (evidence)
    {
    }

    fw::task_t<fw::http_response_t> handle (const fw::http_request_t &http_request)
    {
        const auto request = parse_body (http_request).get<e2e::actor_create_req_t> ();
        auto actor = _actors.get_or_create (request.actor_type, request.actor_id, request);
        if (!actor) {
            throw *actor.error ();
        }
        auto bound = co_await _actors.bind_or_get (actor.value ().ref ()).async ();
        auto joined = co_await bound.context ()
                        .join_entry_spot (fw::node_rid_t::from_string (
                                           g_initial_actor_node_rid.empty ()
                                             ? _evidence.node_rid ()
                                             : g_initial_actor_node_rid),
                                          request)
                        .async ();
        const auto *joined_accepted = std::get_if<fw::actor_join_accepted_t<fw::message_t>> (&joined);
        const auto &ref =
          joined_accepted != nullptr ? joined_accepted->actor : bound.context ().actor_ref ();
        co_return json_response (nlohmann::json (e2e::actor_create_res_t{
          request.actor_id, request.actor_type, std::string (ref.node_rid ().value ()),
          static_cast<std::int64_t> (ref.generation ())}));
    }

  private:
    fw::session_actor_manager_t &_actors;
    evidence_store_t &_evidence;
};

fw::actor_ref_t require_actor_ref (fw::actor_directory_t &directory, const std::string &actor_id)
{
    auto found = directory.find (actor_id).result ();
    if (!found || !found.value ()) {
        throw fw::framework_exception_t (fw::framework_error_kind_t::actor_route_not_found,
                                         "actor '" + actor_id + "' was not found");
    }
    return *found.value ();
}

class actor_ref_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<fw::actor_directory_t>;

    explicit actor_ref_handler_t (fw::actor_directory_t &directory) : _directory (directory) {}

    fw::http_response_t handle (const fw::http_request_t &request)
    {
        const auto actor_id = route_value (request, "actorId");
        const auto ref = require_actor_ref (_directory, actor_id);
        return json_response (nlohmann::json (e2e::actor_ref_snapshot_res_t{
          actor_id, std::string (ref.node_rid ().value ()),
          static_cast<std::int64_t> (ref.generation ())}));
    }

  private:
    fw::actor_directory_t &_directory;
};

std::string error_kind_name (const fw::framework_exception_t &error)
{
    if (fw::detail::boundary_state (error) == fw::detail::boundary_error_t::timed_out) {
        return "TimeoutException";
    }
    switch (error.kind ()) {
        case fw::framework_error_kind_t::actor_location_stale:
            return "ActorLocationStale";
        case fw::framework_error_kind_t::actor_route_not_found:
            return "ActorRouteNotFound";
        default:
            return "FrameworkError:" + std::to_string (static_cast<int> (error.kind ()));
    }
}

class join_actor_handler_t
{
  public:
    using dependency_types =
      fw::dependency_list_t<fw::actor_directory_t, fw::actor_client_t, evidence_store_t>;

    join_actor_handler_t (fw::actor_directory_t &directory,
                          fw::actor_client_t &actors,
                          evidence_store_t &evidence) :
        _directory (directory), _actors (actors), _evidence (evidence)
    {
    }

    fw::task_t<fw::http_response_t> handle (const fw::http_request_t &http_request)
    {
        const auto actor_id = route_value (http_request, "actorId");
        const auto request = parse_body (http_request).get<e2e::join_target_req_t> ();
        try {
            const auto ref = require_actor_ref (_directory, actor_id);
            auto result = co_await _actors.request_to_actor (ref, request)
                            .timeout (std::chrono::seconds (10))
                            .async<e2e::join_target_res_t> ();
            _evidence.add (request.scenario, actor_id,
                           result.accepted ? "success_reply" : "reject_reply",
                           request.target_spot_rid);
            co_return json_response (nlohmann::json (result));
        }
        catch (const fw::framework_exception_t &error) {
            _evidence.add (request.scenario, actor_id, "join_failed",
                           error_kind_name (error));
            co_return json_response (nlohmann::json (e2e::join_target_res_t{
              request.scenario, actor_id, false, std::string{}, request.target_spot_rid, 0,
              error_kind_name (error)}));
        }
    }

  private:
    fw::actor_directory_t &_directory;
    fw::actor_client_t &_actors;
    evidence_store_t &_evidence;
};

class probe_actor_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<fw::actor_directory_t, fw::actor_client_t>;

    probe_actor_handler_t (fw::actor_directory_t &directory, fw::actor_client_t &actors) :
        _directory (directory), _actors (actors)
    {
    }

    fw::task_t<fw::http_response_t> handle (const fw::http_request_t &http_request)
    {
        const auto actor_id = route_value (http_request, "actorId");
        const auto request = parse_body (http_request).get<e2e::probe_req_t> ();
        const auto ref = require_actor_ref (_directory, actor_id);
        auto response = co_await _actors.request_to_actor (ref, request)
                          .timeout (std::chrono::seconds (10))
                          .async<e2e::probe_res_t> ();
        co_return json_response (nlohmann::json (response));
    }

  private:
    fw::actor_directory_t &_directory;
    fw::actor_client_t &_actors;
};

class probe_ref_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<fw::actor_client_t>;

    explicit probe_ref_handler_t (fw::actor_client_t &actors) : _actors (actors) {}

    fw::task_t<fw::http_response_t> handle (const fw::http_request_t &http_request)
    {
        const auto actor_id = route_value (http_request, "actorId");
        const auto request = parse_body (http_request).get<e2e::actor_ref_probe_req_t> ();
        try {
            const auto ref = fw::actor_ref_t (
              fw::node_rid_t::from_string (request.node_rid), e2e::actor_type_stateful, actor_id,
              static_cast<std::uint64_t> (request.generation));
            auto reply = co_await _actors
                           .request_to_actor (ref,
                                              e2e::probe_req_t{request.scenario, request.marker})
                           .timeout (std::chrono::milliseconds (request.timeout_ms))
                           .async<e2e::probe_res_t> ();
            co_return json_response (
              nlohmann::json (e2e::actor_ref_probe_res_t{true, reply, std::string{}}));
        }
        catch (const fw::framework_exception_t &error) {
            co_return json_response (nlohmann::json (e2e::actor_ref_probe_res_t{
              false, std::nullopt, error_kind_name (error)}));
        }
    }

  private:
    fw::actor_client_t &_actors;
};

class send_ref_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<fw::actor_client_t>;

    explicit send_ref_handler_t (fw::actor_client_t &actors) : _actors (actors) {}

    fw::task_t<fw::http_response_t> handle (const fw::http_request_t &http_request)
    {
        const auto actor_id = route_value (http_request, "actorId");
        const auto request = parse_body (http_request).get<e2e::actor_ref_probe_req_t> ();
        const auto ref = fw::actor_ref_t (fw::node_rid_t::from_string (request.node_rid),
                                          e2e::actor_type_stateful, actor_id,
                                          static_cast<std::uint64_t> (request.generation));
        _actors.send_to_actor (ref, e2e::handoff_packet_msg_t{request.scenario, request.marker})
          .submit ();
        co_return json_response (nlohmann::json::object ());
    }

  private:
    fw::actor_client_t &_actors;
};

class bound_push_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<fw::actor_directory_t, fw::actor_client_t>;

    bound_push_handler_t (fw::actor_directory_t &directory, fw::actor_client_t &actors) :
        _directory (directory), _actors (actors)
    {
    }

    fw::task_t<fw::http_response_t> handle (const fw::http_request_t &http_request)
    {
        const auto actor_id = route_value (http_request, "actorId");
        const auto request = parse_body (http_request).get<e2e::bound_push_req_t> ();
        const auto ref = require_actor_ref (_directory, actor_id);
        auto response = co_await _actors.request_to_actor (ref, request)
                          .timeout (std::chrono::seconds (10))
                          .async<e2e::bound_push_res_t> ();
        co_return json_response (nlohmann::json (response));
    }

  private:
    fw::actor_directory_t &_directory;
    fw::actor_client_t &_actors;
};

class shutdown_flag_t
{
  public:
    std::atomic_bool requested{false};
};

class shutdown_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<shutdown_flag_t>;

    explicit shutdown_handler_t (shutdown_flag_t &flag) : _flag (flag) {}

    fw::http_response_t handle (const fw::http_request_t &)
    {
        _flag.requested.store (true);
        return json_response (nlohmann::json{{"status", "stopping"}});
    }

  private:
    shutdown_flag_t &_flag;
};

} // namespace

int main (int argc, char **argv)
{
    const auto role = env_or ("ZLINK_CPP_E2E_ROLE", "actor");
    const auto rid = require_env ("ZLINK_CPP_E2E_ACTOR_RID");
    const auto http_url = require_env ("ZLINK_CPP_E2E_ACTOR_HTTP");
    const auto router_endpoint = require_env ("ZLINK_CPP_E2E_ACTOR_ROUTER");
    const auto stream_endpoint = require_env ("ZLINK_CPP_E2E_ACTOR_STREAM");
    const auto pub_endpoint = require_env ("ZLINK_CPP_E2E_ACTOR_PUB");
    const auto redis_endpoint = require_env ("ZLINK_CPP_E2E_REDIS_LOCATION_ENDPOINT");
    const auto key_prefix = require_env ("ZLINK_CPP_E2E_LOCATION_KEY_PREFIX");
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto evidence_file = env_or ("ZLINK_CPP_E2E_EVIDENCE_FILE",
                                       log_dir + "/" + rid + ".evidence.log");
    std::filesystem::create_directories (log_dir);

    auto evidence = std::make_unique<evidence_store_t> (rid, evidence_file);
    auto domain_state = std::make_unique<domain_state_store_t> (log_dir);
    auto joined_gates = std::make_unique<joined_gate_store_t> ();
    auto transfer_gates = std::make_unique<transfer_gate_store_t> ();
    auto shutdown_flag = std::make_unique<shutdown_flag_t> ();
    g_evidence = evidence.get ();
    g_domain_state = domain_state.get ();
    g_joined_gates = joined_gates.get ();
    g_transfer_gates = transfer_gates.get ();
    g_initial_actor_node_rid = env_or ("ZLINK_CPP_E2E_INITIAL_ACTOR_NODE", rid);
    auto *shutdown_flag_ptr = shutdown_flag.get ();

    auto app = fw::app_t::create ();
    app.logging ().use_file (log_dir + "/" + rid + ".app.log");
    app.add_zlink_framework ([&] (fw::zlink_framework_options_t &framework) {
        framework.services ().add_singleton<evidence_store_t> (std::move (evidence));
        framework.services ().add_singleton<domain_state_store_t> (std::move (domain_state));
        framework.services ().add_singleton<joined_gate_store_t> (std::move (joined_gates));
        framework.services ().add_singleton<transfer_gate_store_t> (std::move (transfer_gates));
        framework.services ().add_singleton<shutdown_flag_t> (std::move (shutdown_flag));

        framework.set_default_request_timeout (std::chrono::seconds (3));

        framework.configure_dispatch ()
          .message_flow (fw::message_flow_log_mode_t::key_transitions)
          .set_message_flow_observer (
            [] (const fw::message_flow_event_t &event) { g_evidence->add_flow (event); });

        framework.set_actor_transfer_forward_window (std::chrono::seconds (3));
        framework.add_location_store (
          std::make_shared<fw::locations::redis::redis_location_store_t> (
            fw::locations::redis::redis_location_options_t{.connection_string = redis_endpoint,
                                                           .key_prefix = key_prefix}));
        auto &locations = framework.configure_locations ();
        locations.heartbeat_interval = std::chrono::seconds (1);
        locations.owner_lease_ttl = std::chrono::seconds (3);
        locations.polling_interval = std::chrono::milliseconds (500);

        auto mesh = framework.add_spot_mesh (e2e::mesh_name);
        mesh.enable_router (router_endpoint)
          .enable_pub_sub (pub_endpoint)
          .set_routing_id (zlink::routing_id_t::from (rid));
        if (role == "actor") {
            mesh.add_entry_spot<transfer_entry_spot_t> ()
              .add_spot<transfer_user_spot_t> ("transfer-user")
              .add_actor_factory<transfer_actor_factory_t> (e2e::actor_type_stateful)
              .add_actor_transfer_adapter<transfer_actor_t, transfer_actor_adapter_t> (
                e2e::actor_type_stateful)
              .add_actor_factory<transfer_actor_factory_t> (e2e::actor_type_empty_state)
              .add_actor_transfer_adapter<transfer_actor_t, transfer_actor_adapter_t> (
                e2e::actor_type_empty_state)
              .add_actor_factory<transfer_actor_factory_t> (e2e::actor_type_no_adapter)
              .add_actor_factory<transfer_actor_factory_t> (e2e::actor_type_fail_leave)
              .add_actor_transfer_adapter<transfer_actor_t, transfer_actor_adapter_t> (
                e2e::actor_type_fail_leave)
              .add_actor_factory<transfer_actor_factory_t> (e2e::actor_type_fail_transfer_out)
              .add_actor_transfer_adapter<transfer_actor_t, transfer_actor_adapter_t> (
                e2e::actor_type_fail_transfer_out)
              .add_actor_factory<transfer_actor_factory_t> (e2e::actor_type_fail_transfer_in)
              .add_actor_transfer_adapter<transfer_actor_t, transfer_actor_adapter_t> (
                e2e::actor_type_fail_transfer_in);
        }

        if (role == "actor") {
            framework.add_stream_node (std::string (e2e::mesh_name) + "-internal-" + rid)
              .bind (stream_endpoint)
              .register_session<transfer_session_t> ();
        } else {
            framework.add_stream_node (std::string (e2e::mesh_name) + "-stream-" + rid)
              .bind (stream_endpoint)
              .register_session<transfer_session_t> ();
        }

        framework.http ().listen (http_url)
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence")
          .map_post<evidence_wait_handler_t> ("/evidence/wait");
        if (role == "actor") {
            framework.http ()
              .map_post<joined_gate_release_handler_t> ("/joined-gates/{spotRid}/release")
              .map_post<transfer_gate_release_handler_t> ("/transfer-gates/{actorId}/release")
              .map_post<create_spot_handler_t> ("/spots")
              .map_get<actor_ref_handler_t> ("/actors/{actorId}/ref")
              .map_post<probe_ref_handler_t> ("/actors/{actorId}/probe-ref")
              .map_post<send_ref_handler_t> ("/actors/{actorId}/send-ref")
              .map_post<bound_push_handler_t> ("/actors/{actorId}/bound-push")
              .map_post<shutdown_handler_t> ("/shutdown");
        } else if (role == "controller") {
            framework.http ()
              .map_post<create_actor_handler_t> ("/actors")
              .map_post<join_actor_handler_t> ("/actors/{actorId}/join")
              .map_post<probe_actor_handler_t> ("/actors/{actorId}/probe");
        }
    });

    std::thread shutdown_watcher ([&app, shutdown_flag_ptr] {
        while (!shutdown_flag_ptr->requested.load ()) {
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }
        app.request_stop ();
    });
    const int code = app.run (argc, argv);
    shutdown_flag_ptr->requested.store (true);
    shutdown_watcher.join ();
    return code;
}
