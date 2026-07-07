/* SPDX-License-Identifier: MPL-2.0 */

#include "../../Shared/messages.hpp"

#include <zlink/framework.hpp>
#include <zlink/locations/redis.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

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

std::string kind_name (zlink::framework::framework_error_kind_t kind)
{
    switch (kind) {
        case zlink::framework::framework_error_kind_t::actor_route_not_found:
            return "actor_route_not_found";
        case zlink::framework::framework_error_kind_t::actor_location_stale:
            return "actor_location_stale";
        case zlink::framework::framework_error_kind_t::route_not_connected:
            return "route_not_connected";
        case zlink::framework::framework_error_kind_t::request_failed:
            return "request_failed";
        default:
            return "framework_error_" + std::to_string (static_cast<int> (kind));
    }
}

e2e::actor_call_response_t failed (const e2e::actor_call_request_t &request,
                                   const zlink::framework::framework_exception_t &error)
{
    return {request.scenario, request.actor_id, error.what (), kind_name (error.kind ())};
}

class send_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::actor_client_t>;
    using request_type = e2e::actor_call_request_t;
    using reply_type = e2e::actor_call_response_t;

    explicit send_handler_t (zlink::framework::actor_client_t &actors) : _actors (actors) {}

    zlink::framework::task_t<e2e::actor_call_response_t>
    handle (const e2e::actor_call_request_t &request)
    {
        try {
            e2e::actor_notify_t notify{request.scenario, request.actor_id, request.value};
            co_await _actors.send_to_actor (request.actor_id, notify)
              .packet_name (e2e::actor_notify_t::packet_name)
              .async ();
            co_return e2e::actor_call_response_t{request.scenario, request.actor_id, "sent", ""};
        }
        catch (const zlink::framework::framework_exception_t &error) {
            co_return failed (request, error);
        }
    }

  private:
    zlink::framework::actor_client_t &_actors;
};

class request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::actor_client_t>;
    using request_type = e2e::actor_call_request_t;
    using reply_type = e2e::actor_call_response_t;

    explicit request_handler_t (zlink::framework::actor_client_t &actors) : _actors (actors) {}

    zlink::framework::task_t<e2e::actor_call_response_t>
    handle (const e2e::actor_call_request_t &request)
    {
        try {
            e2e::actor_ask_t ask{request.scenario, request.actor_id, request.value};
            auto reply = co_await _actors.request_to_actor (request.actor_id, ask)
                           .packet_name (e2e::actor_ask_t::packet_name)
                           .timeout (std::chrono::seconds (5))
                           .async<e2e::actor_reply_t> ();
            co_return e2e::actor_call_response_t{
              request.scenario, request.actor_id, reply.value, ""};
        }
        catch (const zlink::framework::framework_exception_t &error) {
            co_return failed (request, error);
        }
    }

  private:
    zlink::framework::actor_client_t &_actors;
};

} // namespace

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    app.logging ().use_file (log_dir + "/caller.log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &framework) {
        framework.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/caller-flow.log")
          .trace_label ("cpp-to-actor-caller");
        add_redis_location_store (framework);
        auto mesh = framework.add_spot_mesh (e2e::spot_mesh_name)
          .enable_router (env_or ("ZLINK_CPP_E2E_CALLER_SPOT"))
          .enable_pub_sub (env_or ("ZLINK_CPP_E2E_CALLER_PUBSUB"))
          .set_routing_id (zlink::routing_id_t::from (
            env_or ("ZLINK_CPP_E2E_CALLER_RID", "caller")));
        const auto actor_spot = env_or ("ZLINK_CPP_E2E_ACTOR_SPOT");
        if (!actor_spot.empty ()) {
            mesh.connect_router (zlink::routing_id_t::from (env_or ("ZLINK_CPP_E2E_ACTOR_RID",
                                                                     "actor-a")),
                                 actor_spot);
        }
        framework.http ()
          .listen (env_or ("ZLINK_CPP_E2E_CALLER_HTTP"))
          .map_health ("/health")
          .map_post<send_handler_t> ("/send")
          .map_post<request_handler_t> ("/request");
    });
    return app.run (argc, argv);
}
