/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../../Shared/messages.hpp"
#include "../../Shared/configuration.hpp"

#include <zlink/framework.hpp>
#include <zlink/locations/redis.hpp>

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace e2e = zlink::e2e::to_actor_messaging;

namespace
{

void add_redis_location_store (zlink::framework::zlink_framework_options_t &framework,
                               const e2e::redis_configuration_t &redis)
{
    framework.add_location_store (
      std::make_shared<zlink::framework::locations::redis::redis_location_store_t> (
        zlink::framework::locations::redis::redis_location_options_t{
          .connection_string = redis.endpoint, .key_prefix = redis.key_prefix}));
    auto &locations = framework.configure_locations ();
    locations.heartbeat_interval = std::chrono::seconds (1);
    locations.owner_lease_ttl = std::chrono::seconds (3);
    locations.polling_interval = std::chrono::milliseconds (500);
}

class evidence_store_t
{
  public:
    void append (e2e::actor_evidence_t entry)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _entries.push_back (std::move (entry));
    }

    std::vector<e2e::actor_evidence_t> all () const
    {
        std::lock_guard<std::mutex> lock (_mutex);
        return _entries;
    }

  private:
    mutable std::mutex _mutex;
    std::vector<e2e::actor_evidence_t> _entries;
};

class to_actor_e2e_actor_t
{
  public:
    void set_actor_ref (const zlink::framework::actor_ref_t &actor_ref)
    {
        _actor_id = std::string (actor_ref.actor_id ());
    }

    const std::string &actor_id () const { return _actor_id; }

  private:
    std::string _actor_id;
};

class to_actor_e2e_spot_t : public zlink::framework::entry_spot_t
{
  public:
    explicit to_actor_e2e_spot_t (evidence_store_t &evidence) : _evidence (evidence) {}

    void configure (zlink::framework::spot_context_t &context)
    {
        _context = zlink::framework::entry_spot_context_t (context);
        context.handlers ().add_actor_send<&to_actor_e2e_spot_t::on_notify> (
          e2e::actor_notify_t::packet_name);
        context.handlers ().add_actor_request<&to_actor_e2e_spot_t::on_ask> (
          e2e::actor_ask_t::packet_name);
    }

    void on_create_actor (to_actor_e2e_actor_t &actor, const zlink::framework::message_t &)
    {
        _evidence.append ({"create", actor.actor_id (), "create", "created"});
    }

    zlink::framework::task_t<void> on_actor_joined (to_actor_e2e_actor_t &actor)
    {
        _evidence.append ({"join", actor.actor_id (), "join", "joined"});
        co_return;
    }

    void on_notify (to_actor_e2e_actor_t &actor,
                    zlink::framework::spot_actor_send_context_t &,
                    const e2e::actor_notify_t &message)
    {
        _evidence.append ({message.scenario, actor.actor_id (), "send", message.value});
    }

    zlink::framework::task_t<e2e::actor_reply_t>
    on_ask (to_actor_e2e_actor_t &actor,
            zlink::framework::spot_actor_request_context_t &,
            const e2e::actor_ask_t &message)
    {
        _evidence.append ({message.scenario, actor.actor_id (), "request", message.value});
        auto reply = e2e::actor_reply_t{message.scenario, actor.actor_id (),
                                        "reply:" + message.value};
        if (message.scenario == "TA-B1-destroy" || message.scenario == "TA-A4-destroy"
            || message.scenario == "TA-B2-destroy") {
            co_await _context.destroy_actor (actor);
        }
        co_return reply;
    }

  private:
    evidence_store_t &_evidence;
    zlink::framework::entry_spot_context_t _context;
};

