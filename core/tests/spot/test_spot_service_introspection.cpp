/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <chrono>
#include <string.h>
#include <vector>

SETUP_TEARDOWN_TESTCONTEXT

static void assert_routing_id_bytes (const zlink_routing_id_t *rid_,
                                     const char *expected_)
{
    TEST_ASSERT_NOT_NULL (rid_);
    TEST_ASSERT_NOT_NULL (expected_);
    const size_t expected_size = strlen (expected_);
    TEST_ASSERT_EQUAL_UINT8 (static_cast<uint8_t> (expected_size), rid_->size);
    TEST_ASSERT_EQUAL_MEMORY (expected_, rid_->data, expected_size);
}

static void test_spot_pub_sub_options_and_routing_ids ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    void *pub = zlink_spot_pub_new (pub_node);
    void *sub = zlink_spot_sub_new (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    const char *pub_rid = "spot-pub-rid";
    const char *sub_rid = "spot-sub-rid";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_set_routing_id (pub, pub_rid, strlen (pub_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_set_routing_id (sub, sub_rid, strlen (sub_rid)));

    const int pub_sndhwm = 222;
    const int pub_sndtimeo = 90;
    const int sub_rcvhwm = 333;
    const int sub_rcvtimeo = 80;
    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_set_option (pub, ZLINK_SPOT_PUB_OPT_SNDHWM, &pub_sndhwm,
                                 sizeof (pub_sndhwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_set_option (pub, ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                                 &pub_sndtimeo, sizeof (pub_sndtimeo)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_set_option (pub, ZLINK_SPOT_PUB_OPT_LINGER, &linger,
                                 sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_set_option (sub, ZLINK_SPOT_SUB_OPT_RCVHWM, &sub_rcvhwm,
                                 sizeof (sub_rcvhwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_set_option (sub, ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                                 &sub_rcvtimeo, sizeof (sub_rcvtimeo)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_set_option (sub, ZLINK_SPOT_SUB_OPT_LINGER, &linger,
                                 sizeof (linger)));

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_routing_id (pub, &rid));
    assert_routing_id_bytes (&rid, pub_rid);
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_routing_id (sub, &rid));
    assert_routing_id_bytes (&rid, sub_rid);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22610));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "svc-int"));
    msleep (100);

    zlink_peer_info_t peers[4];
    size_t peer_count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_peers (pub, peers, &peer_count));
    TEST_ASSERT_TRUE (peer_count > 0);
    peer_count = 4;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_peers (sub, peers, &peer_count));
    TEST_ASSERT_TRUE (peer_count > 0);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_pub_set_routing_id (pub, "late", 4));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_sub_set_routing_id (sub, "late", 4));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
}

