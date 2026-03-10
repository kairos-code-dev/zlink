/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include "services/spot/spot_node.hpp"
#include "services/discovery/discovery.hpp"

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

static bool wait_for_registry_uplink (void *discovery_, int timeout_ms_)
{
    zlink::discovery_t *discovery =
      static_cast<zlink::discovery_t *> (discovery_);
    if (!discovery)
        return false;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        std::string uplink;
        if (discovery->latest_registry_uplink (&uplink) && !uplink.empty ())
            return true;
        msleep (10);
    }
    return false;
}

static int connect_discovery_registry_with_retry (void *discovery_,
                                                  const char *endpoint_,
                                                  int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_discovery_connect_registry (discovery_, endpoint_) == 0)
            return 0;
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (10);
    }
    errno = EAGAIN;
    return -1;
}

static int register_spot_node_with_retry (void *node_,
                                          const char *service_name_,
                                          const char *advertise_endpoint_,
                                          int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_spot_node_register (node_, service_name_, advertise_endpoint_)
            == 0)
            return 0;
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (10);
    }
    errno = EAGAIN;
    return -1;
}

static bool wait_for_spot_message_bytes (void *sub_,
                                         const char *topic_,
                                         const char *payload_,
                                         size_t payload_size_,
                                         int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        char topic[256] = {0};
        size_t topic_len = sizeof (topic);
        const int rc = zlink_spot_sub_recv (sub_, &parts, &part_count,
                                            ZLINK_DONTWAIT, topic, &topic_len);
        if (rc == 0) {
            const bool matched =
              topic_ && strcmp (topic, topic_) == 0 && part_count == 1
              && zlink_msg_size (&parts[0]) == payload_size_
              && memcmp (zlink_msg_data (&parts[0]), payload_, payload_size_)
                   == 0;
            zlink_multipart_close (parts, part_count);
            if (matched)
                return true;
        } else {
            TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        }
        msleep (10);
    }
    return false;
}

