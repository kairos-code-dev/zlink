/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <vector>

SETUP_TEARDOWN_TESTCONTEXT

static void setup_registry (void *ctx_,
                            void **registry_out_,
                            const char *pub_ep_,
                            const char *router_ep_)
{
    void *registry = zlink_registry_new (ctx_);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, pub_ep_, router_ep_));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_broadcast_interval (registry,
                                                                     50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    *registry_out_ = registry;
}

static void assert_routing_id_bytes (const zlink_routing_id_t *rid_,
                                     const char *expected_)
{
    TEST_ASSERT_NOT_NULL (rid_);
    TEST_ASSERT_NOT_NULL (expected_);
    const size_t expected_size = strlen (expected_);
    TEST_ASSERT_EQUAL_UINT8 (static_cast<uint8_t> (expected_size), rid_->size);
    TEST_ASSERT_EQUAL_MEMORY (expected_, rid_->data, expected_size);
}

static bool wait_service_event (void *monitor_,
                                zlink_service_event_t *event_out_,
                                int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_service_monitor_recv (monitor_, event_out_, ZLINK_DONTWAIT)
            == 0)
            return true;
        msleep (10);
    }
    return false;
}

static bool wait_discovery_receiver_count (void *discovery_,
                                           const char *service_name_,
                                           int expected_min_,
                                           int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        const int count =
          zlink_discovery_receiver_count (discovery_, service_name_);
        if (count >= expected_min_)
            return true;
        msleep (10);
    }
    return false;
}

