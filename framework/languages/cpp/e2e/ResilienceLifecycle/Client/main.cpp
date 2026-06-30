/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/rm_a1_discovery_request_scenario.hpp"
#include "Scenarios/rm_a2_manual_endpoint_scenario.hpp"
#include "Scenarios/rm_a4_same_rid_failover_scenario.hpp"
#include "Scenarios/rm_a6_multiple_channels_scenario.hpp"
#include "Scenarios/rm_b1_scale_out_scenario.hpp"
#include "Scenarios/rm_b2_scale_in_scenario.hpp"
#include "Scenarios/rm_c1_request_send_scenario.hpp"
#include "Scenarios/rm_c2_targeted_route_scenario.hpp"
#include "Scenarios/rm_c3_multi_provider_distribution_scenario.hpp"
#include "Scenarios/rm_c4_timeout_isolation_scenario.hpp"
#include "Scenarios/rm_c5_missing_packet_scenario.hpp"
#include "Scenarios/rm_c7_weighted_provider_scenario.hpp"
#include "Scenarios/rm_c8_payload_round_trip_scenario.hpp"
#include "Scenarios/rm_c9_backpressure_scenario.hpp"
#include "Scenarios/rl_a3_reconnect_storm_scenario.hpp"
#include "Scenarios/rl_b2_crash_during_inflight_scenario.hpp"
#include "Scenarios/rl_b4_runtime_drain_scenario.hpp"
#include "Scenarios/rl_d1_high_fanout_scenario.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace rm = zlink::framework::e2e::registry_messaging;
namespace rm_client = zlink::framework::e2e::registry_messaging::client;

namespace
{

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit scenario_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto &channels = services.get_required<zlink::framework::channel_client_t> ();
            auto &routes = services.get_required<zlink::framework::route_client_t> ();
            const auto scenario = rm_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "common");
            if (scenario == "rm-a1") {
                rm_client::run_rm_a1_discovery_request_scenario (channels);
            } else if (scenario == "rm-a2") {
                rm_client::run_rm_a2_manual_endpoint_scenario (channels);
            } else if (scenario == "common") {
                rm_client::run_rm_a1_discovery_request_scenario (channels);
                rm_client::run_rm_c1_request_send_scenario (channels);
                rm_client::run_rm_a2_manual_endpoint_scenario (channels);
                rm_client::run_rm_c3_multi_provider_distribution_scenario (channels);
                rm_client::run_rm_a6_multiple_channels_scenario (channels);
                rm_client::run_rm_c8_payload_round_trip_scenario (channels);
                rm_client::run_rm_c2_targeted_route_scenario (routes);
                rm_client::run_rm_c4_timeout_isolation_scenario (channels);
                rm_client::run_rm_c5_missing_packet_scenario (channels);
            } else if (scenario == "rm-a6") {
                rm_client::run_rm_a6_multiple_channels_scenario (channels);
            } else if (scenario == "rm-c1") {
                rm_client::run_rm_c1_request_send_scenario (channels);
            } else if (scenario == "rm-c2") {
                rm_client::run_rm_c2_targeted_route_scenario (routes);
            } else if (scenario == "rm-c3") {
                rm_client::run_rm_c3_multi_provider_distribution_scenario (channels);
            } else if (scenario == "rm-c4" || scenario == "timeout-cleanup") {
                rm_client::run_rm_c4_timeout_isolation_scenario (channels);
            } else if (scenario == "rm-c5") {
                rm_client::run_rm_c5_missing_packet_scenario (channels);
            } else if (scenario == "rm-b1" || scenario == "scale-out") {
                rm_client::run_rm_b1_scale_out_scenario (channels);
            } else if (scenario == "rm-b2" || scenario == "scale-in") {
                rm_client::run_rm_b2_scale_in_scenario (channels);
            } else if (scenario == "rm-a4" || scenario == "failover") {
                rm_client::run_rm_a4_same_rid_failover_scenario (channels);
            } else if (scenario == "rm-c7" || scenario == "weighted") {
                rm_client::run_rm_c7_weighted_provider_scenario (channels);
            } else if (scenario == "rm-c8") {
                rm_client::run_rm_c8_payload_round_trip_scenario (channels);
            } else if (scenario == "rm-c8-max" || scenario == "max-size") {
                rm_client::run_rm_c8_max_message_size_scenario (channels);
            } else if (scenario == "rm-c9" || scenario == "backpressure") {
                rm_client::run_rm_c9_backpressure_scenario (channels);
            } else if (scenario == "quick") {
                rm_client::run_quick_resilience_scenario (channels);
            } else if (scenario == "inflight-crash") {
                rm_client::run_inflight_crash_scenario (channels);
            } else if (scenario == "drain-restore") {
                rm_client::run_drain_restore_scenario (channels);
            } else if (scenario == "resilience-stress") {
                rm_client::run_resilience_stress_scenario (channels);
            } else {
                throw std::runtime_error ("unknown scenario " + scenario);
            }
            passed = true;
        }
        catch (const std::exception &error) {
            std::cerr << "registry-messaging scenario failed: " << error.what () << "\n";
        }
        _app.stop ();
    }

    void stop () noexcept override {}

    bool passed = false;

  private:
    zlink::framework::app_t &_app;
};

} // namespace