static bool wait_for_topology_state (void *registry_,
                                     uint16_t service_kind_,
                                     const char *service_name_,
                                     const zlink_routing_id_t *routing_id_,
                                     uint16_t state_,
                                     int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_registry_topology_filter_t filter;
        memset (&filter, 0, sizeof (filter));
        filter.service_kind = service_kind_;
        if (service_name_)
            strncpy (filter.service_name, service_name_,
                     sizeof (filter.service_name) - 1);
        if (routing_id_)
            filter.routing_id = *routing_id_;

        size_t count = 0;
        if (zlink_registry_topology_query (registry_, &filter, NULL, &count) == 0
            && count > 0) {
            std::vector<zlink_registry_topology_entry_t> entries (count);
            if (zlink_registry_topology_query (registry_, &filter, &entries[0],
                                               &count)
                == 0) {
                for (size_t i = 0; i < count; ++i) {
                    if (entries[i].state == state_)
                        return true;
                }
            }
        }
        msleep (10);
    }
    return false;
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

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_pub_publish_bytes (pub, "svc-int", "pong", 4, 0));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256] = {0};
    size_t topic_len = sizeof (topic);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_sub_recv (sub, &parts, &part_count, 0, topic, &topic_len));
    TEST_ASSERT_EQUAL_UINT (7, topic_len);
    TEST_ASSERT_EQUAL_MEMORY ("svc-int", topic, topic_len);
    TEST_ASSERT_EQUAL_UINT64 (1, part_count);
    TEST_ASSERT_EQUAL_UINT (4, zlink_msg_size (&parts[0]));
    TEST_ASSERT_EQUAL_MEMORY ("pong", zlink_msg_data (&parts[0]), 4);
    zlink_multipart_close (parts, part_count);

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

    void *sub_monitor =
      zlink_spot_sub_monitor_open (sub, ZLINK_MONITOR_EVENT_READY
                                          | ZLINK_MONITOR_EVENT_PEER_UP
                                          | ZLINK_SPOT_SUB_FILTER_APPLIED);
    void *pub_monitor =
      zlink_spot_pub_monitor_open (pub, ZLINK_MONITOR_EVENT_READY
                                          | ZLINK_MONITOR_EVENT_PEER_UP
                                          | ZLINK_SPOT_PUB_QUEUE_DRAINED);
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

    bool saw_filter_applied = false;
    bool saw_queue_drained = false;
    bool saw_sub_generic = false;
    bool saw_pub_generic = false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (500);
    while ((!saw_sub_generic || !saw_pub_generic)
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
            if (ev.event_type == ZLINK_SPOT_SUB_FILTER_APPLIED) {
                saw_filter_applied = true;
            } else if (ev.event_type == ZLINK_MONITOR_EVENT_READY
                       || ev.event_type == ZLINK_MONITOR_EVENT_PEER_UP) {
                saw_sub_generic = true;
                TEST_ASSERT_TRUE ((ev.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT)
                                  != 0);
            }
        }
        while (
          event.user_data == &pub_tag
          && zlink_service_monitor_recv (pub_monitor, &ev, ZLINK_DONTWAIT)
               == 0) {
            if (ev.event_type == ZLINK_SPOT_PUB_QUEUE_DRAINED) {
                saw_queue_drained = true;
            } else if (ev.event_type == ZLINK_MONITOR_EVENT_READY
                       || ev.event_type == ZLINK_MONITOR_EVENT_PEER_UP) {
                saw_pub_generic = true;
                TEST_ASSERT_TRUE ((ev.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT)
                                  != 0);
            }
        }
    }

    TEST_ASSERT_FALSE (saw_filter_applied);
    TEST_ASSERT_FALSE (saw_queue_drained);

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
    zlink_routing_id_t sub_rid;
    memset (&sub_rid, 0, sizeof (sub_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_routing_id (sub, &sub_rid));

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "spot-disc", 9));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://spot-topology-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-topology"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_discovery (pub_node, discovery, "svc-topology"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_discovery (sub_node, discovery, "svc-topology"));

    void *sub_monitor =
      zlink_spot_sub_monitor_open (sub, ZLINK_MONITOR_EVENT_CLOSED
                                          | ZLINK_MONITOR_EVENT_LOST
                                          | ZLINK_MONITOR_EVENT_PEER_DOWN);
    TEST_ASSERT_NOT_NULL (sub_monitor);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22612));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "svc-topology"));

    msleep (200);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_disconnect_peer_pub (sub_node, endpoint));

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    const std::chrono::steady_clock::time_point down_deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (1000);
    while (std::chrono::steady_clock::now () < down_deadline) {
        if (zlink_service_monitor_recv (sub_monitor, &ev, ZLINK_DONTWAIT) == 0
            && (ev.event_type == ZLINK_MONITOR_EVENT_LOST
                || ev.event_type == ZLINK_MONITOR_EVENT_PEER_DOWN)) {
            TEST_ASSERT_TRUE ((ev.detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT)
                              != 0);
            break;
        }
        msleep (10);
    }
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_SUB, "svc-topology", &sub_rid,
      ZLINK_TOPOLOGY_STATE_LOST, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));

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
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_spot_topology_summary_lifecycle ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_endpoints (
      registry, "inproc://spot-summary-pub", "inproc://spot-summary-router"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry,
                                                                     50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "spot-summary-disc", 17));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://spot-summary-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-summary"));

    void *node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_discovery (node, discovery, "svc-summary"));

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22627));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (node, endpoint));

    void *pub = zlink_spot_pub_new (node);
    void *sub = zlink_spot_sub_new (node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    zlink_routing_id_t pub_rid;
    zlink_routing_id_t sub_rid;
    memset (&pub_rid, 0, sizeof (pub_rid));
    memset (&sub_rid, 0, sizeof (sub_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_routing_id (pub, &pub_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_routing_id (sub, &sub_rid));

    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_PUB, "svc-summary", &pub_rid,
      ZLINK_TOPOLOGY_STATE_READY, 2000));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_SUB, "svc-summary", &sub_rid,
      ZLINK_TOPOLOGY_STATE_CONNECTING, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "svc-summary"));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_SUB, "svc-summary", &sub_rid,
      ZLINK_TOPOLOGY_STATE_READY, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_SUB, "svc-summary", &sub_rid,
      ZLINK_TOPOLOGY_STATE_STOPPED, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_SPOT_PUB, "svc-summary", &pub_rid,
      ZLINK_TOPOLOGY_STATE_STOPPED, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_spot_register_null_derivation_and_wildcard_rejection ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
              test_port (22618));
    snprintf (registry_router, sizeof (registry_router), "tcp://127.0.0.1:%d",
              test_port (22619));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, registry_pub, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-regnull"));
    TEST_ASSERT_TRUE (wait_for_registry_uplink (discovery, 2000));

    void *ok_node = zlink_spot_node_new (ctx);
    void *wild_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (ok_node);
    TEST_ASSERT_NOT_NULL (wild_node);

    char concrete_endpoint[MAX_SOCKET_STRING];
    snprintf (concrete_endpoint, sizeof (concrete_endpoint),
              "tcp://127.0.0.1:%d", test_port (22620));
    char wildcard_endpoint[MAX_SOCKET_STRING];
    snprintf (wildcard_endpoint, sizeof (wildcard_endpoint), "tcp://*:%d",
              test_port (22621));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (ok_node, concrete_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (wild_node, wildcard_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_discovery (ok_node, discovery, "svc-regnull"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_discovery (wild_node, discovery, "svc-regnull"));

    TEST_ASSERT_SUCCESS_ERRNO (
      register_spot_node_with_retry (ok_node, "svc-regnull", NULL, 5000));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_register (wild_node, "svc-regnull", NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unregister (ok_node, "svc-regnull"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&wild_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&ok_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_tls_settings_lock_after_bind_connect_and_register ()
{
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    tls_test_files_t files = make_tls_test_files ();

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
              test_port (22625));
    snprintf (registry_router, sizeof (registry_router), "tcp://127.0.0.1:%d",
              test_port (22626));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, registry_pub, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));

    void *discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_SPOT);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-tlslock"));
    TEST_ASSERT_TRUE (wait_for_registry_uplink (discovery, 2000));

    void *server_node = zlink_spot_node_new (ctx);
    void *client_node = zlink_spot_node_new (ctx);
    void *reg_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (server_node);
    TEST_ASSERT_NOT_NULL (client_node);
    TEST_ASSERT_NOT_NULL (reg_node);

    char tls_endpoint[MAX_SOCKET_STRING];
    snprintf (tls_endpoint, sizeof (tls_endpoint), "tls://localhost:%d",
              test_port (22622));
    char reg_endpoint[MAX_SOCKET_STRING];
    snprintf (reg_endpoint, sizeof (reg_endpoint), "tcp://127.0.0.1:%d",
              test_port (22623));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_tls_server (server_node, files.server_cert.c_str (),
                                      files.server_key.c_str ()));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (server_node, tls_endpoint));
    TEST_ASSERT_EQUAL_INT (
      -1,
      zlink_spot_node_set_tls_server (server_node, files.server_cert.c_str (),
                                      files.server_key.c_str ()));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_set_tls_client (
      client_node, files.ca_cert.c_str (), "localhost", 0));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (client_node, tls_endpoint));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_set_tls_client (client_node, files.ca_cert.c_str (),
                                          "localhost", 0));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (reg_node, reg_endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_discovery (reg_node, discovery, "svc-tlslock"));
    TEST_ASSERT_SUCCESS_ERRNO (
      register_spot_node_with_retry (reg_node, "svc-tlslock", NULL, 5000));
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_set_tls_client (reg_node, files.ca_cert.c_str (),
                                          "localhost", 0));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_unregister (reg_node, "svc-tlslock"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&reg_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&client_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&server_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    cleanup_tls_test_files (files);
}

