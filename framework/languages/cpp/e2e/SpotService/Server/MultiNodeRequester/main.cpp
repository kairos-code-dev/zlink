/* SPDX-License-Identifier: MPL-2.0 */

#include "../MultiNode/Handlers/multi_node_handlers.hpp"
#include "../Shared/Support/codecs.hpp"
#include "../Shared/Support/env.hpp"
#include "../Shared/Support/location_store.hpp"

#include <zlink/framework.hpp>

#include <memory>
#include <string>

class requester_bridge_spot_t : public zlink::framework::spot_t
{
  public:
    void configure (zlink::framework::spot_context_t &) {}
};

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = env_or ("ZLINK_CPP_E2E_NODE_RID", multi_node_a_name);
    const auto route_client_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_CLIENT_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto redis_endpoint = env_or ("ZLINK_CPP_E2E_REDIS_ENDPOINT");
    const auto redis_key_prefix = env_or ("ZLINK_CPP_E2E_REDIS_KEY_PREFIX");

    app.logging ()
      .use_file (log_dir + "/" + node_rid + "-requester.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        auto state = std::make_unique<scenario_state_t> (node_rid);
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-requester-flow.log")
          .trace_label ("cpp-sm-" + node_rid + "-requester");
        options.services ()
          .add_singleton<scenario_state_t> (std::move (state))
          .add_transient<multi_node_route_ping_proxy_handler_t, scenario_state_t,
                         zlink::framework::route_client_t> ()
          .add_transient<multi_node_state_route_handler_t, scenario_state_t,
                         zlink::framework::route_client_t> ();
        configure_codecs (options.codecs ());
        add_redis_location_store (options, redis_endpoint, redis_key_prefix);
        options.add_route_mesh_channel (multi_node_route_channel_for (node_rid))
          .enable_server (route_client_endpoint)
          .set_routing_id (zlink::routing_id_t::from ("requester-" + node_rid))
          .enable_client ();
        options.add_spot_mesh ("requester-" + node_rid)
          .set_routing_id (zlink::routing_id_t::from ("requester-spot-" + node_rid))
          .enable_router (spot_router_endpoint)
          .add_spot<requester_bridge_spot_t> ("requester-bridge");
        options.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_post<multi_node_route_ping_proxy_handler_t> ("/route/control-ping")
          .map_post<multi_node_state_route_handler_t> ("/spot/state/request");
    });
    return app.run (argc, argv);
}
