/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "services/spot/runtime/spot_runtime.hpp"

namespace
{
void test_spot_runtime_control_task_state_is_single_owner ()
{
    zlink::spot_runtime_t runtime (NULL);

    TEST_ASSERT_EQUAL_UINT64 (0, runtime.control_task_id ());
    TEST_ASSERT_TRUE (runtime.try_set_control_task_id (17));
    TEST_ASSERT_EQUAL_UINT64 (17, runtime.control_task_id ());
    TEST_ASSERT_FALSE (runtime.try_set_control_task_id (19));
    TEST_ASSERT_EQUAL_UINT64 (17, runtime.control_task_id ());
    TEST_ASSERT_EQUAL_UINT64 (17, runtime.clear_control_task_id ());
    TEST_ASSERT_EQUAL_UINT64 (0, runtime.control_task_id ());
    TEST_ASSERT_EQUAL_UINT64 (0, runtime.clear_control_task_id ());
}

void test_spot_runtime_connected_peer_version_tracks_changes_once ()
{
    zlink::spot_runtime_t runtime (NULL);

    TEST_ASSERT_EQUAL_UINT64 (0, runtime.connected_peer_version_seen ());
    TEST_ASSERT_FALSE (runtime.note_connected_peer_version (0));
    TEST_ASSERT_TRUE (runtime.note_connected_peer_version (3));
    TEST_ASSERT_EQUAL_UINT64 (3, runtime.connected_peer_version_seen ());
    TEST_ASSERT_FALSE (runtime.note_connected_peer_version (3));
    TEST_ASSERT_TRUE (runtime.note_connected_peer_version (5));
    TEST_ASSERT_EQUAL_UINT64 (5, runtime.connected_peer_version_seen ());
}
}

int main (int argc, char **argv)
{
    UNITY_BEGIN ();
    RUN_TEST (test_spot_runtime_control_task_state_is_single_owner);
    RUN_TEST (test_spot_runtime_connected_peer_version_tracks_changes_once);
    return UNITY_END ();
}
