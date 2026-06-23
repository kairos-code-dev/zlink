/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/spot_service_contracts.hpp"

#include <zlink/framework.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace e2e = zlink::framework::e2e::spot_service;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

class scenario_state_t
{
  public:
    explicit scenario_state_t (std::string node_rid) : node_rid (std::move (node_rid)) {}

    void record (std::string marker,
                 std::string actor_id = {},
                 std::string spot_rid = {},
                 std::string value = {})
    {
        std::lock_guard lock (_mutex);
        entries.push_back ({std::move (marker), node_rid, std::move (actor_id),
                            std::move (spot_rid), std::move (value)});
    }

    e2e::evidence_snapshot_t snapshot () const
    {
        std::lock_guard lock (_mutex);
        return {.node_rid = node_rid, .entries = entries};
    }

    std::string node_rid;

  private:
    mutable std::mutex _mutex;
    std::vector<e2e::evidence_entry_t> entries;
};

struct scenario_actor_t
{
    explicit scenario_actor_t (std::string actor_id) : actor_id (std::move (actor_id)) {}

    void set_actor_ref (const zlink::framework::actor_ref_t &value)
    {
        actor_ref = value;
        actor_id = std::string (value.actor_id ());
    }

    void set_actor_context (zlink::framework::actor_context_t value) { context = std::move (value); }

    std::string actor_id;
    std::string display_name;
    int level = 0;
    zlink::framework::actor_ref_t actor_ref;
    zlink::framework::actor_context_t context;
};

struct scenario_actor_factory_t
{
    scenario_actor_t create (std::string actor_id) const
    {
        return scenario_actor_t (std::move (actor_id));
    }
};

zlink::framework::actor_ref_t to_actor_ref (const e2e::actor_ref_dto_t &actor)
{
    return zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string (actor.node_rid), actor.actor_type,
      actor.actor_id, actor.generation);
}

e2e::actor_ref_dto_t from_actor_ref (const zlink::framework::actor_ref_t &actor)
{
    return {.node_rid = std::string (actor.node_rid ().value ()),
            .actor_type = std::string (actor.actor_type ()),
            .actor_id = std::string (actor.actor_id ()),
            .generation = actor.generation ()};
}

std::string owner_for_key (const std::string &key)
{
    if (key.rfind ("b-", 0) == 0 || key.rfind ("remote-", 0) == 0) {
        return "play-b";
    }
    return "play-a";
}

zlink::framework::spot_rid_t user_spot_rid (const std::string &key)
{
    return zlink::framework::spot_rid_t::from_string ("user:" + owner_for_key (key) + ":" + key);
}

