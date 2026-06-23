/* SPDX-License-Identifier: MPL-2.0 */

#include "../Shared/spot_service_contracts.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

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

void ensure (bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error (message);
    }
}

zlink::framework::actor_ref_t to_actor_ref (const e2e::actor_ref_dto_t &actor)
{
    return zlink::framework::actor_ref_t (
      zlink::framework::node_rid_t::from_string (actor.node_rid), actor.actor_type,
      actor.actor_id, actor.generation);
}

template <typename T> zlink::message_t encode_json (const T &value)
{
    return zlink::message_t::from (nlohmann::json (value).dump ());
}

zlink::framework::stream_header_t request_header (std::string packet_name)
{
    return zlink::framework::stream_header_t (
      zlink::framework::stream_message_kind_t::request, zlink::framework::stream_codec_t::json,
      zlink::framework::stream_header_flags_t::none, std::nullopt, std::move (packet_name));
}

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto scope = services.create_scope ();
            auto &routes = scope.get_required<zlink::framework::route_client_t> ();
            auto &actors = scope.get_required<zlink::framework::session_actor_manager_t> ();
            run (routes, actors);
            passed = true;
        }
        catch (const std::exception &error) {
            std::cerr << "spot-service scenario failed: " << error.what () << std::endl;
        }
        _app.stop ();
        std::cout << std::flush;
        std::cerr << std::flush;
        std::_Exit (passed ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    zlink::framework::session_actor_t bind_actor (zlink::framework::route_client_t &routes,
                                                  zlink::framework::session_actor_manager_t &actors,
                                                  const std::string &target_node,
                                                  const std::string &actor_id,
                                                  const std::string &display_name)
    {
        auto ensured = routes
                         .request (e2e::route_channel, zlink::routing_id_t::from (target_node),
                                   e2e::ensure_actor_req_t{actor_id, display_name})
                         .packet_name ("EnsureActor")
                         .timeout (std::chrono::milliseconds (3000))
                         .async<e2e::ensure_actor_res_t> ()
                         .result ();
        ensure (ensured.has_value (),
                "EnsureActor failed for " + actor_id + ": "
                  + (ensured.error () ? ensured.error ()->what () : "unknown"));
        auto bound = actors.bind (to_actor_ref (ensured.value ().actor)).async ().result ();
        ensure (bound.has_value (), "bind actor failed for " + actor_id);
        return bound.value ();
    }

    template <typename TReply, typename TRequest>
    TReply relay_request (zlink::framework::session_actor_t &actor,
                          const std::string &packet_name,
                          const TRequest &request)
    {
        auto reply = actor.relay_request_raw (request_header (packet_name), encode_json (request))
                       .async ()
                       .result ();
        ensure (reply.has_value (),
                packet_name + " relay failed: "
                  + (reply.error () ? reply.error ()->what () : "unknown"));
        return reply.value ().template decode<TReply> ();
    }

    void run (zlink::framework::route_client_t &routes,
              zlink::framework::session_actor_manager_t &actors)
    {
        auto refresh_actor = [&actors] (const std::string &actor_id) {
            auto refreshed = actors.find (actor_id);
            ensure (refreshed.has_value (), "actor was not found after join: " + actor_id);
            return refreshed.value ();
        };

        auto local = bind_actor (routes, actors, "play-a", "alice", "Alice");
        auto local_join = relay_request<e2e::join_res_t> (
          local, "JoinReq",
          e2e::join_req_t{.key = "a-room",
                          .actor_id = "alice",
                          .display_name = "Alice",
                          .level = 7,
                          .tags = {"alpha", "local"}});
        ensure (local_join.owner_node_rid == "play-a", "SM-A1/SM-B1 owner mismatch");
        ensure (local_join.spot_rid == "user:play-a:a-room", "SM-A1 spot rid mismatch");
        ensure (local_join.actor_id == "alice" && local_join.display_name == "Alice"
                  && local_join.level == 7 && local_join.tags.size () == 2
                  && local_join.tags[0] == "alpha" && local_join.tags[1] == "local",
                "SM-B3 join payload fidelity mismatch");
        std::cout << "scenario SM-A1 passed\n";
        std::cout << "scenario SM-B1 passed\n";
        std::cout << "scenario SM-B3 passed\n";
        local = refresh_actor ("alice");

        auto state1 =
          relay_request<e2e::state_res_t> (local, "StateReq", e2e::state_req_t{"add", 3});
        auto state2 =
          relay_request<e2e::state_res_t> (local, "StateReq", e2e::state_req_t{"add", 4});
        ensure (state1.value == 3 && state1.sequence == 1, "SM-A2 first mutation mismatch");
        ensure (state2.value == 7 && state2.sequence == 2, "SM-A2 second mutation mismatch");
        std::cout << "scenario SM-A2 passed\n";

        auto same_key_actor = bind_actor (routes, actors, "play-a", "alice-2", "Alice Two");
        auto same_key_join = relay_request<e2e::join_res_t> (
          same_key_actor, "JoinReq",
          e2e::join_req_t{.key = "a-room",
                          .actor_id = "alice-2",
                          .display_name = "Alice Two",
                          .level = 2,
                          .tags = {"same-key"}});
        ensure (same_key_join.owner_node_rid == "play-a"
                  && same_key_join.spot_rid == local_join.spot_rid,
                "SM-A4 same key did not keep the same owner");
        same_key_actor = refresh_actor ("alice-2");

        auto remote = bind_actor (routes, actors, "play-b", "bob", "Bob");
        auto remote_join = relay_request<e2e::join_res_t> (
          remote, "JoinReq",
          e2e::join_req_t{.key = "b-room",
                          .actor_id = "bob",
                          .display_name = "Bob",
                          .level = 9,
                          .tags = {"beta", "remote"}});
        ensure (remote_join.owner_node_rid == "play-b", "SM-B2 owner mismatch");
        ensure (remote_join.spot_rid == "user:play-b:b-room", "SM-A4 remote spot rid mismatch");
        remote = refresh_actor ("bob");
        std::cout << "scenario SM-A4 passed\n";
        std::cout << "scenario SM-B2 passed\n";

        auto left =
          relay_request<e2e::leave_res_t> (local, "LeaveReq", e2e::leave_req_t{"client-left"});
        ensure (left.left && left.actor_id == "alice", "SM-B6 leave reply mismatch");
        std::cout << "scenario SM-B6 leave passed\n";
    }

    zlink::framework::app_t &_app;
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
      .add_json<e2e::disconnect_res_t> ();
}

} // namespace

int main (int argc, char **argv)
{
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto route_a_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT");
    const auto route_b_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");

    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.logging ().use_file (log_dir + "/client.log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/client-flow.log")
          .trace_node_id ("cpp-sm-client");
        configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        auto route = options.add_route_mesh_channel (e2e::route_channel)
          .enable_server (route_endpoint)
          .set_routing_id (zlink::routing_id_t::from (std::string ("session-a")))
          .enable_client ()
          .enable_spot_route_egress (e2e::route_channel);
        if (!route_a_endpoint.empty ()) {
            route.enable_client (route_a_endpoint);
        }
        if (!route_b_endpoint.empty ()) {
            route.enable_client (route_b_endpoint);
        }
        options.add_spot_mesh (e2e::spot_mesh)
          .use_registry_spot_resolver (e2e::route_channel)
          .add_node ("session-a")
          .enable_router (spot_router_endpoint, zlink::routing_id_t::from (std::string ("session-a")))
          .enable_actor_gateway ()
          .enable_pub_sub (pubsub_endpoint, zlink::routing_id_t::from (std::string ("session-a")));
    });
    app.add_hosted_service (std::move (scenario));
    const auto code = app.run (argc, argv);
    return code == 0 && scenario_result->passed ? 0 : 1;
}
