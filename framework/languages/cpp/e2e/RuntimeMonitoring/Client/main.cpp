/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/mon_a1_socket_events_scenario.hpp"
#include "Scenarios/mon_a2_location_events_scenario.hpp"
#include "Scenarios/mon_a3_spot_events_scenario.hpp"
#include "Scenarios/mon_a4_availability_transition_scenario.hpp"
#include "Scenarios/mon_a5_fixed_kinds_scenario.hpp"
#include "Scenarios/mon_b1_kind_filter_scenario.hpp"
#include "Scenarios/mon_b2_registration_validation_scenario.hpp"
#include "Scenarios/mon_c1_dispatch_failure_scenario.hpp"
#include "Scenarios/mon_d1_failure_recovery_scenario.hpp"
#include "Support/client_options.hpp"

#include <exception>
#include <iostream>

namespace rm_client = zlink::framework::e2e::runtime_monitoring::client;

namespace
{

using rm_client::client_options_t;

int run_scenarios (const client_options_t &client_options)
{
    if (client_options.scenario == "mon-d1") {
        rm_client::run_mon_d1_failure_recovery_scenario (client_options);
    } else {
        rm_client::run_mon_a1_socket_events_scenario (client_options);
        rm_client::run_mon_a2_location_events_scenario (client_options);
        rm_client::run_mon_a3_spot_events_scenario (client_options);
        rm_client::run_mon_a5_fixed_kinds_scenario (client_options);
        rm_client::run_mon_a4_availability_transition_scenario (client_options);
        rm_client::run_mon_b1_kind_filter_scenario (client_options);
        rm_client::run_mon_c1_dispatch_failure_scenario (client_options);
        rm_client::run_mon_b2_registration_validation_scenario (client_options);
    }
    return 0;
}

} // namespace

int main (int argc, char **argv)
{
    (void) argc;
    (void) argv;
    auto client_options = rm_client::read_client_options ();
    try {
        run_scenarios (client_options);
    }
    catch (const std::exception &ex) {
        std::cerr << "runtime-monitoring client failed: " << ex.what () << '\n';
        return 1;
    }
    std::cout << "runtime-monitoring client result=passed" << '\n';
    return 0;
}
