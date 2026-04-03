/* SPDX-License-Identifier: MPL-2.0 */

#include "spot_pubsub_scenario_shared.hpp"

#include <stdlib.h>
#include <string.h>

static bool should_run_spot_e2e_smoke_test (const char *name_)
{
    static const char *const smoke_cases[] = {
      "test_spot_peer_tcp",
      "test_spot_multi_publisher",
      "test_spot_node_direct_local_and_child_interop",
      "test_spot_sub_handler_basic",
      "test_spot_recv_callback_isolated_by_handle",
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
    RUN_SPOT_TEST (test_spot_peer_tcp);
    RUN_SPOT_TEST (test_spot_multi_publisher);
    RUN_SPOT_TEST (test_spot_node_direct_local_and_child_interop);
    RUN_SPOT_TEST (test_spot_sub_handler_basic);
    RUN_SPOT_TEST (test_spot_recv_callback_isolated_by_handle);
    RUN_SPOT_TEST (test_spot_node_discovery_direct_and_child_interop);
#undef RUN_SPOT_TEST
    return UNITY_END ();
}
