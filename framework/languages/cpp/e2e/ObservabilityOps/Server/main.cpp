/* SPDX-License-Identifier: MPL-2.0 */

/* Config 11 (ObservabilityOps) server host. One binary hosts a play role:
 * spot mesh + room spot + shared spot route channel + STREAM session gateway
 * (play-a only) + evidence HTTP endpoints. Evidence follows the common
 * config-11 §3 minimal JSON arrays: metrics / drainEvents / peerRows. */

#include "../Shared/observability_contracts.hpp"

#include <zlink/framework.hpp>
#include <zlink/locations/redis.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace obs = zlink::framework::e2e::observability_ops;
namespace fw = zlink::framework;

namespace
{

std::string env_or (const char *name, const std::string &fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

const char *metric_kind_name (fw::metric_instrument_kind_t kind)
{
    switch (kind) {
        case fw::metric_instrument_kind_t::counter:
            return "counter";
        case fw::metric_instrument_kind_t::updown:
            return "updown";
        case fw::metric_instrument_kind_t::observable:
            return "observable";
        case fw::metric_instrument_kind_t::histogram:
            return "histogram";
    }
    return "unknown";
}

const char *drain_state_name (fw::drain_state_t state)
{
    switch (state) {
        case fw::drain_state_t::serving:
            return "serving";
        case fw::drain_state_t::draining:
            return "draining";
        case fw::drain_state_t::drained:
            return "drained";
        case fw::drain_state_t::force_stopping:
            return "force_stopping";
    }
    return "unknown";
}

/* Aggregates the config-11 §3 evidence arrays. Metric samples keep the raw
 * catalog fields so counter deltas and current gauges stay distinguishable. */
class observability_evidence_t
{
  public:
    void record_metric (const fw::metric_event_payload_t &event)
    {
        std::lock_guard lock (_mutex);
        _metrics.push_back (nlohmann::json{{"name", event.name},
                                           {"kind", metric_kind_name (event.instrument_kind)},
                                           {"value", event.value},
                                           {"unit", event.unit},
                                           {"tags", event.tags}});
    }

    void record_drain (const fw::drain_event_t &event)
    {
        std::lock_guard lock (_mutex);
        _drain_events.push_back (nlohmann::json{{"sequence", ++_drain_sequence},
                                                {"state", drain_state_name (event.state)},
                                                {"source", event.source_name}});
    }

    nlohmann::json snapshot () const
    {
        std::lock_guard lock (_mutex);
        return nlohmann::json{{"metrics", _metrics}, {"drainEvents", _drain_events}};
    }

  private:
    mutable std::mutex _mutex;
    std::vector<nlohmann::json> _metrics;
    std::vector<nlohmann::json> _drain_events;
    int _drain_sequence = 0;
};

/* Runtime control seam for the HTTP handlers: bound to the app after
 * create() so the drain endpoints reach the shared drain operation. */
struct drain_control_t
{
    std::function<void (std::chrono::milliseconds)> start_drain;
    std::function<bool ()> is_ready;
};

class room_spot_t;

/* OBS-A4(b): timer-originated callbacks start a fresh flow (origin=timer);
 * the tick publishes so the fresh flow shows up as a `sent` line. */
struct room_timer_handler_t
{
    void handle (room_spot_t &spot, const fw::timer_tick_t &tick) const;
};

class room_spot_t : public fw::spot_t
{
  public:
    void configure (fw::spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_handler<&room_spot_t::apply_action> (
          obs::obs_action_req_t::packet_name);
        context.handlers ().add_subscribe<&room_spot_t::on_projection> (obs::projection_topic);
    }

    void on_initialize ()
    {
        if (env_or ("ZLINK_CPP_E2E_ROOM_TIMER") == "1") {
            _timer = _context.add_timer<room_timer_handler_t> ("obs-tick",
                                                               std::chrono::milliseconds (200));
        }
    }

    obs::obs_action_res_t apply_action (const obs::obs_action_req_t &request)
    {
        _applied += request.value;
        /* OBS-A4(a)/B3: one action fans out to every mesh subscriber under
         * the same flow. */
        _context
          .publish (obs::projection_topic,
                    obs::projection_event_t{request.spot_rid, request.marker, _applied})
          .submit ();
        return obs::obs_action_res_t{request.spot_rid, request.marker, _applied};
    }

    void on_projection (const obs::projection_event_t &) { ++_projections_seen; }

    void publish_timer_marker ()
    {
        _context
          .publish (obs::projection_topic,
                    obs::projection_event_t{std::string (_context.spot_rid ().value ()),
                                            "obs-timer", _applied})
          .submit ();
    }

  private:
    fw::spot_context_t _context;
    fw::timer_t _timer;
    int _applied = 0;
    int _projections_seen = 0;
};

void room_timer_handler_t::handle (room_spot_t &spot, const fw::timer_tick_t &) const
{
    spot.publish_timer_marker ();
}

struct obs_actor_t
{
    explicit obs_actor_t (std::string id) : actor_id (std::move (id)) {}

    void set_actor_ref (const fw::actor_ref_t &value) { actor_ref = value; }

    std::string actor_id;
    int total = 0;
    fw::actor_ref_t actor_ref;
};

struct obs_actor_factory_t
{
    obs_actor_t create (std::string actor_id) const { return obs_actor_t (std::move (actor_id)); }
};

/* Entry spot hosts the player actors: admission accepts every join and the
 * ping handler accumulates state, so post-transfer pings prove continuity. */
class obs_entry_spot_t : public fw::entry_spot_t
{
  public:
    void configure (fw::entry_spot_context_t &context)
    {
        _context = context;
        context.handlers ().add_actor_request<&obs_entry_spot_t::actor_ping> (
          obs::actor_ping_req_t::packet_name);
    }

    void configure (fw::spot_context_t &context)
    {
        fw::entry_spot_context_t entry_context (context);
        configure (entry_context);
    }

    fw::spot_actor_join_response_t on_actor_join (std::string_view actor_id,
                                                  const fw::message_t &)
    {
        return fw::spot_actor_join_response_t::accept (
          obs::join_actor_res_t{std::string (actor_id),
                                std::string (_context.node_rid ().value ()), true, {}});
    }

    obs::actor_ping_res_t actor_ping (obs_actor_t &actor,
                                      fw::spot_actor_request_context_t &,
                                      const obs::actor_ping_req_t &request)
    {
        actor.total += request.value;
        return obs::actor_ping_res_t{actor.actor_id,
                                     std::string (_context.node_rid ().value ()), actor.total};
    }

  private:
    fw::entry_spot_context_t _context;
};

class join_actor_handler_t
{
  public:
    using request_type = obs::join_actor_req_t;
    using reply_type = obs::join_actor_res_t;
    using dependency_types = fw::dependency_list_t<fw::session_actor_manager_t>;

    explicit join_actor_handler_t (fw::session_actor_manager_t &actors) : _actors (actors) {}

    obs::join_actor_res_t handle (const obs::join_actor_req_t &request)
    {
        try {
            auto actor = _actors.get_or_create (obs::actor_type, request.actor_id);
            if (!actor) {
                const auto *error = actor.error ();
                return obs::join_actor_res_t{request.actor_id, {}, false,
                                             error != nullptr ? error->what () : "join failed"};
            }
            /* The drain handoff moves actors that hold a spot placement, so
             * the fixture joins the local entry spot (rid == node rid). */
            const auto role = env_or ("ZLINK_CPP_E2E_ROLE", "play-a");
            auto joined = actor.value ()
                            .context ()
                            .join_spot (fw::spot_rid_t::from_string (role),
                                        obs::join_actor_req_t{request.actor_id})
                            .async ()
                            .result ();
            if (!joined) {
                const auto *error = joined.error ();
                return obs::join_actor_res_t{request.actor_id, {}, false,
                                             error != nullptr ? error->what ()
                                                              : "entry join failed"};
            }
            const auto *accepted =
              std::get_if<fw::actor_join_accepted_t<fw::message_t>> (&joined.value ());
            if (accepted == nullptr) {
                return obs::join_actor_res_t{request.actor_id, {}, false, "join rejected"};
            }
            return obs::join_actor_res_t{
              request.actor_id, std::string (accepted->actor.node_rid ().value ()), true, {}};
        }
        catch (const std::exception &error) {
            return obs::join_actor_res_t{request.actor_id, {}, false, error.what ()};
        }
    }

  private:
    fw::session_actor_manager_t &_actors;
};

class actor_ping_handler_t
{
  public:
    using request_type = obs::actor_ping_req_t;
    using reply_type = obs::actor_ping_res_t;
    using dependency_types = fw::dependency_list_t<fw::actor_client_t, fw::actor_directory_t>;

    actor_ping_handler_t (fw::actor_client_t &actors, fw::actor_directory_t &directory) :
        _actors (actors), _directory (directory)
    {
    }

    fw::task_t<obs::actor_ping_res_t> handle (const obs::actor_ping_req_t &request)
    {
        auto actor_ref = co_await _directory.find (request.actor_id);
        if (!actor_ref) {
            throw fw::framework_exception_t (fw::framework_error_kind_t::actor_route_not_found,
                                             "player actor route was not found");
        }
        co_return co_await _actors.request_to_actor (*actor_ref, request)
          .timeout (std::chrono::milliseconds (5000))
          .async<obs::actor_ping_res_t> ();
    }

  private:
    fw::actor_client_t &_actors;
    fw::actor_directory_t &_directory;
};

/* play-a session gateway: forwards ObsActionReq packets to the target room
 * spot through the opaque spot handle (flow-correlation OBS-A1 chain:
 * connector -> stream inbound -> spot dispatch). */
class obs_session_t final : public fw::packet_stream_session_t
{
  public:
    using dependency_types =
      fw::dependency_list_t<fw::route_client_t, fw::spot_handle_resolver_t>;

    obs_session_t (fw::route_client_t &routes, fw::spot_handle_resolver_t &spots) :
        _routes (routes), _spots (spots)
    {
    }

    fw::task_t<void> on_connected (fw::stream_t &) override { co_return; }
    fw::task_t<void> on_disconnected (fw::stream_t &) override { co_return; }
    fw::task_t<void> on_error (fw::stream_t &, const fw::stream_error_t &) override { co_return; }

    fw::task_t<void> on_packet (fw::stream_t &stream,
                                const fw::stream_dispatch_context_t &dispatch,
                                const zlink::message_t &payload) override
    {
        if (dispatch.packet_name () != obs::obs_action_req_t::packet_name) {
            throw fw::framework_exception_t (fw::framework_error_kind_t::handler_not_found,
                                             "ObservabilityOps session has no handler for "
                                               + std::string (dispatch.packet_name ()));
        }
        auto request = payload.parse_json<obs::obs_action_req_t> ();
        auto handle = co_await _spots.resolve_spot_handle (
          fw::spot_rid_t::from_string (request.spot_rid));
        if (!handle) {
            throw fw::framework_exception_t (fw::framework_error_kind_t::spot_route_not_found,
                                             "room '" + request.spot_rid
                                               + "' has no live location row");
        }
        auto reply = co_await _routes.request_to_spot (*handle, request)
                       .timeout (std::chrono::milliseconds (5000))
                       .async<obs::obs_action_res_t> ();
        stream.reply_packet (zlink::message_t::from_json (reply))
          .submit ();
        co_return;
    }

  private:
    fw::route_client_t &_routes;
    fw::spot_handle_resolver_t &_spots;
};

class evidence_handler_t
{
  public:
    using dependency_types = fw::dependency_list_t<observability_evidence_t,
                                                   fw::location_store_t,
                                                   drain_control_t>;

    evidence_handler_t (observability_evidence_t &evidence,
                        fw::location_store_t &store,
                        drain_control_t &drain) :
        _evidence (evidence), _store (store), _drain (drain)
    {
    }

    fw::http_response_t handle (const fw::http_request_t &)
    {
        nlohmann::json json;
        try {
            json = _evidence.snapshot ();
        }
        catch (const std::exception &error) {
            json = nlohmann::json{{"metrics", nlohmann::json::array ()},
                                  {"drainEvents", nlohmann::json::array ()},
                                  {"snapshotError", error.what ()}};
        }
        auto peer_rows = nlohmann::json::array ();
        try {
            const auto peers = _store.list_peers (fw::peer_location_filter_t{}).result ().value ();
            for (const auto &peer : peers) {
                peer_rows.push_back (nlohmann::json{
                  {"nodeRid", peer.node_rid ? peer.node_rid->to_hex () : ""},
                  {"meshName", peer.mesh_name},
                  {"ownerId", peer.owner_id},
                  {"draining", peer.draining},
                  {"generation", peer.generation}});
            }
        }
        catch (const std::exception &error) {
            json["peerRowsError"] = error.what ();
        }
        json["peerRows"] = peer_rows;
        try {
            json["ready"] = _drain.is_ready ? _drain.is_ready () : true;
        }
        catch (const std::exception &error) {
            json["readyError"] = error.what ();
            json["ready"] = true;
        }
        fw::http_response_t response;
        /* Owner ids/rids may contain raw bytes; evidence is diagnostic, so
         * non-UTF-8 sequences are replaced instead of failing the endpoint. */
        response.body =
          json.dump (-1, ' ', false, nlohmann::json::error_handler_t::replace);
        return response;
    }

  private:
    observability_evidence_t &_evidence;
    fw::location_store_t &_store;
    drain_control_t &_drain;
};

class create_room_handler_t
{
  public:
    using request_type = obs::create_room_req_t;
    using reply_type = obs::create_room_res_t;
    using dependency_types = fw::dependency_list_t<fw::spot_node_manager_t>;

    explicit create_room_handler_t (fw::spot_node_manager_t &spots) : _spots (spots) {}

    obs::create_room_res_t handle (const obs::create_room_req_t &request)
    {
        const auto created = _spots.get_or_create_spot (
          obs::room_spot, fw::spot_rid_t::from_string (request.spot_rid));
        const auto state = created.state == fw::spot_create_state_t::created ? "created"
                           : created.state == fw::spot_create_state_t::existing
                             ? "existing"
                             : "rejected";
        return obs::create_room_res_t{request.spot_rid, state};
    }

  private:
    fw::spot_node_manager_t &_spots;
};

class drain_handler_t
{
  public:
    using request_type = obs::drain_req_t;
    using reply_type = obs::create_room_res_t;
    using dependency_types = fw::dependency_list_t<drain_control_t>;

    explicit drain_handler_t (drain_control_t &drain) : _drain (drain) {}

    obs::create_room_res_t handle (const obs::drain_req_t &request)
    {
        if (_drain.start_drain) {
            _drain.start_drain (std::chrono::milliseconds (request.deadline_ms));
        }
        return obs::create_room_res_t{"", "draining"};
    }

  private:
    drain_control_t &_drain;
};

} // namespace

int main (int argc, char **argv)
{
    const auto role = env_or ("ZLINK_CPP_E2E_ROLE", "play-a");
    const auto redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT");
    const auto redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto peer_route_endpoint = env_or ("ZLINK_CPP_E2E_PEER_ROUTE_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto spot_pub_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_PUB_ENDPOINT");
    const auto stream_endpoint = env_or ("ZLINK_CPP_E2E_STREAM_ENDPOINT");
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");

    auto app = fw::app_t::create ();
    auto evidence_owner = std::make_unique<observability_evidence_t> ();
    auto *evidence = evidence_owner.get ();
    auto drain_control_owner = std::make_unique<drain_control_t> ();
    auto *drain_control = drain_control_owner.get ();
    drain_control->start_drain = [&app] (std::chrono::milliseconds deadline) {
        (void) app.drain (deadline);
    };
    drain_control->is_ready = [&app] { return app.is_ready (); };

    /* OBS-B4: a node without a metric reader must keep messaging intact on
     * the inactive instrument path. */
    const bool metrics_enabled = env_or ("ZLINK_CPP_E2E_METRICS", "on") != "off";
    if (metrics_enabled) {
        app.monitoring ().on<fw::metric_event_payload_t> (
          [evidence] (const fw::metric_event_payload_t &event) {
              evidence->record_metric (event);
          });
    }
    app.monitoring ().on<fw::drain_event_t> (
      [evidence] (const fw::drain_event_t &event) { evidence->record_drain (event); });

    app.add_zlink_framework ([&] (fw::zlink_framework_options_t &framework) {
        framework.services ().add_singleton<observability_evidence_t> (
          std::move (evidence_owner));
        framework.services ().add_singleton<drain_control_t> (std::move (drain_control_owner));
        framework.add_location_store (
          std::make_shared<fw::locations::redis::redis_location_store_t> (
            fw::locations::redis::redis_location_options_t{
              .connection_string = redis_endpoint, .key_prefix = redis_key_prefix}));
        {
            auto locations = framework.configure_locations ();
            locations.heartbeat_interval = std::chrono::seconds (1);
            locations.owner_lease_ttl = std::chrono::seconds (5);
            locations.polling_interval = std::chrono::milliseconds (250);
            locations.spot_router_channels[obs::spot_mesh] = obs::spot_route_channel;
        }
        /* OBS-A3(b): an off node must not create flows but still propagates
         * the received pair to the next hop. */
        const auto trace_mode = env_or ("ZLINK_CPP_E2E_TRACE_MODE", "key_transitions");
        framework.configure_dispatch ()
          .message_flow (trace_mode == "off" ? fw::message_flow_log_mode_t::off
                                             : fw::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + role + "-flow.log")
          .trace_label ("cpp-obs-" + role);

        auto spot_route = framework.add_route_mesh (obs::spot_route_channel);
        spot_route.enable_server (route_endpoint);
        spot_route.set_routing_id (zlink::routing_id_t::from (role));
        if (!peer_route_endpoint.empty ()) {
            spot_route.enable_client (peer_route_endpoint);
        }

        auto spot_mesh = framework.add_spot_mesh (obs::spot_mesh);
        /* OBS-C3: room mesh policy is configurable per run — drain_natural
         * keeps in-progress rooms; release_and_recreate frees the rows for
         * a GetOrCreate rebuild on another owner. */
        spot_mesh.use_drain_policy (env_or ("ZLINK_CPP_E2E_DRAIN_POLICY") == "release_and_recreate"
                                      ? fw::spot_drain_policy_t::release_and_recreate
                                      : fw::spot_drain_policy_t::drain_natural);
        spot_mesh.set_routing_id (zlink::routing_id_t::from (role))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (spot_pub_endpoint)
          .accept_route_mesh (obs::spot_route_channel)
          .add_entry_spot<obs_entry_spot_t> ()
          .add_spot<room_spot_t> (obs::room_spot)
          .add_actor_factory<obs_actor_factory_t> (obs::actor_type);

        if (!stream_endpoint.empty ()) {
            framework.add_stream_node ("obs.session")
              .bind (stream_endpoint)
              .register_session<obs_session_t> ();
        }

        framework.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence")
          .map_post<create_room_handler_t> ("/spot/create")
          .map_post<join_actor_handler_t> ("/actor/join")
          .map_post<actor_ping_handler_t> ("/actor/ping")
          .map_post<drain_handler_t> ("/drain");
    });
    return app.run (argc, argv);
}