class ensure_actor_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::session_actor_manager_t,
                                         e2e::actor_configuration_t>;
    using request_type = e2e::actor_call_request_t;
    using reply_type = e2e::actor_call_response_t;

    ensure_actor_handler_t (zlink::framework::session_actor_manager_t actors,
                            e2e::actor_configuration_t &configuration) :
        _actors (std::move (actors)), _configuration (configuration)
    {
    }

    e2e::actor_call_response_t handle (const e2e::actor_call_request_t &request)
    {
        try {
            auto actor = _actors.get_or_create (e2e::actor_type_name, request.actor_id,
                                                std::string ("create"));
            if (!actor) {
                throw *actor.error ();
            }
            auto bound = _actors.bind_or_get (actor.value ().ref ()).submit ().result ();
            if (!bound) {
                throw *bound.error ();
            }
            auto joined = bound.value ()
                            .context ()
                            .join_entry_spot (
                              zlink::framework::node_rid_t::from_string (
                                _configuration.node_rid),
                              zlink::framework::message_t {})
                            .async ()
                            .result ();
            if (!joined) {
                throw *joined.error ();
            }
            return {request.scenario, request.actor_id, "ensured", ""};
        }
        catch (const zlink::framework::framework_exception_t &error) {
            return {request.scenario, request.actor_id, "",
                    std::to_string (static_cast<int> (error.kind ())) + ":" + error.what ()};
        }
    }

  private:
    zlink::framework::session_actor_manager_t _actors;
    e2e::actor_configuration_t &_configuration;
};

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<evidence_store_t>;

    explicit evidence_handler_t (evidence_store_t &evidence) : _evidence (evidence) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_evidence.all ()).dump ();
        return response;
    }

  private:
    evidence_store_t &_evidence;
};

class push_actor_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::session_actor_manager_t>;
    using request_type = e2e::actor_call_request_t;
    using reply_type = e2e::actor_call_response_t;

    explicit push_actor_handler_t (zlink::framework::session_actor_manager_t &actors) :
        _actors (actors)
    {
    }

    e2e::actor_call_response_t handle (const e2e::actor_call_request_t &request)
    {
        const auto actor = _actors.find (request.actor_id);
        if (!actor) {
            return {request.scenario, request.actor_id, "", "actor_route_not_found"};
        }
        actor->bound_session ()
          .send (e2e::actor_push_notify_t{request.scenario, request.actor_id, request.value})
          .submit ();
        return {request.scenario, request.actor_id, "pushed", ""};
    }

  private:
    zlink::framework::session_actor_manager_t &_actors;
};

} // namespace

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto configuration =
      e2e::load_role_configuration<e2e::actor_configuration_t> (app, argc, argv);
    app.logging ().use_file (configuration.log_dir + "/actor.log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([configuration] (
                              zlink::framework::zlink_framework_options_t &framework) {
        auto evidence = std::make_unique<evidence_store_t> ();
        auto *evidence_ptr = evidence.get ();
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (configuration.log_dir + "/actor-flow.log")
          .trace_label ("cpp-to-actor-actor");
        framework.services ().add_singleton<evidence_store_t> (std::move (evidence));
        framework.services ().add_singleton<e2e::actor_configuration_t> (
          std::make_unique<e2e::actor_configuration_t> (configuration));
        add_redis_location_store (framework, configuration.redis);
        auto mesh = framework.add_route_mesh (e2e::spot_mesh_name)
          .listen (configuration.spot_endpoint)
          .set_routing_id (zlink::routing_id_t::from (configuration.node_rid))
          .add_entry_spot<to_actor_e2e_spot_t> (
            [evidence_ptr] { return std::make_shared<to_actor_e2e_spot_t> (*evidence_ptr); })
          .add_actor_factory<to_actor_e2e_actor_t> (e2e::actor_type_name);
        if (!configuration.caller_spot_endpoint.empty ()) {
            mesh.peer_connections ().connect (
              zlink::routing_id_t::from (configuration.caller_rid),
              configuration.caller_spot_endpoint);
        }
        framework.http ()
          .listen (configuration.http_endpoint)
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence")
          .map_post<push_actor_handler_t> ("/push")
          .map_post<ensure_actor_handler_t> ("/ensure");
    });
    return app.run (argc, argv);
}