static void test_spot_monitors_and_monitor_poller ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    void *pub = zlink_spot_pub_new (pub_node);
    void *sub = zlink_spot_sub_new (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_set_routing_id (pub, "pub-mon", 7));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_set_routing_id (sub, "sub-mon", 7));

    const int async_mode = ZLINK_SPOT_NODE_PUB_MODE_ASYNC;
    const int queue_hwm = 4;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_set_option (pub, ZLINK_SPOT_PUB_OPT_MODE, &async_mode,
                                 sizeof (async_mode)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_set_option (pub, ZLINK_SPOT_PUB_OPT_QUEUE_HWM, &queue_hwm,
                                 sizeof (queue_hwm)));

    void *sub_monitor = zlink_spot_sub_monitor_open (
      sub, ZLINK_MONITOR_EVENT_PEER_UP | ZLINK_SPOT_SUB_FILTER_APPLIED);
    void *pub_monitor =
      zlink_spot_pub_monitor_open (pub, ZLINK_SPOT_PUB_QUEUE_DRAINED);
    TEST_ASSERT_NOT_NULL (sub_monitor);
    TEST_ASSERT_NOT_NULL (pub_monitor);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);
    int sub_tag = 31;
    int pub_tag = 32;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_monitor (poller, sub_monitor, &sub_tag, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_poller_add_monitor (poller, pub_monitor, &pub_tag, ZLINK_POLLIN));

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22611));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "svc-mon"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish_bytes (pub, "svc-mon", "payload", 7, 0));

    bool saw_peer_up = false;
    bool saw_filter_applied = false;
    bool saw_queue_drained = false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
    while ((!saw_peer_up || !saw_filter_applied || !saw_queue_drained)
           && std::chrono::steady_clock::now () < deadline) {
        zlink_poller_event_t event;
        memset (&event, 0, sizeof (event));
        const int prc = zlink_poller_wait (poller, &event, 50);
        if (prc <= 0)
            continue;

        zlink_service_event_t ev;
        while (event.user_data == &sub_tag
               && zlink_service_monitor_recv (sub_monitor, &ev, ZLINK_DONTWAIT)
                    == 0) {
            if (ev.event_type == ZLINK_MONITOR_EVENT_PEER_UP) {
                saw_peer_up = true;
                assert_routing_id_bytes (&ev.routing_id, "sub-mon");
            } else if (ev.event_type == ZLINK_SPOT_SUB_FILTER_APPLIED) {
                saw_filter_applied = true;
                assert_routing_id_bytes (&ev.routing_id, "sub-mon");
            }
        }
        while (
          event.user_data == &pub_tag
          && zlink_service_monitor_recv (pub_monitor, &ev, ZLINK_DONTWAIT)
               == 0) {
            if (ev.event_type == ZLINK_SPOT_PUB_QUEUE_DRAINED) {
                saw_queue_drained = true;
                assert_routing_id_bytes (&ev.routing_id, "pub-mon");
            }
        }
    }

    TEST_ASSERT_TRUE (saw_peer_up);
    TEST_ASSERT_TRUE (saw_filter_applied);
    TEST_ASSERT_TRUE (saw_queue_drained);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&pub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
}

static void test_spot_monitor_closed_and_topology_reports ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_endpoints (
      registry, "inproc://spot-topology-pub", "inproc://spot-topology-router"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry,
                                                                     50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    void *pub = zlink_spot_pub_new (pub_node);
    void *sub = zlink_spot_sub_new (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_set_routing_id (pub, "pub-topology", 12));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_set_routing_id (sub, "sub-topology", 12));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_registry (
      pub_node, "inproc://spot-topology-router"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_connect_registry (
      sub_node, "inproc://spot-topology-router"));

    void *sub_monitor = zlink_spot_sub_monitor_open (
      sub, ZLINK_MONITOR_EVENT_CLOSED | ZLINK_SPOT_SUB_FILTER_APPLIED);
    TEST_ASSERT_NOT_NULL (sub_monitor);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22612));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "svc-topology"));

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_SPOT_SUB;
    strncpy (filter.service_name, "spot-sub", sizeof (filter.service_name) - 1);
    size_t count = 0;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (3000);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_registry_topology_query (registry, &filter, NULL, &count) == 0
            && count >= 1)
            break;
        msleep (10);
    }
    TEST_ASSERT_TRUE (count >= 1);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    const std::chrono::steady_clock::time_point close_deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (1000);
    bool saw_closed = false;
    while (std::chrono::steady_clock::now () < close_deadline) {
        if (zlink_service_monitor_recv (sub_monitor, &ev, ZLINK_DONTWAIT) == 0
            && ev.event_type == ZLINK_MONITOR_EVENT_CLOSED) {
            saw_closed = true;
            break;
        }
        msleep (10);
    }
    TEST_ASSERT_TRUE (saw_closed);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&sub_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_pub_sub_options_and_routing_ids);
    RUN_TEST (test_spot_monitors_and_monitor_poller);
    RUN_TEST (test_spot_monitor_closed_and_topology_reports);
    return UNITY_END ();
}
