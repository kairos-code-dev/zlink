/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <atomic>
#include <chrono>
#include <string.h>
#include <vector>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
struct monitor_probe_t
{
    monitor_probe_t () : calls (0)
    {
        memset (&last_event, 0, sizeof (last_event));
    }

    std::atomic<int> calls;
    zlink_service_event_t last_event;
};

struct gateway_server_t
{
    gateway_server_t () : discovery (NULL), gateway (NULL) {}

    void *discovery;
    void *gateway;
};

monitor_probe_t *g_monitor_a = NULL;
monitor_probe_t *g_monitor_b = NULL;
monitor_probe_t *g_monitor_c = NULL;

void assert_routing_id_bytes (const zlink_routing_id_t *rid_,
                              const char *expected_)
{
    TEST_ASSERT_NOT_NULL (rid_);
    TEST_ASSERT_NOT_NULL (expected_);
    const size_t expected_size = strlen (expected_);
    TEST_ASSERT_EQUAL_UINT8 (static_cast<uint8_t> (expected_size), rid_->size);
    TEST_ASSERT_EQUAL_MEMORY (expected_, rid_->data, expected_size);
}

void discard_gateway_message (zlink_gateway_msg_kind_t kind_,
                              const char *,
                              size_t,
                              const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    (void) kind_;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void service_monitor_handler_a (const zlink_service_event_t *event_)
{
    if (!g_monitor_a || !event_)
        return;
    g_monitor_a->last_event = *event_;
    g_monitor_a->calls.fetch_add (1);
}

void service_monitor_handler_b (const zlink_service_event_t *event_)
{
    if (!g_monitor_b || !event_)
        return;
    g_monitor_b->last_event = *event_;
    g_monitor_b->calls.fetch_add (1);
}

void service_monitor_handler_c (const zlink_service_event_t *event_)
{
    if (!g_monitor_c || !event_)
        return;
    g_monitor_c->last_event = *event_;
    g_monitor_c->calls.fetch_add (1);
}

bool wait_for_calls (std::atomic<int> *counter_, int expected_, int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (counter_->load () >= expected_)
            return true;
        msleep (step_ms);
    }
    return counter_->load () >= expected_;
}

bool wait_discovery_receiver_count (void *discovery_,
                                    const char *service_name_,
                                    int expected_min_,
                                    int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_discovery_receiver_count (discovery_, service_name_)
            >= expected_min_)
            return true;
        msleep (10);
    }
    return false;
}

bool wait_gateway_connection_count (void *gateway_,
                                    const char *service_name_,
                                    int expected_min_,
                                    int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_gateway_connection_count (gateway_, service_name_)
            >= expected_min_)
            return true;
        msleep (10);
    }
    return false;
}

bool wait_for_topology_state (void *registry_,
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

void setup_registry (void *ctx_,
                     void **registry_out_,
                     const char *pub_ep_,
                     const char *router_ep_)
{
    void *registry = zlink_registry_new (ctx_);
    TEST_ASSERT_NOT_NULL (registry);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry, pub_ep_, router_ep_));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));
    *registry_out_ = registry;
}

void register_gateway_with_timeout (void *gateway_,
                                    const char *service_name_,
                                    const char *endpoint_,
                                    uint32_t weight_,
                                    int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_register (gateway_, service_name_, endpoint_, weight_)
            == 0)
            return;
        if (errno != EAGAIN)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway register timeout");
}

void init_gateway_server (gateway_server_t *server_,
                          void *ctx_,
                          const char *registry_ep_,
                          const char *routing_id_,
                          const char *bind_ep_,
                          zlink_gateway_handler_fn handler_)
{
    server_->discovery =
      zlink_discovery_new_typed (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_->discovery, registry_ep_));
    server_->gateway =
      zlink_gateway_new (ctx_, server_->discovery, routing_id_, handler_);
    TEST_ASSERT_NOT_NULL (server_->gateway);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_bind (server_->gateway, bind_ep_));
}

