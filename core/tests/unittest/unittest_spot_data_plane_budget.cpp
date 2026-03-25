/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "services/spot/spot_data_plane_internal.hpp"

namespace
{
void test_mesh_pub_budget_defaults_follow_transport_and_ready_peer_count ()
{
    TEST_ASSERT_EQUAL_INT (
      64, zlink::resolve_mesh_pub_sndhwm_default ("tcp://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      768, zlink::resolve_mesh_pub_sndhwm_default ("tcp://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      64, zlink::resolve_mesh_pub_sndhwm_default ("tcp://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      64, zlink::resolve_mesh_pub_sndhwm_default ("ws://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      768, zlink::resolve_mesh_pub_sndhwm_default ("ws://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      64, zlink::resolve_mesh_pub_sndhwm_default ("ws://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      64, zlink::resolve_mesh_pub_sndhwm_default ("tls://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      768, zlink::resolve_mesh_pub_sndhwm_default ("tls://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      768, zlink::resolve_mesh_pub_sndhwm_default ("tls://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      64, zlink::resolve_mesh_pub_sndhwm_default ("wss://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      768, zlink::resolve_mesh_pub_sndhwm_default ("wss://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      256,
      zlink::resolve_mesh_pub_sndhwm_default ("wss://127.0.0.1:9000", 2));
}

void test_mesh_pub_budget_refresh_follows_ready_peer_count_changes ()
{
    TEST_ASSERT_TRUE (zlink::should_refresh_mesh_pub_budget (
      "tls://127.0.0.1:9000", 0, 1));
    TEST_ASSERT_TRUE (zlink::should_refresh_mesh_pub_budget (
      "wss://127.0.0.1:9000", 0, 2));
    TEST_ASSERT_TRUE (zlink::should_refresh_mesh_pub_budget (
      "tcp://127.0.0.1:9000", 0, 1));
    TEST_ASSERT_TRUE (zlink::should_refresh_mesh_pub_budget (
      "tcp://127.0.0.1:9000", 1, 2));
    TEST_ASSERT_FALSE (zlink::should_refresh_mesh_pub_budget (
      "tls://127.0.0.1:9000", 1, 2));
    TEST_ASSERT_FALSE (zlink::should_refresh_mesh_pub_budget (
      "wss://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::should_refresh_mesh_pub_budget (
      "ws://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::should_refresh_mesh_pub_budget (
      "tls://127.0.0.1:9000", 2, 2));
    TEST_ASSERT_FALSE (zlink::should_refresh_mesh_pub_budget (
      "wss://127.0.0.1:9000", 100, 100));
}

void test_ready_peer_count_is_clamped_to_connected_peers ()
{
    TEST_ASSERT_EQUAL_UINT (
      2, zlink::clamp_ready_peer_count (100, 2));
    TEST_ASSERT_EQUAL_UINT (
      1, zlink::clamp_ready_peer_count (1, 2));
    TEST_ASSERT_EQUAL_UINT (
      0, zlink::clamp_ready_peer_count (7, 0));
}
}

int main (int argc, char **argv)
{
    UNITY_BEGIN ();
    RUN_TEST (
      test_mesh_pub_budget_defaults_follow_transport_and_ready_peer_count);
    RUN_TEST (
      test_mesh_pub_budget_refresh_follows_ready_peer_count_changes);
    RUN_TEST (test_ready_peer_count_is_clamped_to_connected_peers);
    return UNITY_END ();
}
