/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane_internal.hpp"
#include "utils/macros.hpp"
#include "services/spot/spot_runtime.hpp"

#include "unity.h"

#include <string.h>

namespace
{
void test_mesh_xsub_monitor_ready_zero_clears_connected_peer ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_ready_peer_count.store (1);
    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 0;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000", sizeof (raw.remote_addr) - 1);

    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_EQUAL_UINT (
      1, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (
      1, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (0, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_ready_count_growth_marks_connected ()
{
    zlink::spot_runtime_t runtime (NULL);
    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000", sizeof (raw.remote_addr) - 1);

    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_EQUAL_UINT (
      1, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (
      1, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_disconnect_clears_connected_peer ()
{
    zlink::spot_runtime_t runtime (NULL);
    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000", sizeof (raw.remote_addr) - 1);

    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);

    raw.event = ZLINK_EVENT_DISCONNECTED;
    raw.value = 0;
    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_TRUE (runtime.mesh_peer_state.connected_endpoints.empty ());
    TEST_ASSERT_EQUAL_UINT (
      0, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (2, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_ready_count_changes_without_rewriting_membership ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9001");
    runtime.mesh_peer_state.connected_ready_peer_count.store (2);

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9001", sizeof (raw.remote_addr) - 1);

    bool endpoint_membership_changed = true;
    TEST_ASSERT_FALSE (zlink::sync_mesh_peer_monitor_state (
      &runtime.mesh_peer_state, raw, &endpoint_membership_changed));
    TEST_ASSERT_EQUAL_UINT (
      2, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (
      2, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_FALSE (endpoint_membership_changed);
    TEST_ASSERT_EQUAL_UINT64 (0, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_ready_growth_reports_endpoint_membership_change ()
{
    zlink::spot_mesh_peer_state_t state;

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000",
             sizeof (raw.remote_addr) - 1);

    bool endpoint_membership_changed = false;
    TEST_ASSERT_TRUE (
      zlink::sync_mesh_peer_monitor_state (&state, raw,
                                           &endpoint_membership_changed));
    TEST_ASSERT_TRUE (endpoint_membership_changed);
    TEST_ASSERT_EQUAL_UINT (1, state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (1, state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, state.version.load ());
}

void test_mesh_xsub_monitor_ready_positive_keeps_endpoint_present ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9000");

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9001",
             sizeof (raw.remote_addr) - 1);

    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_EQUAL_UINT (
      2, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_TRUE (runtime.mesh_peer_state.connected_endpoints.count (
                        "wss://127.0.0.1:9001")
                      == 1);
    TEST_ASSERT_EQUAL_UINT (
      2, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, runtime.mesh_peer_state.version.load ());
}

void test_mesh_xsub_monitor_same_ready_count_does_not_bump_version ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_endpoints.insert ("wss://127.0.0.1:9001");

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));
    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 2;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9002",
             sizeof (raw.remote_addr) - 1);

    zlink::spot_data_plane_protocol_t::sync_mesh_xsub_connected_endpoint (
      &runtime, raw);
    TEST_ASSERT_EQUAL_UINT (
      3, runtime.mesh_peer_state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (
      3, runtime.mesh_peer_state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, runtime.mesh_peer_state.version.load ());
}

void test_explicit_disconnect_updates_private_mesh_peer_state ()
{
    zlink::spot_mesh_peer_state_t state;
    state.connected_endpoints.insert ("wss://127.0.0.1:9000");
    state.connected_endpoints.insert ("wss://127.0.0.1:9001");
    state.connected_ready_peer_count.store (2);

    TEST_ASSERT_TRUE (zlink::remove_connected_mesh_peer_endpoint (
      &state, "wss://127.0.0.1:9000"));
    TEST_ASSERT_EQUAL_UINT (1, state.connected_endpoints.size ());
    TEST_ASSERT_EQUAL_UINT (2, state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (1, state.version.load ());

    TEST_ASSERT_TRUE (zlink::remove_connected_mesh_peer_endpoint (
      &state, "wss://127.0.0.1:9001"));
    TEST_ASSERT_TRUE (state.connected_endpoints.empty ());
    TEST_ASSERT_EQUAL_UINT (0, state.connected_ready_peer_count.load ());
    TEST_ASSERT_EQUAL_UINT64 (2, state.version.load ());
}

void test_ready_endpoint_helper_tracks_endpoint_local_readiness ()
{
    std::set<std::string> endpoints;
    endpoints.insert ("wss://127.0.0.1:9000");
    endpoints.insert ("wss://127.0.0.1:9001");

    zlink_monitor_event_t raw;
    memset (&raw, 0, sizeof (raw));

    raw.event = ZLINK_EVENT_CONNECTION_READY;
    raw.value = 1;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9001",
             sizeof (raw.remote_addr) - 1);
    TEST_ASSERT_FALSE (
      zlink::sync_monitor_ready_endpoint (&endpoints, raw));
    TEST_ASSERT_EQUAL_UINT (2, endpoints.size ());
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9001") == 1);

    raw.value = 0;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9000",
             sizeof (raw.remote_addr) - 1);
    TEST_ASSERT_FALSE (
      zlink::sync_monitor_ready_endpoint (&endpoints, raw));
    TEST_ASSERT_EQUAL_UINT (2, endpoints.size ());
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9001") == 1);
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9000") == 1);

    raw.value = 2;
    strncpy (raw.remote_addr, "wss://127.0.0.1:9002",
             sizeof (raw.remote_addr) - 1);
    TEST_ASSERT_TRUE (
      zlink::sync_monitor_ready_endpoint (&endpoints, raw));
    TEST_ASSERT_EQUAL_UINT (3, endpoints.size ());
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9001") == 1);
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9000") == 1);
    TEST_ASSERT_TRUE (endpoints.count ("wss://127.0.0.1:9002") == 1);
}

void test_bootstrap_descriptor_republish_stops_after_ready_topology_stabilizes ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("tls://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_ready_peer_count.store (1);
    runtime.mesh_peer_state.version.store (7);

    TEST_ASSERT_TRUE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, false, 7));
    TEST_ASSERT_TRUE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, UINT64_MAX));
    TEST_ASSERT_FALSE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, 7));
    TEST_ASSERT_FALSE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, 7));
}

