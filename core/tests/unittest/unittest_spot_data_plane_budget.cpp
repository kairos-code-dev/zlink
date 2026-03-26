/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil_unity.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_budget.hpp"
#include "services/spot/spot_runtime.hpp"

namespace
{
void test_mesh_pub_budget_defaults_follow_transport_and_ready_peer_count ()
{
    TEST_ASSERT_EQUAL_INT (
      64,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      768,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      64,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tcp://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      64,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      768,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      64,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "ws://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      64,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      768,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      768,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "tls://127.0.0.1:9000", 2));

    TEST_ASSERT_EQUAL_INT (
      64,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "wss://127.0.0.1:9000", 0));
    TEST_ASSERT_EQUAL_INT (
      768,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "wss://127.0.0.1:9000", 1));
    TEST_ASSERT_EQUAL_INT (
      128,
      zlink::spot_mesh_pub_budget_t::resolve_default (
        "wss://127.0.0.1:9000", 2));
}

void test_mesh_pub_budget_refresh_follows_ready_peer_count_changes ()
{
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tls://127.0.0.1:9000", 0, 1));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "wss://127.0.0.1:9000", 0, 2));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tcp://127.0.0.1:9000", 0, 1));
    TEST_ASSERT_TRUE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tcp://127.0.0.1:9000", 1, 2));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tls://127.0.0.1:9000", 1, 2));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tls://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "wss://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "ws://127.0.0.1:9000", 2, 100));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
      "tls://127.0.0.1:9000", 2, 2));
    TEST_ASSERT_FALSE (zlink::spot_mesh_pub_budget_t::should_refresh (
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

void test_mesh_pub_budget_hint_updates_private_runtime_owner ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "wss://127.0.0.1:9000";

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 1));
    TEST_ASSERT_EQUAL_UINT (
      1, zlink::mesh_pub_ready_peer_count (&runtime.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (
      1, zlink::mesh_pub_budget_version (&runtime.mesh_peer_state));

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_UINT (
      2, zlink::mesh_pub_ready_peer_count (&runtime.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (
      2, zlink::mesh_pub_budget_version (&runtime.mesh_peer_state));

    zlink::spot_mesh_pub_budget_t::reset_runtime_state (&runtime);
    TEST_ASSERT_EQUAL_UINT (
      0, zlink::mesh_pub_ready_peer_count (&runtime.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (
      3, zlink::mesh_pub_budget_version (&runtime.mesh_peer_state));
}

void test_mesh_pub_budget_runtime_owner_uses_bound_endpoint ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "wss://127.0.0.1:9000";

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_INT (
      128, zlink::spot_mesh_pub_budget_t::resolve_runtime_default (&runtime));
    TEST_ASSERT_EQUAL_INT (
      128,
      zlink::spot_mesh_pub_budget_t::resolve_initial_bind_sndhwm (
        &runtime, runtime.bound_endpoint));

    zlink::spot_mesh_pub_budget_t::reset_runtime_state (&runtime);
    TEST_ASSERT_EQUAL_UINT (
      0, zlink::mesh_pub_ready_peer_count (&runtime.mesh_peer_state));
}

void test_mesh_pub_budget_runtime_owner_tracks_ready_count_changes ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.bound_endpoint = "tls://127.0.0.1:9000";

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 1));
    TEST_ASSERT_EQUAL_UINT64 (
      1, zlink::mesh_pub_budget_version (&runtime.mesh_peer_state));

    TEST_ASSERT_TRUE (
      zlink::spot_mesh_pub_budget_t::publish_ready_hint (&runtime, 2));
    TEST_ASSERT_EQUAL_UINT (
      2, zlink::mesh_pub_ready_peer_count (&runtime.mesh_peer_state));
    TEST_ASSERT_EQUAL_UINT64 (
      1, zlink::mesh_pub_budget_version (&runtime.mesh_peer_state));
    TEST_ASSERT_EQUAL_INT (
      768, zlink::spot_mesh_pub_budget_t::resolve_runtime_default (&runtime));
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
    RUN_TEST (test_mesh_pub_budget_hint_updates_private_runtime_owner);
    RUN_TEST (test_mesh_pub_budget_runtime_owner_uses_bound_endpoint);
    RUN_TEST (
      test_mesh_pub_budget_runtime_owner_tracks_ready_count_changes);
    return UNITY_END ();
}
