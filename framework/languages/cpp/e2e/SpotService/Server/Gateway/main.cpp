/* SPDX-License-Identifier: MPL-2.0 */

#include "../../Shared/spot_service_contracts.hpp"
#include "../Shared/scenario_state.hpp"

#include <zlink/framework.hpp>

#include <cstdlib>
#include <memory>
#include <string>

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

class gateway_evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<scenario_state_t>;

    explicit gateway_evidence_handler_t (scenario_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = nlohmann::json (_state.snapshot ()).dump ();
        return response;
    }

  private:
    scenario_state_t &_state;
};

class gateway_publish_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<
      scenario_state_t, zlink::framework::spot_publisher_client_t>;

    gateway_publish_handler_t (scenario_state_t &state,
                               zlink::framework::spot_publisher_client_t &publisher) :
        _state (state), _publisher (publisher)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &http)
    {
        const auto request =
          nlohmann::json::parse (http.body).get<e2e::spot_publish_route_req_t> ();
        auto published =
          _publisher
            .publish (e2e::publisher_channel, e2e::mesh_topic,
                      e2e::mesh_event_t{"evt-sm-c4", request.marker})
            .result ();
        if (!published) {
            throw zlink::framework::framework_exception_t (
              published.error_kind (),
              published.error () ? published.error ()->what () : "SM-C4 publish failed");
        }
        _state.record ("SpotPublish", {}, request.spot_rid,
                       "publisher=" + _state.node_rid + "|marker=" + request.marker);

        zlink::framework::http_response_t response;
        response.body =
          nlohmann::json (e2e::spot_publish_route_res_t{.accepted = true}).dump ();
        return response;
    }

  private:
    scenario_state_t &_state;
    zlink::framework::spot_publisher_client_t &_publisher;
};

void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    codecs.add_json ();
    codecs.add_json<e2e::mesh_event_t,
                    e2e::spot_publish_route_req_t,
                    e2e::spot_publish_route_res_t,
                    e2e::evidence_entry_t,
                    e2e::evidence_snapshot_t> ();
}

} // namespace

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto node_rid = env_or ("ZLINK_CPP_E2E_NODE_RID", "gateway");
    const auto route_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_ENDPOINT");
    const auto route_a_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT");
    const auto route_b_endpoint = env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT");
    const auto spot_router_endpoint = env_or ("ZLINK_CPP_E2E_SPOT_ROUTER_ENDPOINT");
    const auto pubsub_endpoint = env_or ("ZLINK_CPP_E2E_PUBSUB_ENDPOINT");
    const auto http_endpoint = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto registry_router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");

    app.logging ()
      .use_file (log_dir + "/" + node_rid + ".log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        auto state = std::make_unique<scenario_state_t> (node_rid);
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/" + node_rid + "-flow.log")
          .trace_label ("cpp-sm-" + node_rid);
        options.services ()
          .add_singleton<scenario_state_t> (std::move (state))
          .add_transient<gateway_evidence_handler_t, scenario_state_t> ()
          .add_transient<gateway_publish_handler_t, scenario_state_t,
                         zlink::framework::spot_publisher_client_t> ();
        configure_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (registry_router);
        auto route = options.add_route_mesh (e2e::route_channel)
                       .enable_server (route_endpoint)
                       .set_routing_id (zlink::routing_id_t::from (node_rid))
                       .enable_client ();
        if (!route_a_endpoint.empty ()) {
            route.enable_client (route_a_endpoint);
        }
        if (!route_b_endpoint.empty ()) {
            route.enable_client (route_b_endpoint);
        }
        options.add_spot_mesh (e2e::spot_mesh)
          .use_registry_spot_resolver (e2e::route_channel)
          .set_routing_id (zlink::routing_id_t::from (node_rid))
          .enable_router (spot_router_endpoint)
          .enable_pub_sub (pubsub_endpoint);
        options.http ()
          .listen (http_endpoint)
          .map_health ("/health")
          .map_get<gateway_evidence_handler_t> ("/evidence")
          .map_post<gateway_publish_handler_t> ("/spot/publish");
    });
    return app.run (argc, argv);
}