void destroy_gateway_server (gateway_server_t *server_)
{
    if (server_->gateway)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server_->gateway));
    if (server_->discovery)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_->discovery));
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
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-mon", 8));

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_routing_id (discovery, &rid));
    assert_routing_id_bytes (&rid, "disc-mon");

    monitor_probe_t probe;
    g_monitor_a = &probe;
    void *monitor =
      zlink_discovery_monitor_open (discovery, ZLINK_DISCOVERY_SERVICE_UP,
                                    &service_monitor_handler_a);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      discovery, "inproc://svcmon-reg-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svcmon"));

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22600));
    init_gateway_server (&server, ctx, "inproc://svcmon-reg-router",
                         "svcmon-gw", endpoint, &discard_gateway_message);
    register_gateway_with_timeout (server.gateway, "svcmon", endpoint, 1, 3000);

    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (discovery, "svcmon", 1, 3000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 3000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY,
                              probe.last_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_DISCOVERY_SERVICE_UP,
                              probe.last_event.event_type);
    TEST_ASSERT_EQUAL_STRING ("svcmon", probe.last_event.service_name);
    assert_routing_id_bytes (&probe.last_event.routing_id, "disc-mon");

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_discovery_set_routing_id (discovery, "late", 4));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_monitor_a = NULL;
}

static void test_gateway_receiver_routing_ids_and_options ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://svcint-opt-pub",
                    "inproc://svcint-opt-router");

    void *client_discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      client_discovery, "inproc://svcint-opt-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (client_discovery, "svc-int-opt"));

    void *client =
      zlink_gateway_new (ctx, client_discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22602));
    init_gateway_server (&server, ctx, "inproc://svcint-opt-router",
                         "gw-server-int",
                         endpoint, &discard_gateway_message);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_routing_id (client, "gw-int", 6));
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (client, &rid));
    assert_routing_id_bytes (&rid, "gw-int");
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server.gateway, &rid));
    assert_routing_id_bytes (&rid, "gw-server-int");

    const int hwm = 321;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (client, ZLINK_GATEWAY_OPT_SNDHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (client, ZLINK_GATEWAY_OPT_RCVHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (server.gateway, ZLINK_GATEWAY_OPT_SNDHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (server.gateway, ZLINK_GATEWAY_OPT_RCVHWM, &hwm,
                                sizeof (hwm)));

    register_gateway_with_timeout (server.gateway, "svc-int-opt", endpoint, 1,
                                   3000);
    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (client_discovery, "svc-int-opt", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (client, "svc-int-opt", 1, 3000));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_gateway_set_routing_id (client, "late", 4));
    TEST_ASSERT_EQUAL_INT (EFSM, zlink_errno ());

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_gateway_receiver_monitors_and_monitor_poller ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://svcint-reg-pub",
                    "inproc://svcint-reg-router");

    void *client_discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      client_discovery, "inproc://svcint-reg-router"));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (client_discovery, "svc-int"));

    void *client =
      zlink_gateway_new (ctx, client_discovery, NULL, &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22601));
    init_gateway_server (&server, ctx, "inproc://svcint-reg-router", NULL,
                         endpoint, &discard_gateway_message);

    monitor_probe_t server_probe;
    g_monitor_b = &server_probe;
    void *client_monitor = zlink_gateway_monitor_open (
      client, ZLINK_GATEWAY_SERVICE_READY | ZLINK_GATEWAY_ROUTE_UP,
      &service_monitor_handler_a);
    void *server_monitor = zlink_gateway_monitor_open (
      server.gateway, ZLINK_GATEWAY_REGISTER_OK,
      &service_monitor_handler_b);
    TEST_ASSERT_NOT_NULL (client_monitor);
    TEST_ASSERT_NOT_NULL (server_monitor);

    register_gateway_with_timeout (server.gateway, "svc-int", endpoint, 1, 3000);
    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (client_discovery, "svc-int", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (client, "svc-int", 1, 3000));
    TEST_ASSERT_TRUE (wait_for_calls (&server_probe.calls, 1, 3000));

    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_GATEWAY,
                              server_probe.last_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_GATEWAY_REGISTER_OK,
                              server_probe.last_event.event_type);
    TEST_ASSERT_EQUAL_STRING ("svc-int", server_probe.last_event.service_name);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&client_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&server_monitor));
    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_monitor_b = NULL;
}

