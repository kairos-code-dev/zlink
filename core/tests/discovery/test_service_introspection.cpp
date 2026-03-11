/* SPDX-License-Identifier: MPL-2.0 */

#include "../testutil.hpp"
#include "../testutil_unity.hpp"

#include <atomic>
#include <chrono>
#include <string.h>
#include <vector>

static bool should_run_named_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    return !selected || !*selected || strcmp (selected, name_) == 0;
}

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

void discard_gateway_message (const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
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

void *create_gateway_attached (void *ctx_,
                               void *discovery_,
                               const char *service_name_,
                               const char *routing_id_,
                               zlink_socket_msg_handler_fn handler_)
{
    void *gateway =
      zlink_gateway_new (ctx_, service_name_, routing_id_, handler_);
    if (!gateway)
        return NULL;
    if (zlink_gateway_attach_discovery (gateway, discovery_) != 0) {
        const int err = errno;
        zlink_gateway_destroy (&gateway);
        errno = err;
        return NULL;
    }
    return gateway;
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
                                    int expected_min_,
                                    int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_gateway_connection_count (gateway_) >= expected_min_)
            return true;
        msleep (10);
    }
    return false;
}

bool wait_for_topology_state (void *registry_,
                              zlink_service_kind_t service_kind_,
                              const char *service_name_,
                              const zlink_routing_id_t *routing_id_,
                              zlink_topology_state_t state_,
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

void make_registry_endpoint (char *endpoint_out_,
                             size_t endpoint_size_,
                             int port_seed_)
{
    snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
              test_port (port_seed_));
}

void make_registry_endpoint_transport (char *endpoint_out_,
                                       size_t endpoint_size_,
                                       const char *transport_,
                                       int port_seed_)
{
    snprintf (endpoint_out_, endpoint_size_, "%s://127.0.0.1:%d", transport_,
              test_port (port_seed_));
}

void set_registry_tls_server_opts (void *registry_,
                                   zlink_registry_socket_role_t role_,
                                   const tls_test_files_t &files_)
{
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_setsockopt (
      registry_, role_, ZLINK_SOCKOPT_TLS_CERT, files_.server_cert.c_str (),
      files_.server_cert.size () + 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_setsockopt (
      registry_, role_, ZLINK_SOCKOPT_TLS_KEY, files_.server_key.c_str (),
      files_.server_key.size () + 1));
}

void set_discovery_tls_client_opts (void *discovery_,
                                    const tls_test_files_t &files_)
{
    const int trust_system = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_setsockopt (
      discovery_, ZLINK_DISCOVERY_SOCKET_SUB, ZLINK_SOCKOPT_TLS_CA,
      files_.ca_cert.c_str (), files_.ca_cert.size () + 1));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_setsockopt (
      discovery_, ZLINK_DISCOVERY_SOCKET_SUB, ZLINK_SOCKOPT_TLS_HOSTNAME,
      "localhost", sizeof ("localhost")));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_setsockopt (
      discovery_, ZLINK_DISCOVERY_SOCKET_SUB, ZLINK_SOCKOPT_TLS_TRUST_SYSTEM,
      &trust_system, sizeof (trust_system)));
}

int connect_discovery_registry_with_retry (void *discovery_,
                                           const char *endpoint_,
                                           int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_discovery_connect_registry (discovery_, endpoint_) == 0)
            return 0;
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (step_ms);
    }
    errno = EAGAIN;
    return -1;
}

void bind_gateway_with_timeout (void *gateway_,
                                const char *endpoint_,
                                int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_gateway_bind (gateway_, endpoint_) == 0)
            return;
        if (errno != EAGAIN)
            break;
        msleep (step_ms);
    }
    TEST_FAIL_MESSAGE ("gateway bind timeout");
}

