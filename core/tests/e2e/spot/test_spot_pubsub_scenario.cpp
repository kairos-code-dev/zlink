/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <stdlib.h>
#include <string.h>

static bool should_run_spot_e2e_smoke_test (const char *name_)
{
    static const char *const smoke_cases[] = {"test_spot_peer_tcp",
                                              "test_spot_multi_publisher",
                                              "test_spot_node_direct_local_and_child_interop",
                                              "test_spot_node_discovery_direct_and_child_interop"};
    for (size_t i = 0; i < sizeof (smoke_cases) / sizeof (smoke_cases[0]); ++i) {
        if (strcmp (smoke_cases[i], name_) == 0)
            return true;
    }
    return false;
}

static bool should_run_spot_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    if (selected && *selected)
        return strcmp (selected, name_) == 0;

    const char *suite_mode = getenv ("ZLINK_TEST_SUITE_MODE");
    if (suite_mode && strcmp (suite_mode, "e2e") == 0)
        return should_run_spot_e2e_smoke_test (name_);

    return true;
}

int main (int, char **)
{
    setup_test_environment (600);

    UNITY_BEGIN ();
#define RUN_SPOT_TEST(name)                                                    \
    do {                                                                       \
        if (should_run_spot_test (#name))                                      \
            RUN_TEST (name);                                                   \
    } while (0)
    RUN_SPOT_TEST (test_spot_peer_ipc);
    RUN_SPOT_TEST (test_spot_peer_tcp);
    RUN_SPOT_TEST (test_spot_peer_ws);
    RUN_SPOT_TEST (test_spot_peer_tls);
    RUN_SPOT_TEST (test_spot_peer_wss);
    RUN_SPOT_TEST (test_spot_unified_wss_subscription_ready_first_delivery);
    RUN_SPOT_TEST (test_spot_multi_publisher);
    RUN_SPOT_TEST (test_spot_node_direct_local_and_child_interop);
    RUN_SPOT_TEST (test_spot_node_direct_remote_peer_mesh);
    RUN_SPOT_TEST (test_spot_node_direct_sub_option_inheritance_and_handler_conflict);
    RUN_SPOT_TEST (test_spot_node_direct_first_publish_race);
    RUN_SPOT_TEST (test_spot_sub_handler_basic);
    RUN_SPOT_TEST (test_spot_recv_callback_isolated_by_handle);
    RUN_SPOT_TEST (test_spot_recv_callback_isolated_by_service_with_discovery);
    RUN_SPOT_TEST (test_spot_facade_handler_receives_source_rid);
    RUN_SPOT_TEST (test_spot_node_discovery_direct_and_child_interop);
    RUN_SPOT_TEST (test_spot_mmorpg_zone_adjacency_scale_multi_node_discovery);
#undef RUN_SPOT_TEST
    return UNITY_END ();
}
