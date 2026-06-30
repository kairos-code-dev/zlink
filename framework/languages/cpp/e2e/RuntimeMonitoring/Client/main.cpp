/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/mon_a1_socket_events_scenario.hpp"
#include "Scenarios/mon_a2_registry_events_scenario.hpp"
#include "Scenarios/mon_a3_spot_events_scenario.hpp"
#include "Scenarios/mon_a4_availability_transition_scenario.hpp"
#include "Scenarios/mon_a5_fixed_kinds_scenario.hpp"
#include "Scenarios/mon_b1_kind_filter_scenario.hpp"
#include "Scenarios/mon_b2_registration_validation_scenario.hpp"
#include "Scenarios/mon_c1_dispatch_failure_scenario.hpp"
#include "Scenarios/mon_d1_failure_recovery_scenario.hpp"

#include <zlink/framework.hpp>

#include <exception>
#include <iostream>
#include <memory>

namespace rm = zlink::framework::e2e::runtime_monitoring;
namespace rm_client = zlink::framework::e2e::runtime_monitoring::client;

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
            const auto scenario = rm_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "common");
            if (scenario == "mon-d1") {
                rm_client::run_mon_d1_failure_recovery_scenario (channels);
            } else {
                rm_client::run_mon_a1_socket_events_scenario (channels);
                rm_client::run_mon_a2_registry_events_scenario ();
                rm_client::run_mon_a3_spot_events_scenario ();
                rm_client::run_mon_a5_fixed_kinds_scenario ();
                rm_client::run_mon_a4_availability_transition_scenario ();
                rm_client::run_mon_b1_kind_filter_scenario ();
                rm_client::run_mon_c1_dispatch_failure_scenario (channels);
                rm_client::run_mon_b2_registration_validation_scenario ();
            }
            passed = true;
        }
        catch (const std::exception &ex) {
            std::cerr << "runtime-monitoring client failed: " << ex.what () << '\n';
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
    auto app = zlink::framework::app_t::create ();
    auto scenario = std::make_unique<scenario_service_t> (app);
    auto *scenario_result = scenario.get ();
    app.add_zlink_framework ([] (zlink::framework::zlink_framework_options_t &framework) {
        const auto scenario_name = rm_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "common");
        if (scenario_name != "mon-d1") {
            framework.use_discovery ().add_registry_endpoint (
              rm_client::env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER"));
        }
        auto channel = framework.add_client_server_channel (rm::profile_channel);
        if (const auto endpoint = rm_client::env_or ("ZLINK_CPP_E2E_DIRECT_CHANNEL_ENDPOINT");
            !endpoint.empty ()) {
            channel.enable_client (endpoint);
        } else {
            channel.enable_client ();
        }
    });
    app.add_hosted_service (std::move (scenario));
    const auto exit_code = app.run (argc, argv);
    if (exit_code != 0 || !scenario_result->passed) {
        return 1;
    }
    std::cout << "runtime-monitoring client result=passed" << '\n';
    return 0;
}