void init_gateway_server (gateway_server_t *server_,
                          void *ctx_,
                          const char *registry_ep_,
                          const char *service_name_,
                          const char *routing_id_,
                          const char *bind_ep_,
                          zlink_socket_msg_handler_fn handler_)
{
    server_->discovery =
      zlink_discovery_new (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      server_->discovery, registry_ep_, 2000));
    server_->gateway =
      create_gateway_attached (ctx_, server_->discovery, service_name_, routing_id_,
                         handler_);
    TEST_ASSERT_NOT_NULL (server_->gateway);
    bind_gateway_with_timeout (server_->gateway, bind_ep_, 3000);
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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 22640);
    make_registry_endpoint (registry_router, sizeof (registry_router), 22641);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
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

    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svcmon"));

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22600));
    init_gateway_server (&server, ctx, registry_router, "svcmon", "svcmon-gw",
                         endpoint,
                         &discard_gateway_message);

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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 22642);
    make_registry_endpoint (registry_router, sizeof (registry_router), 22643);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      client_discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (client_discovery, "svc-int-opt"));

    void *client =
      create_gateway_attached (ctx, client_discovery, "svc-int-opt", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22602));
    init_gateway_server (&server, ctx, registry_router, "svc-int-opt",
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

    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (client_discovery, "svc-int-opt", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (client, 1, 3000));

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
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 22644);
    make_registry_endpoint (registry_router, sizeof (registry_router), 22645);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      client_discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (client_discovery, "svc-int"));

    void *client =
      create_gateway_attached (ctx, client_discovery, "svc-int", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22601));
    server.discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server.discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      server.discovery, registry_router, 2000));
    server.gateway =
      create_gateway_attached (ctx, server.discovery, "svc-int", NULL,
                               &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (server.gateway);

    monitor_probe_t server_probe;
    g_monitor_b = &server_probe;
    void *client_monitor = zlink_gateway_monitor_open (
      client, ZLINK_GATEWAY_SERVICE_READY | ZLINK_GATEWAY_ROUTE_UP,
      &service_monitor_handler_a);
    void *server_monitor = zlink_gateway_monitor_open (
      server.gateway, ZLINK_GATEWAY_SERVICE_READY,
      &service_monitor_handler_b);
    TEST_ASSERT_NOT_NULL (client_monitor);
    TEST_ASSERT_NOT_NULL (server_monitor);
    bind_gateway_with_timeout (server.gateway, endpoint, 3000);

    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (client_discovery, "svc-int", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (client, 1, 3000));
    TEST_ASSERT_TRUE (wait_for_calls (&server_probe.calls, 1, 3000));

    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_GATEWAY,
                              server_probe.last_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_GATEWAY_SERVICE_READY,
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

static void test_registry_topology_snapshot_and_remote_query ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 22646);
    make_registry_endpoint (registry_router, sizeof (registry_router), 22647);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22603));
    init_gateway_server (&server, ctx, registry_router, "svc-topology",
                         "gw-topology", endpoint,
                         &discard_gateway_message);
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
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_query_client_connect (client, registry_router));
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_query_snapshot (client, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);
    std::vector<zlink_registry_topology_entry_t> remote_entries (count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_snapshot (
      client, &filter, &remote_entries[0], &count));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_STATE_READY,
                              remote_entries[0].state);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server.gateway));
    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_GATEWAY, "svc-topology", &gateway_rid,
      ZLINK_TOPOLOGY_STATE_STOPPED, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server.discovery));

    memset (&filter, 0, sizeof (filter));
    filter.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
    filter.state = ZLINK_TOPOLOGY_STATE_STOPPED;
    filter.routing_id = gateway_rid;
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_topology_query (registry, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_monitor_closed_event_on_service_destroy ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
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

static void test_discovery_registry_transport_restriction ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_discovery_connect_registry (discovery, "inproc://not-allowed"));
    TEST_ASSERT_EQUAL_INT (EPROTONOSUPPORT, zlink_errno ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
}

static void test_discovery_registry_transport_allowed_ws ()
{
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WS transport unavailable");
    }

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint_transport (registry_pub, sizeof (registry_pub), "ws",
                                      22659);
    make_registry_endpoint_transport (registry_router, sizeof (registry_router),
                                      "ws", 22660);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery, registry_router, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_discovery_registry_transport_allowed_tls ()
{
    if (!zlink_has ("tls")) {
        TEST_IGNORE_MESSAGE ("TLS transport unavailable");
    }

    const tls_test_files_t files = make_tls_test_files ();

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint_transport (registry_pub, sizeof (registry_pub), "tls",
                                      22661);
    make_registry_endpoint_transport (registry_router, sizeof (registry_router),
                                      "tls", 22662);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_endpoints (
      registry, registry_pub, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry, 50));
    set_registry_tls_server_opts (registry, ZLINK_REGISTRY_SOCKET_PUB, files);
    set_registry_tls_server_opts (registry, ZLINK_REGISTRY_SOCKET_ROUTER, files);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry));

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    set_discovery_tls_client_opts (discovery, files);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery, registry_router, 2000));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    cleanup_tls_test_files (files);
}