static bool wait_gateway_connection_count (void *gateway_,
                                           const char *service_name_,
                                           int expected_min_,
                                           int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        const int count = zlink_gateway_connection_count (gateway_, service_name_);
        if (count >= expected_min_)
            return true;
        msleep (5);
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
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
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

static bool drain_monitor_event (void *monitor_,
                                 zlink_service_event_t *event_out_)
{
    return zlink_service_monitor_recv (monitor_, event_out_, ZLINK_DONTWAIT)
           == 0;
}

static void wait_gateway_event (void *poller_,
                                void *gateway_monitor_,
                                void *receiver_monitor_,
                                void *gateway_tag_,
                                void *receiver_tag_,
                                bool *saw_gateway_ready_,
                                bool *saw_receiver_register_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (4000);
    while ((!*saw_gateway_ready_ || !*saw_receiver_register_)
           && std::chrono::steady_clock::now () < deadline) {
        zlink_poller_event_t event;
        memset (&event, 0, sizeof (event));
        const int prc = zlink_poller_wait (poller_, &event, 50);
        if (prc <= 0)
            continue;

        zlink_service_event_t service_event;
        while (event.user_data == gateway_tag_
               && drain_monitor_event (gateway_monitor_, &service_event)) {
            if (service_event.event_type == ZLINK_GATEWAY_SERVICE_READY)
                *saw_gateway_ready_ = true;
        }
        while (event.user_data == receiver_tag_
               && drain_monitor_event (receiver_monitor_, &service_event)) {
            if (service_event.event_type == ZLINK_RECEIVER_REGISTER_OK)
                *saw_receiver_register_ = true;
        }
    }
}

static void test_discovery_monitor_and_routing_id ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://svcmon-reg-pub",
                    "inproc://svcmon-reg-router");

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);

    const char *discovery_rid = "disc-mon";
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_set_routing_id (
      discovery, discovery_rid, strlen (discovery_rid)));
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_routing_id (discovery, &rid));
    assert_routing_id_bytes (&rid, discovery_rid);

    void *monitor =
      zlink_discovery_monitor_open (discovery, ZLINK_DISCOVERY_SERVICE_UP);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://svcmon-reg-router"));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_subscribe (discovery, "svcmon"));

    void *receiver = zlink_receiver_new (ctx, "svcmon-rx");
    TEST_ASSERT_NOT_NULL (receiver);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22600));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_bind (receiver, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_connect_registry (
      receiver, "inproc://svcmon-reg-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_register (receiver, "svcmon", endpoint, 1));

    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (discovery, "svcmon", 1, 3000));

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    TEST_ASSERT_TRUE (wait_service_event (monitor, &ev, 3000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY, ev.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_DISCOVERY_SERVICE_UP, ev.event_type);
    TEST_ASSERT_TRUE ((ev.detail_flags & ZLINK_EVENT_DETAIL_SERVICE_NAME) != 0);
    TEST_ASSERT_TRUE ((ev.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT_RID) != 0);
    TEST_ASSERT_EQUAL_STRING ("svcmon", ev.service_name);
    assert_routing_id_bytes (&ev.routing_id, discovery_rid);
    TEST_ASSERT_TRUE (ev.value > 0);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_discovery_set_routing_id (discovery, "late", 4));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_gateway_receiver_routing_ids_and_options ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://svcint-opt-pub",
                    "inproc://svcint-opt-router");

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://svcint-opt-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-int-opt"));

    void *gateway = zlink_gateway_new (ctx, discovery, NULL);
    void *receiver = zlink_receiver_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (gateway);
    TEST_ASSERT_NOT_NULL (receiver);

    const char *gateway_rid = "gw-int";
    const char *receiver_rid = "rx-int";
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_routing_id (gateway, gateway_rid, strlen (gateway_rid)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_set_routing_id (
      receiver, receiver_rid, strlen (receiver_rid)));

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (gateway, &rid));
    assert_routing_id_bytes (&rid, gateway_rid);
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_routing_id (receiver, &rid));
    assert_routing_id_bytes (&rid, receiver_rid);

    const int hwm = 321;
    const int timeout_ms = 111;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (gateway, ZLINK_GATEWAY_OPT_SNDHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (gateway, ZLINK_GATEWAY_OPT_RCVHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_set_option (receiver, ZLINK_RECEIVER_OPT_SNDHWM, &hwm,
                                 sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_set_option (receiver, ZLINK_RECEIVER_OPT_RCVTIMEO,
                                 &timeout_ms, sizeof (timeout_ms)));

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22602));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_bind (receiver, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_connect_registry (
      receiver, "inproc://svcint-opt-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_register (receiver, "svc-int-opt", endpoint, 1));
    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (discovery, "svc-int-opt", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (gateway, "svc-int-opt", 1, 3000));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_gateway_set_routing_id (gateway, "late", 4));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_receiver_set_routing_id (receiver, "late", 4));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_gateway_receiver_monitors_and_monitor_poller ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://svcint-reg-pub",
                    "inproc://svcint-reg-router");

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://svcint-reg-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-int"));

    void *gateway = zlink_gateway_new (ctx, discovery, NULL);
    void *receiver = zlink_receiver_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (gateway);
    TEST_ASSERT_NOT_NULL (receiver);

    void *gateway_monitor = zlink_gateway_monitor_open (
      gateway, ZLINK_GATEWAY_SERVICE_READY | ZLINK_GATEWAY_ROUTE_UP);
    void *receiver_monitor =
      zlink_receiver_monitor_open (receiver, ZLINK_RECEIVER_REGISTER_OK);
    TEST_ASSERT_NOT_NULL (gateway_monitor);
    TEST_ASSERT_NOT_NULL (receiver_monitor);

    void *poller = zlink_poller_new ();
    TEST_ASSERT_NOT_NULL (poller);
    int gateway_tag = 11;
    int receiver_tag = 12;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_add_monitor (
      poller, gateway_monitor, &gateway_tag, ZLINK_POLLIN));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_add_monitor (
      poller, receiver_monitor, &receiver_tag, ZLINK_POLLIN));

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22601));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_bind (receiver, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_connect_registry (
      receiver, "inproc://svcint-reg-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_register (receiver, "svc-int", endpoint, 1));
    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (discovery, "svc-int", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (gateway, "svc-int", 1, 3000));

    bool saw_gateway_ready = false;
    bool saw_receiver_register = false;
    wait_gateway_event (poller, gateway_monitor, receiver_monitor, &gateway_tag,
                        &receiver_tag, &saw_gateway_ready,
                        &saw_receiver_register);
    TEST_ASSERT_TRUE (saw_gateway_ready);
    TEST_ASSERT_TRUE (saw_receiver_register);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_poller_destroy (&poller));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&gateway_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&receiver_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_receiver_unregister_failed_monitor_event ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://svcint-unregfail-pub",
                    "inproc://svcint-unregfail-router");

    void *receiver = zlink_receiver_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (receiver);

    void *monitor =
      zlink_receiver_monitor_open (receiver, ZLINK_RECEIVER_UNREGISTER_FAILED);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_connect_registry (
      receiver, "inproc://svcint-unregfail-router"));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_receiver_unregister (receiver, "missing-service"));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    TEST_ASSERT_TRUE (wait_service_event (monitor, &ev, 3000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_RECEIVER, ev.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_RECEIVER_UNREGISTER_FAILED, ev.event_type);
    TEST_ASSERT_EQUAL_INT (-1, ev.status);
    TEST_ASSERT_EQUAL_INT (EINVAL, ev.error_code);
    TEST_ASSERT_TRUE ((ev.detail_flags & ZLINK_EVENT_DETAIL_SERVICE_NAME) != 0);
    TEST_ASSERT_EQUAL_STRING ("missing-service", ev.service_name);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_registry_topology_snapshot_and_remote_query ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://topology-reg-pub",
                    "inproc://topology-reg-router");

    void *receiver = zlink_receiver_new (ctx, NULL);
    TEST_ASSERT_NOT_NULL (receiver);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_set_routing_id (receiver, "rx-topology", 11));

    zlink_routing_id_t receiver_rid;
    memset (&receiver_rid, 0, sizeof (receiver_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_routing_id (receiver, &receiver_rid));

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22602));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_bind (receiver, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_connect_registry (
      receiver, "inproc://topology-reg-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_register (receiver, "svc-topology", endpoint, 1));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_RECEIVER, "svc-topology", &receiver_rid,
      ZLINK_TOPOLOGY_STATE_READY, 3000));

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_snapshot (registry, NULL, &count));
    TEST_ASSERT_TRUE (count >= 1);

    zlink_registry_topology_entry_t sentinel;
    memset (&sentinel, 0xAB, sizeof (sentinel));
    size_t too_small = 0;
    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_registry_topology_snapshot (registry, &sentinel, &too_small));
    TEST_ASSERT_EQUAL_INT (ENOBUFS, zlink_errno ());
    TEST_ASSERT_EQUAL_UINT16 (0xABAB, sentinel.service_kind);
    TEST_ASSERT_TRUE (too_small >= 1);

    std::vector<zlink_registry_topology_entry_t> entries (count);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_snapshot (registry, &entries[0], &count));
    TEST_ASSERT_TRUE (count >= 1);
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_RECEIVER,
                              entries[0].service_kind);
    TEST_ASSERT_EQUAL_STRING ("svc-topology", entries[0].service_name);
    TEST_ASSERT_EQUAL_STRING (endpoint, entries[0].endpoint);
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_SOURCE_DISCOVERY, entries[0].source);
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_STATE_READY, entries[0].state);
    TEST_ASSERT_EQUAL_UINT (1, entries[0].desired_count);
    TEST_ASSERT_EQUAL_UINT (1, entries[0].ready_count);
    TEST_ASSERT_EQUAL_UINT8 (receiver_rid.size, entries[0].routing_id.size);
    TEST_ASSERT_EQUAL_MEMORY (receiver_rid.data, entries[0].routing_id.data,
                              receiver_rid.size);

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    strncpy (filter.service_name, "svc-topology",
             sizeof (filter.service_name) - 1);
    filter.routing_id = receiver_rid;
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);

    void *client = zlink_registry_query_client_new (ctx);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_client_connect (
      client, "inproc://topology-reg-router"));
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_query_snapshot (client, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    std::vector<zlink_registry_topology_entry_t> remote_entries (count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_snapshot (
      client, &filter, &remote_entries[0], &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_STATE_READY,
                              remote_entries[0].state);
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_SOURCE_DISCOVERY,
                              remote_entries[0].source);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_unregister (receiver,
                                                          "svc-topology"));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_RECEIVER, "svc-topology", &receiver_rid,
      ZLINK_TOPOLOGY_STATE_STOPPED, 3000));

    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_RECEIVER;
    filter.state = ZLINK_TOPOLOGY_STATE_STOPPED;
    filter.routing_id = receiver_rid;
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_monitor_closed_event_on_service_destroy ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-close", 10));

    void *monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_MONITOR_EVENT_CLOSED);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));

    zlink_service_event_t ev;
    memset (&ev, 0, sizeof (ev));
    TEST_ASSERT_TRUE (wait_service_event (monitor, &ev, 1000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY, ev.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_MONITOR_EVENT_CLOSED, ev.event_type);
    TEST_ASSERT_TRUE ((ev.detail_flags & ZLINK_EVENT_DETAIL_SUBJECT_RID) != 0);
    assert_routing_id_bytes (&ev.routing_id, "disc-close");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
}

