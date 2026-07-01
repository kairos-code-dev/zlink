/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/rl_a1_provider_restart_scenario.hpp"
#include "Scenarios/rl_a2_provider_endpoint_remap_scenario.hpp"
#include "Scenarios/rl_a3_reconnect_storm_scenario.hpp"
#include "Scenarios/rl_a4_drain_and_green_endpoint_scenario.hpp"
#include "Scenarios/rl_a5_provider_flapping_scenario.hpp"
#include "Scenarios/rl_b1_cancellation_cleanup_scenario.hpp"
#include "Scenarios/rl_b2_crash_during_inflight_scenario.hpp"
#include "Scenarios/rl_b3_graceful_shutdown_scenario.hpp"
#include "Scenarios/rl_b4_runtime_drain_scenario.hpp"
#include "Scenarios/rl_b5_drain_inflight_scenario.hpp"
#include "Scenarios/rl_b6_gray_fault_scenario.hpp"
#include "Scenarios/rl_c1_client_host_lifecycle_scenario.hpp"
#include "Scenarios/rl_c2_topology_recovery_scenario.hpp"
#include "Scenarios/rl_c3_node_pause_recovery_scenario.hpp"
#include "Scenarios/rl_c4_registry_outage_scenario.hpp"
#include "Scenarios/rl_d1_high_fanout_scenario.hpp"
#include "Scenarios/rl_d2_observer_fault_scenario.hpp"
#include "Scenarios/rl_d3_dispatch_error_evidence_scenario.hpp"
#include "Scenarios/rl_d4_missing_request_handler_scenario.hpp"
#include "Scenarios/rl_d5_mixed_burst_scenario.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace rl = zlink::framework::e2e::resilience_lifecycle;
namespace rl_client = zlink::framework::e2e::resilience_lifecycle::client;

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
            const auto scenario = rl_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "rl-a3");
            if (scenario == "inflight-crash") {
                rl_client::run_inflight_crash_scenario (channels);
            } else if (scenario == "registry-outage") {
                rl_client::run_registry_outage_scenario (channels);
            } else if (scenario == "registry-recovered") {
                rl_client::run_registry_recovered_scenario (channels);
            } else if (scenario == "observer-fault") {
                rl_client::run_observer_fault_scenario (channels);
            } else if (scenario == "rl-a1") {
                rl_client::run_rl_a1_provider_restart_scenario (channels);
            } else if (scenario == "rl-a2") {
                rl_client::run_rl_a2_provider_endpoint_remap_scenario (channels);
            } else if (scenario == "rl-a3") {
                rl_client::run_rl_a3_reconnect_storm_probe (channels);
            } else if (scenario == "rl-a4") {
                rl_client::run_rl_a4_drain_and_green_endpoint_scenario (channels);
            } else if (scenario == "rl-b4") {
                rl_client::run_rl_b4_runtime_drain_scenario (channels);
            } else if (scenario == "rl-b5") {
                rl_client::run_rl_b5_drain_inflight_scenario (channels);
            } else if (scenario == "rl-a5") {
                rl_client::run_rl_a5_provider_flapping_probe (channels);
            } else if (scenario == "rl-b1") {
                rl_client::run_rl_b1_cancellation_cleanup_scenario (channels);
            } else if (scenario == "rl-b3") {
                rl_client::run_rl_b3_graceful_shutdown_scenario (channels);
            } else if (scenario == "rl-b6") {
                rl_client::run_rl_b6_gray_fault_scenario (channels);
            } else if (scenario == "rl-c1") {
                rl_client::run_rl_c1_client_host_lifecycle_probe (channels);
            } else if (scenario == "rl-c2") {
                rl_client::run_rl_c2_topology_recovery_probe (channels);
            } else if (scenario == "rl-c3") {
                rl_client::run_rl_c3_node_pause_recovery_probe (channels);
            } else if (scenario == "rl-d3") {
                rl_client::run_rl_d3_dispatch_error_evidence_scenario (channels);
            } else if (scenario == "rl-d4") {
                rl_client::run_rl_d4_missing_request_handler_scenario ();
            } else if (scenario == "rl-d5") {
                rl_client::run_rl_d5_mixed_burst_scenario ();
            } else {
                throw std::runtime_error ("unknown scenario " + scenario);
            }
            passed = true;
        }
        catch (const std::exception &error) {
            std::cerr << "resilience-lifecycle scenario failed: " << error.what () << "\n";
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
    const auto client_options = rl_client::read_client_options ();

    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.logging ()
      .use_file (client_options.log_dir + "/client.log")
      .set_min_level (zlink::framework::log_level_t::debug);
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (client_options.log_dir + "/client-flow.log")
          .trace_label ("cpp-rl-client");
        rl_client::configure_common_codecs (options.codecs ());
        options.use_discovery ().add_registry_endpoint (client_options.registry_router);
        options.add_client_server_channel (rl::api_channel).enable_client ();
        options.add_client_server_channel ("resilience.lifecycle.api.manual")
          .enable_client (client_options.api_a_endpoint);
        options.add_client_server_channel ("resilience.lifecycle.api.manual.multi")
          .enable_client (client_options.api_a_endpoint)
          .enable_client (client_options.api_b_endpoint);
        options.add_client_server_channel ("resilience.lifecycle.api.manual.weighted")
          .enable_client (client_options.api_a_endpoint)
          .enable_client (client_options.api_b_endpoint);
        options.add_client_server_channel ("resilience.lifecycle.api.manual.max")
          .enable_client (client_options.api_a_endpoint);
        options.add_client_server_channel ("resilience.lifecycle.api.manual.backpressure")
          .enable_client (client_options.api_a_endpoint)
          .client_send_high_water_mark (zlink::message_count_t::value (2))
          .client_receive_high_water_mark (zlink::message_count_t::value (2));
        options.add_client_server_channel ("resilience.lifecycle.api.manual.b")
          .enable_client (client_options.api_b_endpoint);
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        return 1;
    }
    std::cout << "resilience-lifecycle client result=passed\n";
    return 0;
}