static void test_registry_peer_transport_restriction_and_tcp_sync ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry_a = zlink_registry_new (ctx);
    void *registry_b = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry_a);
    TEST_ASSERT_NOT_NULL (registry_b);
    char registry_a_pub[MAX_SOCKET_STRING];
    char registry_a_router[MAX_SOCKET_STRING];
    char registry_b_pub[MAX_SOCKET_STRING];
    char registry_b_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_a_pub, sizeof (registry_a_pub), 22654);
    make_registry_endpoint (registry_a_router, sizeof (registry_a_router), 22655);
    make_registry_endpoint (registry_b_pub, sizeof (registry_b_pub), 22656);
    make_registry_endpoint (registry_b_router, sizeof (registry_b_router), 22657);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry_a, registry_a_pub, registry_a_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_endpoints (registry_b, registry_b_pub, registry_b_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry_a, 50));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry_b, 50));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_registry_add_peer (registry_a, "inproc://not-allowed"));
    TEST_ASSERT_EQUAL_INT (EPROTONOSUPPORT, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_add_peer (registry_a, registry_b_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry_b));

    void *discovery_a = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery_a, registry_a_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery_a, "svc-peer-sync"));

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22658));
    init_gateway_server (&server, ctx, registry_b_router, "svc-peer-sync",
                         "gw-peer-sync", endpoint, &discard_gateway_message);

    zlink_routing_id_t gateway_rid;
    memset (&gateway_rid, 0, sizeof (gateway_rid));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_routing_id (server.gateway, &gateway_rid));

    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (discovery_a, "svc-peer-sync", 1, 10000));

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_a));
}

static void test_registry_peer_transport_mixed_ws_sync ()
{
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WS transport unavailable");
    }

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry_a = zlink_registry_new (ctx);
    void *registry_b = zlink_registry_new (ctx);
    TEST_ASSERT_NOT_NULL (registry_a);
    TEST_ASSERT_NOT_NULL (registry_b);

    char registry_a_pub[MAX_SOCKET_STRING];
    char registry_a_router[MAX_SOCKET_STRING];
    char registry_b_pub[MAX_SOCKET_STRING];
    char registry_b_router[MAX_SOCKET_STRING];
    make_registry_endpoint_transport (registry_a_pub, sizeof (registry_a_pub),
                                      "tcp", 22663);
    make_registry_endpoint_transport (registry_a_router,
                                      sizeof (registry_a_router), "tcp", 22664);
    make_registry_endpoint_transport (registry_b_pub, sizeof (registry_b_pub),
                                      "ws", 22665);
    make_registry_endpoint_transport (registry_b_router,
                                      sizeof (registry_b_router), "ws", 22666);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_endpoints (
      registry_a, registry_a_pub, registry_a_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_set_endpoints (
      registry_b, registry_b_pub, registry_b_router));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry_a, 50));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_set_broadcast_interval (registry_b, 50));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_add_peer (registry_a, registry_b_pub));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_start (registry_b));

    void *discovery_a = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery_a);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery_a, registry_a_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery_a, "svc-peer-mixed-ws"));

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22667));
    init_gateway_server (&server, ctx, registry_b_router, "svc-peer-mixed-ws",
                         "gw-peer-mixed-ws", endpoint,
                         &discard_gateway_message);

    TEST_ASSERT_TRUE (wait_discovery_receiver_count (
      discovery_a, "svc-peer-mixed-ws", 1, 10000));

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_a));
}

static void test_registry_topology_reports_discovery_and_gateway ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    make_registry_endpoint (registry_pub, sizeof (registry_pub), 22648);
    make_registry_endpoint (registry_router, sizeof (registry_router), 22649);
    setup_registry (ctx, &registry, registry_pub, registry_router);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-topology", 13));
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-gw-topology"));

    void *client =
      create_gateway_attached (ctx, discovery, "svc-gw-topology", "gw-topology",
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22604));
    init_gateway_server (&server, ctx, registry_router, "svc-gw-topology",
                         "gw-server-topology", endpoint,
                         &discard_gateway_message);
    TEST_ASSERT_TRUE (
      wait_discovery_receiver_count (discovery, "svc-gw-topology", 1, 3000));
    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (client, 1, 3000));

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
#define RUN_SERVICE_INTROSPECTION_TEST(name)                                   \
    do {                                                                       \
        if (should_run_named_test (#name))                                     \
            RUN_TEST (name);                                                   \
    } while (0)
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_monitor_and_routing_id);
    RUN_SERVICE_INTROSPECTION_TEST (test_gateway_receiver_routing_ids_and_options);
    RUN_SERVICE_INTROSPECTION_TEST (test_gateway_receiver_monitors_and_monitor_poller);
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_topology_snapshot_and_remote_query);
    RUN_SERVICE_INTROSPECTION_TEST (test_monitor_closed_event_on_service_destroy);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_registry_transport_restriction);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_registry_transport_allowed_ws);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_registry_transport_allowed_tls);
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_peer_transport_restriction_and_tcp_sync);
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_peer_transport_mixed_ws_sync);
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_topology_reports_discovery_and_gateway);
#undef RUN_SERVICE_INTROSPECTION_TEST
    return UNITY_END ();
}