static void test_receiver_unregister_failed_monitor_event ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://svcint-unregfail-pub",
                    "inproc://svcint-unregfail-router");

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22610));
    init_gateway_server (&server, ctx, "inproc://svcint-unregfail-router", NULL,
                         endpoint, &discard_gateway_message);

    monitor_probe_t probe;
    g_monitor_c = &probe;
    void *monitor = zlink_gateway_monitor_open (
      server.gateway, ZLINK_GATEWAY_UNREGISTER_FAILED,
      &service_monitor_handler_c);
    TEST_ASSERT_NOT_NULL (monitor);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_gateway_unregister (server.gateway, "missing-service"));
    TEST_ASSERT_EQUAL_INT (EINVAL, zlink_errno ());
    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 3000));

    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_GATEWAY,
                              probe.last_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_GATEWAY_UNREGISTER_FAILED,
                              probe.last_event.event_type);
    TEST_ASSERT_EQUAL_INT (-1, probe.last_event.status);
    TEST_ASSERT_EQUAL_INT (EINVAL, probe.last_event.error_code);
    TEST_ASSERT_EQUAL_STRING ("missing-service", probe.last_event.service_name);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_monitor_c = NULL;
}

static void test_registry_topology_snapshot_and_remote_query ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://topology-reg-pub",
                    "inproc://topology-reg-router");

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22603));
    init_gateway_server (&server, ctx, "inproc://topology-reg-router",
                         "gw-topology", endpoint, &discard_gateway_message);
    register_gateway_with_timeout (server.gateway, "svc-topology", endpoint, 1,
                                   3000);

    zlink_routing_id_t gateway_rid;
    memset (&gateway_rid, 0, sizeof (gateway_rid));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_routing_id (server.gateway, &gateway_rid));

    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_GATEWAY, "svc-topology", &gateway_rid,
      ZLINK_TOPOLOGY_STATE_READY, 3000));

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_snapshot (registry, NULL, &count));
    TEST_ASSERT_TRUE (count >= 1);

    std::vector<zlink_registry_topology_entry_t> entries (count);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_snapshot (registry, &entries[0], &count));
    TEST_ASSERT_TRUE (count >= 1);

    bool saw_ready = false;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].service_kind == ZLINK_SERVICE_KIND_GATEWAY
            && strcmp (entries[i].service_name, "svc-topology") == 0
            && entries[i].state == ZLINK_TOPOLOGY_STATE_READY) {
            saw_ready = true;
            TEST_ASSERT_EQUAL_STRING (endpoint, entries[i].endpoint);
            TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_SOURCE_DISCOVERY,
                                      entries[i].source);
            break;
        }
    }
    TEST_ASSERT_TRUE (saw_ready);

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    strncpy (filter.service_name, "svc-topology",
             sizeof (filter.service_name) - 1);
    filter.routing_id = gateway_rid;
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
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_STATE_READY,
                              remote_entries[0].state);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_unregister (server.gateway, "svc-topology"));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_GATEWAY, "svc-topology", &gateway_rid,
      ZLINK_TOPOLOGY_STATE_STOPPED, 3000));

    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    filter.state = ZLINK_TOPOLOGY_STATE_STOPPED;
    filter.routing_id = gateway_rid;
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_destroy (&client));
    destroy_gateway_server (&server);
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

    monitor_probe_t probe;
    g_monitor_a = &probe;
    void *monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_MONITOR_EVENT_CLOSED, &service_monitor_handler_a);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 1000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY,
                              probe.last_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_MONITOR_EVENT_CLOSED,
                              probe.last_event.event_type);
    assert_routing_id_bytes (&probe.last_event.routing_id, "disc-close");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    g_monitor_a = NULL;
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

    void *client =
      zlink_gateway_new (ctx, discovery, "gw-topology", &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22604));
    init_gateway_server (&server, ctx, "inproc://gwdisc-topology-router",
                         "gw-server-topology", endpoint, &discard_gateway_message);
    register_gateway_with_timeout (server.gateway, "svc-gw-topology", endpoint,
                                   1, 3000);

    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (discovery, "svc-gw-topology", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (client, "svc-gw-topology", 1, 3000));

    zlink_registry_topology_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_DISCOVERY;
    strncpy (filter.service_name, "svc-gw-topology",
             sizeof (filter.service_name) - 1);
    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, NULL, &count));
    TEST_ASSERT_TRUE (count >= 1);

    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    strncpy (filter.service_name, "svc-gw-topology",
             sizeof (filter.service_name) - 1);
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, NULL, &count));
    TEST_ASSERT_TRUE (count >= 1);

    std::vector<zlink_registry_topology_entry_t> entries (count);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, &entries[0], &count));

    bool saw_ready = false;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].state == ZLINK_TOPOLOGY_STATE_READY) {
            saw_ready = true;
            break;
        }
    }
    TEST_ASSERT_TRUE (saw_ready);

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
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