void test_bootstrap_descriptor_republish_resumes_after_topology_change ()
{
    zlink::spot_runtime_t runtime (NULL);
    runtime.mesh_peer_state.connected_endpoints.insert ("tls://127.0.0.1:9000");
    runtime.mesh_peer_state.connected_ready_peer_count.store (1);
    runtime.mesh_peer_state.version.store (3);

    TEST_ASSERT_FALSE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, 3));

    runtime.mesh_peer_state.version.store (4);
    TEST_ASSERT_TRUE (
      zlink::spot_data_plane_protocol_t::should_publish_bootstrap_descriptor (
        &runtime, true, 3));
}

}

int main (int argc, char **argv)
{
    UNITY_BEGIN ();
    RUN_TEST (test_mesh_xsub_monitor_ready_zero_clears_connected_peer);
    RUN_TEST (test_mesh_xsub_monitor_ready_count_growth_marks_connected);
    RUN_TEST (test_mesh_xsub_monitor_disconnect_clears_connected_peer);
    RUN_TEST (
      test_mesh_xsub_monitor_ready_count_changes_without_rewriting_membership);
    RUN_TEST (
      test_mesh_xsub_monitor_ready_growth_reports_endpoint_membership_change);
    RUN_TEST (test_mesh_xsub_monitor_ready_positive_keeps_endpoint_present);
    RUN_TEST (test_mesh_xsub_monitor_same_ready_count_does_not_bump_version);
    RUN_TEST (test_explicit_disconnect_updates_private_mesh_peer_state);
    RUN_TEST (test_ready_endpoint_helper_tracks_endpoint_local_readiness);
    RUN_TEST (
      test_bootstrap_descriptor_republish_stops_after_ready_topology_stabilizes);
    RUN_TEST (test_bootstrap_descriptor_republish_resumes_after_topology_change);
    return UNITY_END ();
}
