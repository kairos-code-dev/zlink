/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "../../Shared/messages.hpp"

#include <zlink/framework.hpp>
#include <zlink/locations/redis.hpp>

#include <cstdlib>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace e2e = zlink::e2e::to_actor_messaging;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

void add_redis_location_store (zlink::framework::zlink_framework_options_t &framework)
{
    const auto endpoint = env_or ("ZLINK_CPP_E2E_REDIS_LOCATION_ENDPOINT",
                                  env_or ("ZLINK_REDIS_LOCATION_ENDPOINT", "127.0.0.1:16379"));
    const auto key_prefix = env_or ("ZLINK_CPP_E2E_LOCATION_KEY_PREFIX");
    if (key_prefix.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_LOCATION_KEY_PREFIX is required");
    }
    framework.add_location_store (
      std::make_shared<zlink::framework::locations::redis::redis_location_store_t> (
        zlink::framework::locations::redis::redis_location_options_t{
          .connection_string = endpoint, .key_prefix = key_prefix}));
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
        context.handlers ().add_actor_send<&to_actor_e2e_spot_t::on_notify> (
          e2e::actor_notify_t::packet_name);
        context.handlers ().add_actor_request<&to_actor_e2e_spot_t::on_ask> (
          e2e::actor_ask_t::packet_name);
    }

    void on_create_actor (to_actor_e2e_actor_t &actor, const zlink::framework::message_t &)
    {
        _evidence.append ({"create", actor.actor_id (), "create", "created"});
    }

    void on_actor_joined (to_actor_e2e_actor_t &actor)
    {
        _evidence.append ({"join", actor.actor_id (), "join", "joined"});
    }

    void on_notify (to_actor_e2e_actor_t &actor,
                    zlink::framework::spot_actor_send_context_t &,
                    const e2e::actor_notify_t &message)
    {
        _evidence.append ({message.scenario, actor.actor_id (), "send", message.value});
    }

    e2e::actor_reply_t on_ask (to_actor_e2e_actor_t &actor,
                               zlink::framework::spot_actor_request_context_t &,
                               const e2e::actor_ask_t &message)
    {
        _evidence.append ({message.scenario, actor.actor_id (), "request", message.value});
        return {message.scenario, actor.actor_id (), "reply:" + message.value};
    }

  private:
    evidence_store_t &_evidence;
};

class ensure_actor_handler_t
{
  public:
    using dependency_types =
      zlink::framework::dependency_list_t<zlink::framework::session_actor_manager_t>;
    using request_type = e2e::actor_call_request_t;
    using reply_type = e2e::actor_call_response_t;

    explicit ensure_actor_handler_t (zlink::framework::session_actor_manager_t actors) :
        _actors (std::move (actors))
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
            auto bound = _actors.bind_or_get (actor.value ().ref ()).async ().result ();
            if (!bound) {
                throw *bound.error ();
            }
            const auto rid = env_or ("ZLINK_CPP_E2E_ACTOR_RID", "actor-a");
            auto joined = bound.value ()
                            .context ()
                            .join_entry_spot (
                              zlink::framework::node_rid_t::from_string (rid),
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

} // namespace

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    app.logging ().use_file (log_dir + "/actor.log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        auto evidence = std::make_unique<evidence_store_t> ();
        auto *evidence_ptr = evidence.get ();
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/actor-flow.log")
          .trace_label ("cpp-to-actor-actor");
        framework.services ().add_singleton<evidence_store_t> (std::move (evidence));
        add_redis_location_store (framework);
        auto mesh = framework.add_spot_mesh (e2e::spot_mesh_name)
          .enable_router (env_or ("ZLINK_CPP_E2E_ACTOR_SPOT"))
          .enable_pub_sub (env_or ("ZLINK_CPP_E2E_ACTOR_PUBSUB"))
          .set_routing_id (zlink::routing_id_t::from (
            env_or ("ZLINK_CPP_E2E_ACTOR_RID", "actor-a")))
          .add_entry_spot<to_actor_e2e_spot_t> (
            [evidence_ptr] { return std::make_shared<to_actor_e2e_spot_t> (*evidence_ptr); })
          .add_actor_factory<to_actor_e2e_actor_t> (e2e::actor_type_name);
        const auto caller_spot = env_or ("ZLINK_CPP_E2E_CALLER_SPOT");
        if (!caller_spot.empty ()) {
            mesh.connect_router (zlink::routing_id_t::from (env_or ("ZLINK_CPP_E2E_CALLER_RID",
                                                                     "caller")),
                                 caller_spot);
        }
        framework.http ()
          .listen (env_or ("ZLINK_CPP_E2E_ACTOR_HTTP"))
          .map_health ("/health")
          .map_get<evidence_handler_t> ("/evidence")
          .map_post<ensure_actor_handler_t> ("/ensure");
    });
    return app.run (argc, argv);
}