int main (int argc, char **argv)
{
    const auto log_dir = rm_client::env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    const auto registry_router = rm_client::env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto api_a_endpoint = rm_client::env_or ("ZLINK_CPP_E2E_API_A_ENDPOINT");
    const auto api_b_endpoint = rm_client::env_or ("ZLINK_CPP_E2E_API_B_ENDPOINT");
    const auto route_a_endpoint = rm_client::env_or ("ZLINK_CPP_E2E_ROUTE_A_ENDPOINT");
    const auto route_b_endpoint = rm_client::env_or ("ZLINK_CPP_E2E_ROUTE_B_ENDPOINT");
    const auto scenario_name = rm_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "common");

    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.logging ()
      .use_file (log_dir + "/client.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/client-flow.log")
          .trace_label ("cpp-rm-client");
        rm_client::configure_common_codecs (options.codecs ());
        if (scenario_name != "rm-a2") {
            options.use_discovery ().add_registry_endpoint (registry_router);
            options.add_client_server_channel (rm::api_channel).enable_client ();
            options.add_client_server_channel (rm::workflow_channel).enable_client ();
        }
        options.add_client_server_channel ("registry.messaging.api.manual")
          .enable_client (api_a_endpoint);
        options.add_client_server_channel ("registry.messaging.api.manual.multi")
          .enable_client (api_a_endpoint)
          .enable_client (api_b_endpoint);
        options.add_client_server_channel ("registry.messaging.api.manual.weighted")
          .enable_client (api_a_endpoint)
          .enable_client (api_b_endpoint);
        options.add_client_server_channel ("registry.messaging.api.manual.max")
          .enable_client (api_a_endpoint);
        options.add_client_server_channel ("registry.messaging.api.manual.backpressure")
          .enable_client (api_a_endpoint)
          .client_send_high_water_mark (zlink::message_count_t::value (2))
          .client_receive_high_water_mark (zlink::message_count_t::value (2));
        options.add_client_server_channel ("registry.messaging.api.manual.b")
          .enable_client (api_b_endpoint);
        options.add_route_mesh (rm::route_channel)
          .enable_server (rm_client::env_or ("ZLINK_CPP_E2E_CLIENT_ROUTE_ENDPOINT"))
          .set_routing_id (zlink::routing_id_t::from (std::string ("client")))
          .enable_client (route_a_endpoint)
          .enable_client (route_b_endpoint);
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        return 1;
    }
    std::cout << "registry-messaging e2e result=passed\n";
    return 0;
}