static void test_registry_topology_reports_discovery_and_gateway ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://gwdisc-topology-pub",
                    "inproc://gwdisc-topology-router");

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-topology", 13));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://gwdisc-topology-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-gw-topology"));

    void *gateway = zlink_gateway_new (ctx, discovery, NULL);
    TEST_ASSERT_NOT_NULL (gateway);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_routing_id (gateway, "gw-topology", 11));

    void *receiver = zlink_receiver_new (ctx, "rx-topology-gw");
    TEST_ASSERT_NOT_NULL (receiver);
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22603));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_bind (receiver, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_connect_registry (
      receiver, "inproc://gwdisc-topology-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_receiver_register (receiver, "svc-gw-topology", endpoint, 1));

    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (discovery, "svc-gw-topology", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (gateway, "svc-gw-topology", 1, 3000));

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
    strncpy (filter.service_name, "svc-gw-topology",
             sizeof (filter.service_name) - 1);
    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);

    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    strncpy (filter.service_name, "svc-gw-topology",
             sizeof (filter.service_name) - 1);
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);

    std::vector<zlink_registry_topology_entry_t> entries (count);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, &entries[0], &count));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_STATE_READY, entries[0].state);
    TEST_ASSERT_TRUE (entries[0].ready_count >= 1);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_receiver_destroy (&receiver));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

int main (int, char **)
{
    setup_test_environment (300);

    UNITY_BEGIN ();
    RUN_TEST (test_discovery_monitor_and_routing_id);
    RUN_TEST (test_gateway_receiver_routing_ids_and_options);
    RUN_TEST (test_gateway_receiver_monitors_and_monitor_poller);
    RUN_TEST (test_receiver_unregister_failed_monitor_event);
    RUN_TEST (test_registry_topology_snapshot_and_remote_query);
    RUN_TEST (test_monitor_closed_event_on_service_destroy);
    RUN_TEST (test_registry_topology_reports_discovery_and_gateway);
    return UNITY_END ();
}
