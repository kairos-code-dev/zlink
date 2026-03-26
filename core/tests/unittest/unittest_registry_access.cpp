/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <string.h>
#include <unity.h>

void setUp ()
{
}

void tearDown ()
{
}

namespace
{
void test_registry_status_snapshot_tracks_configured_state ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);

    zlink_registry_status_t status;
    memset (&status, 0, sizeof (status));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_status_snapshot (registry, &status));
    TEST_ASSERT_EQUAL_UINT32 (0, status.registry_id);
    TEST_ASSERT_EQUAL_INT (ZLINK_REGISTRY_STATE_IDLE, status.state);
    TEST_ASSERT_EQUAL_UINT32 (0, status.peer_registry_count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_id (registry, 42));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_add_peer (registry, "ws://peer-a"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_add_peer (registry, "wss://peer-b"));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_registry_add_peer (registry, "inproc://peer-c"));
    TEST_ASSERT_EQUAL_INT (EPROTONOSUPPORT, errno);

    memset (&status, 0, sizeof (status));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_status_snapshot (registry, &status));
    TEST_ASSERT_EQUAL_UINT32 (42, status.registry_id);
    TEST_ASSERT_EQUAL_INT (ZLINK_REGISTRY_STATE_IDLE, status.state);
    TEST_ASSERT_EQUAL_UINT32 (2, status.peer_registry_count);
    TEST_ASSERT_EQUAL_UINT32 (0, status.connected_peer_registry_count);
    TEST_ASSERT_EQUAL_UINT32 (0, status.topology_entry_count);
    TEST_ASSERT_TRUE ('\0' == status.bind_endpoint[0]);
    TEST_ASSERT_TRUE (status.last_changed_ms > 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}
} // namespace

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_registry_status_snapshot_tracks_configured_state);
    return UNITY_END ();
}