class user_spot_t : public zlink::framework::spot_t
{
  public:
    explicit user_spot_t (scenario_state_t &state) : _state (state) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&user_spot_t::mutate> ("StateReq");
        context.handlers ().add_actor_packet<&user_spot_t::leave> ("LeaveReq");
        context.handlers ().add_actor_packet<&user_spot_t::disconnect> ("DisconnectReq");
        context.handlers ().add_actor_packet<&user_spot_t::outbound> ("OutboundReq");
        context.handlers ().add_actor_packet<&user_spot_t::type_mismatch> ("TypeMismatchReq");
        context.handlers ().add_subscribe<&user_spot_t::on_mesh_event> (e2e::mesh_topic);
    }

    void on_initialize ()
    {
        _state.record ("SpotInitialized", {}, std::string (_context.spot_rid ().value ()));
    }

    void on_closing ()
    {
        _state.record ("SpotClosing", {}, std::string (_context.spot_rid ().value ()));
    }

    zlink::framework::spot_actor_join_response_t
    on_actor_join (scenario_actor_t &actor, const zlink::framework::message_t &request_message)
    {
        const auto request = request_message.decode<e2e::join_req_t> ();
        actor.display_name = request.display_name;
        actor.level = request.level;
        _state.record ("ActorJoined", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       request.key);
        return zlink::framework::spot_actor_join_response_t::accept (
          e2e::join_res_t{std::string (_context.spot_rid ().value ()),
                          std::string (_context.node_rid ().value ()),
                          actor.actor_id,
                          actor.display_name,
                          actor.level,
                          request.tags});
    }

    void on_actor_joined (const scenario_actor_t &actor)
    {
        _state.record ("ActorJoinedCallback", actor.actor_id,
                       std::string (_context.spot_rid ().value ()));
    }

    void onLeaveActor (const scenario_actor_t &actor)
    {
        _state.record ("ActorLeft", actor.actor_id, std::string (_context.spot_rid ().value ()));
    }

    void onDisconnectActor (const scenario_actor_t &actor)
    {
        _state.record ("ActorDisconnected", actor.actor_id,
                       std::string (_context.spot_rid ().value ()));
    }

    e2e::state_res_t mutate (const scenario_actor_t &actor,
                             zlink::framework::spot_actor_request_context_t &,
                             const e2e::state_req_t &request)
    {
        if (request.op == "add") {
            _value += request.amount;
        } else if (request.op == "set") {
            _value = request.amount;
        }
        ++_sequence;
        _state.record ("StateMutated", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       std::to_string (_value));
        return {.spot_rid = std::string (_context.spot_rid ().value ()),
                .owner_node_rid = std::string (_context.node_rid ().value ()),
                .value = _value,
                .sequence = _sequence};
    }

    zlink::framework::task_t<e2e::leave_res_t>
    leave (scenario_actor_t &actor,
           zlink::framework::spot_actor_request_context_t &,
           const e2e::leave_req_t &)
    {
        auto left = co_await _context.leaveActor (actor.actor_ref, actor);
        if (!left.empty ()) {
            _state.record ("ActorLeaveRequested", actor.actor_id,
                           std::string (_context.spot_rid ().value ()));
        }
        co_return e2e::leave_res_t{!left.empty (), actor.actor_id};
    }

    e2e::disconnect_res_t disconnect (const scenario_actor_t &actor,
                                      zlink::framework::spot_actor_request_context_t &,
                                      const e2e::disconnect_req_t &request)
    {
        _state.record ("DisconnectRequested", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), request.reason);
        return {.disconnected = true, .actor_id = actor.actor_id};
    }

    zlink::framework::task_t<e2e::outbound_res_t>
    outbound (const scenario_actor_t &actor,
              zlink::framework::spot_actor_request_context_t &,
              const e2e::outbound_req_t &request)
    {
        auto reply = co_await _context.outbound ()
                       .request (e2e::api_channel, e2e::channel_echo_req_t{request.value})
                       .timeout (std::chrono::milliseconds (3000))
                       .async<e2e::channel_echo_res_t> ();
        co_await _context.outbound ()
          .send (e2e::api_channel,
                 e2e::channel_command_t{"cmd-" + actor.actor_id + "-" + request.value})
          .async ();
        co_await _context.publish (e2e::mesh_topic,
                                   e2e::mesh_event_t{"evt-" + actor.actor_id, request.value})
          .async ();
        _state.record ("SpotOutbound", actor.actor_id,
                       std::string (_context.spot_rid ().value ()), reply.value);
        co_return e2e::outbound_res_t{reply.value, true, true};
    }

    e2e::type_mismatch_res_t type_mismatch (const scenario_actor_t &actor,
                                            zlink::framework::spot_actor_request_context_t &,
                                            const e2e::type_mismatch_req_t &request)
    {
        try {
            (void) _context.manager ().get_or_create_spot (e2e::alternate_spot,
                                                           _context.spot_rid (), request);
        }
        catch (const zlink::framework::framework_exception_t &error) {
            if (error.kind () == zlink::framework::framework_error_kind_t::spot_type_mismatch) {
                const auto spot_name =
                  _context.manager ().spot_name_for (_context.spot_rid ()).value_or ("");
                _state.record ("SpotTypeMismatch", actor.actor_id,
                               std::string (_context.spot_rid ().value ()), spot_name);
                return {.rejected = true,
                        .error_kind = "spot_type_mismatch",
                        .spot_name = spot_name,
                        .value = _value};
            }
            throw;
        }
        return {.rejected = false,
                .error_kind = "none",
                .spot_name = _context.manager ().spot_name_for (_context.spot_rid ()).value_or (""),
                .value = _value};
    }

    void on_mesh_event (const e2e::mesh_event_t &event)
    {
        _state.record ("MeshEventReceived", {}, std::string (_context.spot_rid ().value ()),
                       event.event_id + ":" + event.value);
    }

  private:
    scenario_state_t &_state;
    zlink::framework::spot_context_t _context;
    int _value = 0;
    int _sequence = 0;
};

