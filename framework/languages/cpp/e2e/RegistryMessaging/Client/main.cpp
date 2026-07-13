/* SPDX-License-Identifier: FSL-1.1-ALv2 */

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

#include <iostream>
#include <stdexcept>
#include <string>

namespace rm_client = zlink::framework::e2e::registry_messaging::client;

int main ()
{
    const auto scenario = rm_client::env_or ("ZLINK_CPP_E2E_SCENARIO", "common");
    if (scenario == "rm-a1") {
        rm_client::run_rm_a1_discovery_request_scenario ();
    } else if (scenario == "rm-a2") {
        rm_client::run_rm_a2_manual_endpoint_scenario ();
    } else if (scenario == "common") {
        rm_client::run_rm_a1_discovery_request_scenario ();
        rm_client::run_rm_c1_request_send_scenario ();
        rm_client::run_rm_a2_manual_endpoint_scenario ();
        rm_client::run_rm_c3_multi_provider_distribution_scenario ();
        rm_client::run_rm_a6_multiple_channels_scenario ();
        rm_client::run_rm_c8_payload_round_trip_scenario ();
        rm_client::run_rm_c2_targeted_route_scenario ();
        rm_client::run_rm_c4_timeout_isolation_scenario ();
        rm_client::run_rm_c5_missing_packet_scenario ();
    } else if (scenario == "rm-a6") {
        rm_client::run_rm_a6_multiple_channels_scenario ();
    } else if (scenario == "rm-c1") {
        rm_client::run_rm_c1_request_send_scenario ();
    } else if (scenario == "rm-c2") {
        rm_client::run_rm_c2_targeted_route_scenario ();
    } else if (scenario == "rm-c3") {
        rm_client::run_rm_c3_multi_provider_distribution_scenario ();
    } else if (scenario == "rm-c4" || scenario == "timeout-cleanup") {
        rm_client::run_rm_c4_timeout_isolation_scenario ();
    } else if (scenario == "rm-c5") {
        rm_client::run_rm_c5_missing_packet_scenario ();
    } else if (scenario == "rm-b1" || scenario == "scale-out") {
        rm_client::run_rm_b1_scale_out_scenario ();
    } else if (scenario == "rm-b2" || scenario == "scale-in") {
        rm_client::run_rm_b2_scale_in_scenario ();
    } else if (scenario == "rm-a4" || scenario == "failover") {
        rm_client::run_rm_a4_same_rid_failover_scenario ();
    } else if (scenario == "rm-c7" || scenario == "weighted") {
        rm_client::run_rm_c7_weighted_provider_scenario ();
    } else if (scenario == "rm-c8") {
        rm_client::run_rm_c8_payload_round_trip_scenario ();
    } else if (scenario == "rm-c8-max" || scenario == "max-size") {
        rm_client::run_rm_c8_max_message_size_scenario ();
    } else if (scenario == "rm-c9" || scenario == "backpressure") {
        rm_client::run_rm_c9_backpressure_scenario ();
    } else {
        throw std::runtime_error ("unknown scenario " + scenario);
    }
    std::cout << "registry-messaging e2e result=passed\n";
    return 0;
}
