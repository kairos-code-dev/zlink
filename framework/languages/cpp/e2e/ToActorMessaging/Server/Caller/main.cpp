/* SPDX-License-Identifier: FSL-1.1-ALv2 */

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

std::shared_ptr<zlink::framework::location_store_t> make_redis_location_store ()
{
    const auto endpoint = env_or ("ZLINK_CPP_E2E_REDIS_LOCATION_ENDPOINT",
                                  env_or ("ZLINK_REDIS_LOCATION_ENDPOINT", "127.0.0.1:16379"));
    const auto key_prefix = env_or ("ZLINK_CPP_E2E_LOCATION_KEY_PREFIX");
    if (key_prefix.empty ()) {
        throw std::runtime_error ("ZLINK_CPP_E2E_LOCATION_KEY_PREFIX is required");
    }
    return std::make_shared<zlink::framework::locations::redis::redis_location_store_t> (
      zlink::framework::locations::redis::redis_location_options_t{
        .connection_string = endpoint, .key_prefix = key_prefix});
}

void write_fault_actor_row (const e2e::actor_call_request_t &request)
{
    auto store = make_redis_location_store ();
    const auto owner_id = "toactor-fault-" + request.actor_id;
    const auto actor_node =
      request.scenario == "TA-B3-prepare" ? std::string ("ghost-node")
                                          : env_or ("ZLINK_CPP_E2E_ACTOR_RID", "actor-a");
    const auto actor_spot =
      request.scenario == "TA-B3-prepare" ? std::string ("ghost-spot")
                                          : env_or ("ZLINK_CPP_E2E_ACTOR_RID", "actor-a");
    const auto lease =
      store->renew_owner_lease (owner_id, zlink::routing_id_t::from (actor_node),
                                std::chrono::seconds (30))
        .result ();
    if (!lease) {
        throw lease.error () ? *lease.error ()
                             : zlink::framework::framework_exception_t (
                                 zlink::framework::framework_error_kind_t::request_failed,
                                 "fault owner lease failed");
    }
    auto write =
      store
        ->update_actor (
          zlink::framework::actor_location_t{
            .actor_id = request.actor_id,
            .actor_type = e2e::actor_type_name,
            .actor_ref = zlink::framework::actor_ref_t (
              zlink::framework::node_rid_t::from_string (actor_node), e2e::actor_type_name,
              request.actor_id, 99),
            .node_rid = zlink::routing_id_t::from (actor_node),
            .location_kind = zlink::spot_kind::entry,
            .spot_mesh_name = e2e::spot_mesh_name,
            .spot_rid = zlink::routing_id_t::from (actor_spot),
            .owner_id = owner_id},
          zlink::framework::location_write_intent_t::takeover)
        .result ();
    if (!write
        || write.value ().status != zlink::framework::location_write_status_t::stored) {
        throw write.error () ? *write.error ()
                             : zlink::framework::framework_exception_t (
                                 zlink::framework::framework_error_kind_t::request_failed,
                                 "fault actor row write failed");
    }
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
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::actor_client_t,
                                                                zlink::framework::actor_directory_t>;
    using request_type = e2e::actor_call_request_t;
    using reply_type = e2e::actor_call_response_t;

    send_handler_t (zlink::framework::actor_client_t &actors,
                    zlink::framework::actor_directory_t &directory) :
        _actors (actors), _directory (directory)
    {
    }

    zlink::framework::task_t<e2e::actor_call_response_t>
    handle (const e2e::actor_call_request_t &request)
    {
        try {
            e2e::actor_notify_t notify{request.scenario, request.actor_id, request.value};
            auto actor_ref = co_await _directory.find (request.actor_id);
            if (!actor_ref) {
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::actor_route_not_found,
                  "actor route was not found");
            }
            _actors.send_to_actor (*actor_ref, notify)
              .submit ();
            co_return e2e::actor_call_response_t{request.scenario, request.actor_id, "sent", ""};
        }
        catch (const zlink::framework::framework_exception_t &error) {
            co_return failed (request, error);
        }
    }

  private:
    zlink::framework::actor_client_t &_actors;
    zlink::framework::actor_directory_t &_directory;
};

class request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<zlink::framework::actor_client_t,
                                                                zlink::framework::actor_directory_t>;
    using request_type = e2e::actor_call_request_t;
    using reply_type = e2e::actor_call_response_t;

    request_handler_t (zlink::framework::actor_client_t &actors,
                       zlink::framework::actor_directory_t &directory) :
        _actors (actors), _directory (directory)
    {
    }

    zlink::framework::task_t<e2e::actor_call_response_t>
    handle (const e2e::actor_call_request_t &request)
    {
        try {
            e2e::actor_ask_t ask{request.scenario, request.actor_id, request.value};
            auto actor_ref = co_await _directory.find (request.actor_id);
            if (!actor_ref) {
                throw zlink::framework::framework_exception_t (
                  zlink::framework::framework_error_kind_t::actor_route_not_found,
                  "actor route was not found");
            }
            auto reply = co_await _actors.request_to_actor (*actor_ref, ask)
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
    zlink::framework::actor_directory_t &_directory;
};

class prepare_failure_handler_t
{
  public:
    using request_type = e2e::actor_call_request_t;
    using reply_type = e2e::actor_call_response_t;

    e2e::actor_call_response_t handle (const e2e::actor_call_request_t &request)
    {
        try {
            write_fault_actor_row (request);
            return {request.scenario, request.actor_id, "prepared", ""};
        }
        catch (const zlink::framework::framework_exception_t &error) {
            return failed (request, error);
        }
    }
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
          .map_post<request_handler_t> ("/request")
          .map_post<prepare_failure_handler_t> ("/prepare-failure");
    });
    return app.run (argc, argv);
}