static void test_spot_late_connect_replays_existing_subscription ()
{
    if (!zlink_has ("tcp")) {
        TEST_IGNORE_MESSAGE ("TCP not available");
        return;
    }

    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *pub_node = zlink_spot_node_new (ctx);
    void *sub_node = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (pub_node);
    TEST_ASSERT_NOT_NULL (sub_node);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22624));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_bind (pub_node, endpoint));

    void *pub = zlink_spot_pub_new (pub_node);
    void *sub = zlink_spot_sub_new (sub_node);
    TEST_ASSERT_NOT_NULL (pub);
    TEST_ASSERT_NOT_NULL (sub);

    const int recv_timeo = 200;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_set_option (
      sub, ZLINK_SPOT_SUB_OPT_RCVTIMEO, &recv_timeo, sizeof (recv_timeo)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_subscribe (sub, "svc-late"));

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_connect_peer_pub (sub_node, endpoint));

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (2000);
    bool received = false;
    int seq = 0;
    while (std::chrono::steady_clock::now () < deadline && !received) {
        char payload[16];
        const int payload_size =
          snprintf (payload, sizeof (payload), "m-%d", seq++);
        TEST_ASSERT_TRUE (payload_size > 0);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_pub_publish_bytes (pub, "svc-late", payload,
                                        static_cast<size_t> (payload_size), 0));
        received =
          wait_for_spot_message_bytes (sub, "svc-late", payload, payload_size,
                                       100);
    }
    TEST_ASSERT_TRUE_MESSAGE (
      received, "late-connect peer did not receive replayed subscription");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_sub_destroy (&sub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_pub_destroy (&pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_node));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

static void test_spot_faulted_node_apis_fail_with_efsm ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *node_handle = zlink_spot_node_new (ctx);
    TEST_ASSERT_NOT_NULL (node_handle);

    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_handle);
    node->debug_mark_fault (EIO);

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_bind (node_handle, "tcp://127.0.0.1:9"));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_EQUAL_PTR (NULL, zlink_spot_pub_new (node_handle));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_EQUAL_PTR (NULL, zlink_spot_sub_new (node_handle));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_spot_node_connect_peer_pub (node_handle, "tcp://127.0.0.1:9"));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&node_handle));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
    RUN_TEST (test_spot_pub_sub_options_and_routing_ids);
    RUN_TEST (test_spot_monitors_and_monitor_poller);
    RUN_TEST (test_spot_monitor_closed_and_topology_reports);
    RUN_TEST (test_spot_topology_summary_lifecycle);
    RUN_TEST (test_spot_register_null_derivation_and_wildcard_rejection);
    RUN_TEST (test_spot_tls_settings_lock_after_bind_connect_and_register);
    RUN_TEST (test_spot_late_connect_replays_existing_subscription);
    RUN_TEST (test_spot_faulted_node_apis_fail_with_efsm);
    return UNITY_END ();
}
