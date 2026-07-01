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
#include "Support/client_options.hpp"

#include <zlink/framework.hpp>

#include <exception>
#include <iostream>
#include <memory>
#include <utility>

namespace rm = zlink::framework::e2e::runtime_monitoring;
namespace rm_client = zlink::framework::e2e::runtime_monitoring::client;

namespace
{

using rm_client::client_options_t;

class scenario_service_t final : public zlink::framework::hosted_service_t
{
  public:
    scenario_service_t (zlink::framework::app_t &app, client_options_t options)
      : _app (app), _options (std::move (options))
    {
    }

    void start (zlink::framework::service_provider_t &services) override
    {
        try {
            auto &channels = services.get_required<zlink::framework::channel_client_t> ();
            if (_options.scenario == "mon-d1") {
                rm_client::run_mon_d1_failure_recovery_scenario (channels, _options);
            } else {
                rm_client::run_mon_a1_socket_events_scenario (channels, _options);
                rm_client::run_mon_a2_registry_events_scenario (_options);
                rm_client::run_mon_a3_spot_events_scenario (_options);
                rm_client::run_mon_a5_fixed_kinds_scenario (_options);
                rm_client::run_mon_a4_availability_transition_scenario (_options);
                rm_client::run_mon_b1_kind_filter_scenario (_options);
                rm_client::run_mon_c1_dispatch_failure_scenario (channels, _options);
                rm_client::run_mon_b2_registration_validation_scenario (_options);
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
    client_options_t _options;
};

} // namespace

int main (int argc, char **argv)
{
    auto app = zlink::framework::app_t::create ();
    auto client_options = rm_client::read_client_options ();
    auto scenario = std::make_unique<scenario_service_t> (app, client_options);
    auto *scenario_result = scenario.get ();
    app.add_zlink_framework ([client_options] (zlink::framework::zlink_framework_options_t &framework) {
        if (client_options.scenario != "mon-d1") {
            framework.use_discovery ().add_registry_endpoint (client_options.registry_router_endpoint);
        }
        if (!client_options.log_dir.empty ()) {
            const std::string label =
              client_options.scenario == "mon-d1" ? "client-d1" : "client";
            framework.configure_dispatch ()
              .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
              .trace_log_file (client_options.log_dir + "/" + label + "-flow.log")
              .trace_label ("cpp-mon-" + label);
        }
        auto channel = framework.add_client_server_channel (rm::profile_channel);
        if (!client_options.direct_channel_endpoint.empty ()) {
            channel.enable_client (client_options.direct_channel_endpoint);
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
