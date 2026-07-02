/* SPDX-License-Identifier: MPL-2.0 */

#include "Scenarios/dr_a1_basic_discovery_scenario.hpp"
#include "Scenarios/dr_a2_cluster_bridge_scenario.hpp"
#include "Scenarios/dr_a3_cluster_bridge_scenario.hpp"
#include "Scenarios/dr_a4_third_registry_scenario.hpp"
#include "Scenarios/dr_b1_late_start_registry_scenario.hpp"
#include "Scenarios/dr_b2_live_endpoint_continuity_scenario.hpp"
#include "Scenarios/dr_b3_peer_flapping_scenario.hpp"
#include "Scenarios/dr_c1_registry_down_scenario.hpp"
#include "Scenarios/dr_c2_registry_recovery_scenario.hpp"
#include "Scenarios/dr_c3_all_registry_outage_scenario.hpp"
#include "Scenarios/dr_d1_embedded_deployment_scenario.hpp"
#include "Scenarios/dr_d2_standalone_deployment_scenario.hpp"
#include "Scenarios/dr_d3_mixed_deployment_scenario.hpp"
#include "Scenarios/dr_d4_topology_query_scenario.hpp"
#include "Support/client_support.hpp"

#include <iostream>

namespace drha_client = zlink::framework::e2e::discovery_registry_ha::client;

int main ()
{
    const auto scenario = drha_client::env_or ("ZLINK_CPP_DRHA_SCENARIO", "DR-A1");
    if (scenario == "DR-A1") {
        drha_client::run_dr_a1_basic_discovery_scenario ();
    } else if (scenario == "DR-A2") {
        drha_client::run_dr_a2_cluster_bridge_scenario ();
    } else if (scenario == "DR-A3") {
        drha_client::run_dr_a3_cluster_bridge_scenario ();
    } else if (scenario == "DR-A4") {
        drha_client::run_dr_a4_third_registry_scenario ();
    } else if (scenario == "DR-B1") {
        drha_client::run_dr_b1_late_start_registry_scenario ();
    } else if (scenario == "DR-B2") {
        drha_client::run_dr_b2_live_endpoint_continuity_scenario ();
    } else if (scenario == "DR-B3") {
        drha_client::run_dr_b3_peer_flapping_scenario ();
    } else if (scenario == "DR-C1") {
        drha_client::run_dr_c1_registry_down_scenario ();
    } else if (scenario == "DR-C2") {
        drha_client::run_dr_c2_registry_recovery_scenario ();
    } else if (scenario == "DR-C3") {
        drha_client::run_dr_c3_all_registry_outage_scenario ();
    } else if (scenario == "DR-D1") {
        drha_client::run_dr_d1_embedded_deployment_scenario ();
    } else if (scenario == "DR-D2") {
        drha_client::run_dr_d2_standalone_deployment_scenario ();
    } else if (scenario == "DR-D3") {
        drha_client::run_dr_d3_mixed_deployment_scenario ();
    } else if (scenario == "DR-D4") {
        drha_client::run_dr_d4_topology_query_scenario ();
    } else {
        throw std::runtime_error ("Unsupported DiscoveryRegistryHa scenario: " + scenario);
    }
    std::cout << "discovery-registry-ha client scenario=" << scenario << " result=passed\n";
    return 0;
}