class entry_spot_t : public zlink::framework::entry_spot_t
{
  public:
    explicit entry_spot_t (scenario_state_t &state) : _state (state) {}

    void configure (zlink::framework::entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_packet<&entry_spot_t::join> ("JoinReq");
    }

    void configure (zlink::framework::spot_context_t &context)
    {
        zlink::framework::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    void onCreateActor (const scenario_actor_t &actor)
    {
        _state.record ("ActorCreated", actor.actor_id, std::string (_context.spot_rid ().value ()));
    }

    void on_actor_joined (const scenario_actor_t &actor)
    {
        _state.record ("EntryActorJoined", actor.actor_id,
                       std::string (_context.spot_rid ().value ()));
    }

    zlink::framework::task_t<e2e::join_res_t>
    join (scenario_actor_t &actor,
          zlink::framework::spot_actor_request_context_t &,
          const e2e::join_req_t &request)
    {
        const auto rid = user_spot_rid (request.key);
        _context.manager ().get_or_create_spot (e2e::user_spot, rid, request);
        _state.record ("EntryJoin", actor.actor_id, std::string (_context.spot_rid ().value ()),
                       request.key);
        auto joined = co_await actor.context.join_spot (rid, request).async<e2e::join_res_t> ();
        co_return joined.reply;
    }

  private:
    scenario_state_t &_state;
    zlink::framework::entry_spot_context_t _context;
};

class alternate_user_spot_t : public zlink::framework::spot_t
{
};

class spot_lifecycle_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<scenario_state_t, zlink::framework::spot_node_manager_t>;
    using request_type = e2e::lifecycle_req_t;
    using reply_type = e2e::lifecycle_res_t;

    spot_lifecycle_handler_t (scenario_state_t &state,
                              zlink::framework::spot_node_manager_t &spots) :
        _state (state),
        _spots (spots)
    {
    }

    e2e::lifecycle_res_t handle (const e2e::lifecycle_req_t &request,
                                 const zlink::framework::route_handler_context_t &)
    {
        const auto rid = user_spot_rid (request.key);
        const auto created = _spots.get_or_create_spot (e2e::user_spot, rid, request);
        const auto closed = _spots.close_spot (rid).result ();
        _state.record ("SpotLifecycleClosed", {}, std::string (rid.value ()),
                       closed && closed.value () ? "closed" : "not-closed");
        return {.spot_rid = std::string (created.spot_rid.value ()),
                .created = created.state == zlink::framework::spot_create_state_t::created,
                .closed = closed && closed.value ()};
    }

  private:
    scenario_state_t &_state;
    zlink::framework::spot_node_manager_t &_spots;
};

class ensure_actor_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;
    using request_type = e2e::ensure_actor_req_t;
    using reply_type = e2e::ensure_actor_res_t;

    explicit ensure_actor_handler_t (scenario_state_t &state) : _state (state) {}

    e2e::ensure_actor_res_t handle (const e2e::ensure_actor_req_t &request,
                                    const zlink::framework::route_handler_context_t &)
    {
        const auto generation = ++_generation;
        _state.record ("ActorEnsured", request.actor_id, {}, request.display_name);
        return {.actor = {.node_rid = _state.node_rid,
                          .actor_type = e2e::actor_type,
                          .actor_id = request.actor_id,
                          .generation = generation}};
    }

  private:
    scenario_state_t &_state;
    std::uint64_t _generation = 0;
};

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit evidence_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_state.snapshot ()).dump ();
        return response;
    }

  private:
    scenario_state_t &_state;
};

