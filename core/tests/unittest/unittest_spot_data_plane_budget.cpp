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
      384,
      zlink::resolve_mesh_pub_sndhwm_default ("wss://127.0.0.1:9000", 2));
}
}

int main (int argc, char **argv)
{
    UNITY_BEGIN ();
    RUN_TEST (
      test_mesh_pub_budget_defaults_follow_transport_and_ready_peer_count);
    return UNITY_END ();
}
