/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::discovery_registry_ha::client
{

inline void run_dr_d4_topology_query_scenario ()
{
    const auto registry_url = env_or ("ZLINK_CPP_DRHA_REG1_URL");
    const auto probe_url = env_or ("ZLINK_CPP_DRHA_PROBE_URL");

    ensure (!registry_url.empty (), "ZLINK_CPP_DRHA_REG1_URL is required");
    ensure (!probe_url.empty (), "ZLINK_CPP_DRHA_PROBE_URL is required");

    post_json<topology_ready_wait_req_t, operation_status_t> (
      registry_url, "/registry/topology/wait", {.ready_count = 1});
    post_json<topology_ready_wait_req_t, operation_status_t> (
      probe_url, "/registry/topology/wait", {.ready_count = 1});

    auto local = get_json<topology_snapshot_t> (registry_url, "/registry/topology");
    auto remote = get_json<topology_snapshot_t> (probe_url, "/registry/topology");
    ensure (!local.entries.empty (), "DR-D4 local topology snapshot is empty");
    ensure (local.entries == remote.entries,
            "DR-D4 in-process and remote topology snapshots did not match");

    std::cout << "scenario DR-D4 passed\n";
}

} // namespace zlink::framework::e2e::discovery_registry_ha::client