void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ()
      .add_json<e2e::actor_ref_dto_t> ()
      .add_json<e2e::ensure_actor_req_t> ()
      .add_json<e2e::ensure_actor_res_t> ()
      .add_json<e2e::join_req_t> ()
      .add_json<e2e::join_res_t> ()
      .add_json<e2e::state_req_t> ()
      .add_json<e2e::state_res_t> ()
      .add_json<e2e::leave_req_t> ()
      .add_json<e2e::leave_res_t> ()
      .add_json<e2e::disconnect_req_t> ()
      .add_json<e2e::disconnect_res_t> ()
      .add_json<e2e::channel_echo_req_t> ()
      .add_json<e2e::channel_echo_res_t> ()
      .add_json<e2e::channel_command_t> ()
      .add_json<e2e::mesh_event_t> ()
      .add_json<e2e::outbound_req_t> ()
      .add_json<e2e::outbound_res_t> ()
      .add_json<e2e::type_mismatch_req_t> ()
      .add_json<e2e::type_mismatch_res_t> ()
      .add_json<e2e::lifecycle_req_t> ()
      .add_json<e2e::lifecycle_res_t> ()
      .add_json<e2e::evidence_entry_t> ()
      .add_json<e2e::evidence_snapshot_t> ();
}

} // namespace

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto role = env_or ("ZLINK_CPP_E2E_ROLE", "play");
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");

    if (role == "registry") {
        const auto pub = env_or ("ZLINK_CPP_E2E_REGISTRY_PUB");
        const auto router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
        app.logging ().use_file (log_dir + "/registry.log").set_min_level (
          zlink::framework::log_level_t::debug);
        app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
            options.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (log_dir + "/registry-flow.log")
              .trace_node_id ("cpp-sm-registry");
            options.enable_registry (pub, router);
        });
        return app.run (argc, argv);
    }

    const auto node_rid = env_or ("ZLINK_CPP_E2E_NODE_RID", "play-a");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto api_peer_endpoint = env_or ("ZLINK_CPP_E2E_API_PEER_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");

    app.logging ().use_file (log_dir + "/" + node_rid + ".log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        auto state = std::make_unique<scenario_state_t> (node_rid);
        auto *state_ptr = state.get ();
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_node_id ("cpp-sm-" + node_rid);
        options.services ()
          .add_singleton<scenario_state_t> (std::move (state))
          .add_transient<ensure_actor_handler_t, scenario_state_t> ()
          .add_transient<spot_lifecycle_handler_t,
                         scenario_state_t,
                         zlink::framework::spot_node_manager_t> ();
        configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        options.add_route_mesh_channel (e2e::route_channel)
          .enable_server (route_endpoint)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_client ()
          .add_request_handler<ensure_actor_handler_t,
                               e2e::ensure_actor_req_t,
                               e2e::ensure_actor_res_t> ("EnsureActor",
                                                         &ensure_actor_handler_t::handle)
          .add_request_handler<spot_lifecycle_handler_t,
                               e2e::lifecycle_req_t,
                               e2e::lifecycle_res_t> ("LifecycleReq",
                                                       &spot_lifecycle_handler_t::handle)
          .enable_spot_route_egress (e2e::route_channel);
        auto api = options.add_client_server_channel (e2e::api_channel).enable_client ();
        if (!api_peer_endpoint.empty ()) {
            api.enable_client (api_peer_endpoint);
        }
        options.add_spot_mesh (e2e::spot_mesh)
          .use_registry_spot_resolver (e2e::route_channel)
          .add_node (node_rid)
          .enable_router (spot_router_endpoint, zlink::routing_id_t::from (node_rid))
          .enable_actor_gateway ()
          .enable_pub_sub (pubsub_endpoint, zlink::routing_id_t::from (node_rid))
          .accept_routes_from_channel (e2e::route_channel)
          .add_entry_spot<entry_spot_t> (
            [state_ptr] { return std::make_shared<entry_spot_t> (*state_ptr); })
          .add_spot<user_spot_t> (
            e2e::user_spot, [state_ptr] { return std::make_shared<user_spot_t> (*state_ptr); })
          .add_spot<alternate_user_spot_t> (e2e::alternate_spot)
          .add_actor_factory<scenario_actor_factory_t> (e2e::actor_type);
        options.http ().listen (http_endpoint).map_health ("/health").map_get<evidence_handler_t> (
          "/evidence");
    });
    return app.run (argc, argv);
}
