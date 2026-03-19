/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string.h>
#include <thread>
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

struct event_sequence_probe_t
{
    std::mutex sync;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
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
event_sequence_probe_t *g_event_sequence_probe = NULL;
void *g_discovery_self_close_subject = NULL;
std::atomic<int> *g_discovery_self_close_calls = NULL;
int g_discovery_self_close_rc = 0;
int g_discovery_self_close_errno = 0;

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
                              size_t part_count_,
                          void *)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void service_monitor_handler_a (const zlink_service_event_t *event_, void *)
{
    if (!g_monitor_a || !event_)
        return;
    g_monitor_a->last_event = *event_;
    g_monitor_a->calls.fetch_add (1);
}

void service_monitor_handler_b (const zlink_service_event_t *event_, void *)
{
    if (!g_monitor_b || !event_)
        return;
    g_monitor_b->last_event = *event_;
    g_monitor_b->calls.fetch_add (1);
}

void service_monitor_handler_c (const zlink_service_event_t *event_, void *)
{
    if (!g_monitor_c || !event_)
        return;
    g_monitor_c->last_event = *event_;
    g_monitor_c->calls.fetch_add (1);
}

void service_monitor_sequence_handler (const zlink_service_event_t *event_, void *)
{
    if (!g_event_sequence_probe || !event_)
        return;

    std::lock_guard<std::mutex> lock (g_event_sequence_probe->sync);
    g_event_sequence_probe->events.push_back (*event_);
    g_event_sequence_probe->cv.notify_all ();
}

void discovery_monitor_parent_self_close_handler (const zlink_service_event_t *, void *)
{
    if (g_discovery_self_close_calls)
        g_discovery_self_close_calls->fetch_add (1);

    void *discovery = g_discovery_self_close_subject;
    errno = 0;
    g_discovery_self_close_rc = zlink_discovery_destroy (&discovery);
    g_discovery_self_close_errno = errno;
}

bool read_gateway_snapshot (void *gateway_, zlink_monitor_snapshot_t *out_)
{
    if (!gateway_ || !out_)
        return false;

    void *monitor = zlink_gateway_monitor_open (
      gateway_,
      ZLINK_GATEWAY_SERVICE_READY | ZLINK_GATEWAY_SERVICE_LOST
        | ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
        | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &zlink_service_monitor_ignore_handler, NULL);
    if (!monitor)
        return false;

    const int rc = zlink_monitor_snapshot (monitor, out_);
    zlink_service_monitor_close (&monitor);
    return rc == 0;
}

void *create_gateway_attached (void *ctx_,
                               void *discovery_,
                               const char *service_name_,
                               const char *routing_id_,
                               zlink_socket_msg_handler_fn handler_)
{
    void *gateway = zlink_gateway_new (ctx_, service_name_);
    if (!gateway)
        return NULL;
    if (routing_id_
        && zlink_set_routing_id (gateway, routing_id_,
                                         strlen (routing_id_))
             != 0) {
        const int err = errno;
        zlink_gateway_destroy (&gateway);
        errno = err;
        return NULL;
    }
    if (handler_ && zlink_recv_handler (gateway, handler_, NULL) != 0) {
        const int err = errno;
        zlink_gateway_destroy (&gateway);
        errno = err;
        return NULL;
    }
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

bool wait_for_event_sequence_count (event_sequence_probe_t *probe_,
                                    size_t expected_,
                                    int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->sync);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_] () { return probe_->events.size () >= expected_; });
}

bool wait_gateway_connection_count (void *gateway_,
                                    int expected_min_,
                                    int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (read_gateway_snapshot (gateway_, &snapshot)
            && static_cast<int> (snapshot.ready_peer_count) >= expected_min_) {
            return true;
        }
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

bool wait_for_gateway_peer_state (void *registry_,
                                  const zlink_routing_id_t *gateway_rid_,
                                  const zlink_routing_id_t *peer_rid_,
                                  zlink_topology_state_t state_,
                                  int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_registry_gateway_peer_filter_t filter;
        memset (&filter, 0, sizeof (filter));
        filter.state = state_;
        if (gateway_rid_)
            filter.gateway_routing_id = *gateway_rid_;
        if (peer_rid_)
            filter.peer_routing_id = *peer_rid_;

        size_t count = 0;
        if (zlink_registry_gateway_peers_query (registry_, &filter, NULL, &count)
              == 0
            && count > 0) {
            std::vector<zlink_registry_gateway_peer_entry_t> entries (count);
            if (zlink_registry_gateway_peers_query (registry_, &filter,
                                                    &entries[0], &count)
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
      zlink_registry_set_broadcast_interval (registry, 50));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, pub_ep_, router_ep_));
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
    LIBZLINK_UNUSED (role_);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_tls_server (
      registry_, files_.server_cert.c_str (), files_.server_key.c_str (), 0));
}

void set_discovery_tls_client_opts (void *discovery_,
                                    const tls_test_files_t &files_)
{
    const int trust_system = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_set_tls_client (
      discovery_, files_.ca_cert.c_str (), "localhost", trust_system));
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

void *create_started_registry_with_port_seed (void *ctx_,
                                               int *port_seed_,
                                               char *pub_ep_out_,
                                               size_t pub_size_,
                                               char *router_ep_out_,
                                               size_t router_size_)
{
    for (int attempt = 0; attempt < 32; ++attempt) {
        void *registry = zlink_registry_new (ctx_);
        if (!registry)
            return NULL;
        snprintf (pub_ep_out_, pub_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_));
        snprintf (router_ep_out_, router_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_ + 1));
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, pub_ep_out_, router_ep_out_)
                 == 0) {
            *port_seed_ += 2;
            return registry;
        }
        zlink_registry_destroy (&registry);
        *port_seed_ += 2;
    }
    errno = EADDRINUSE;
    return NULL;
}

void *create_started_registry_with_port_seed_transport (
  void *ctx_,
  const char *transport_,
  int *port_seed_,
  char *pub_ep_out_,
  size_t pub_size_,
  char *router_ep_out_,
  size_t router_size_)
{
    for (int attempt = 0; attempt < 32; ++attempt) {
        void *registry = zlink_registry_new (ctx_);
        if (!registry)
            return NULL;
        snprintf (pub_ep_out_, pub_size_, "%s://127.0.0.1:%d", transport_,
                  test_port (*port_seed_));
        snprintf (router_ep_out_, router_size_, "%s://127.0.0.1:%d",
                  transport_, test_port (*port_seed_ + 1));
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, pub_ep_out_, router_ep_out_)
                 == 0) {
            *port_seed_ += 2;
            return registry;
        }
        zlink_registry_destroy (&registry);
        *port_seed_ += 2;
    }
    errno = EADDRINUSE;
    return NULL;
}

void bind_gateway_with_port_seed (void *gateway_,
                                   const char *transport_,
                                   int *port_seed_,
                                   char *endpoint_out_,
                                   size_t endpoint_size_,
                                   int timeout_ms_)
{
    for (int port_attempt = 0; port_attempt < 32; ++port_attempt) {
        snprintf (endpoint_out_, endpoint_size_, "%s://127.0.0.1:%d",
                  transport_, test_port (*port_seed_));
        const int step_ms = 10;
        const int attempts = timeout_ms_ / step_ms;
        for (int i = 0; i < attempts; ++i) {
            if (zlink_gateway_bind (gateway_, endpoint_out_) == 0) {
                *port_seed_ += 1;
                return;
            }
            if (errno == EADDRINUSE)
                break;
            if (errno != EAGAIN)
                break;
            msleep (step_ms);
        }
        if (errno != EADDRINUSE)
            break;
        *port_seed_ += 1;
    }
    TEST_FAIL_MESSAGE ("gateway bind with port seed timeout");
}

void init_gateway_server_with_port_seed (gateway_server_t *server_,
                                          void *ctx_,
                                          const char *registry_ep_,
                                          const char *service_name_,
                                          const char *routing_id_,
                                          const char *transport_,
                                          int *port_seed_,
                                          char *endpoint_out_,
                                          size_t endpoint_size_,
                                          zlink_socket_msg_handler_fn handler_)
{
    server_->discovery =
      zlink_discovery_new (ctx_, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_->discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      server_->discovery, registry_ep_, 2000));
    server_->gateway =
      create_gateway_attached (ctx_, server_->discovery, service_name_,
                               routing_id_, handler_);
    TEST_ASSERT_NOT_NULL (server_->gateway);
    bind_gateway_with_port_seed (server_->gateway, transport_, port_seed_,
                                  endpoint_out_, endpoint_size_, 3000);
}
}

static void test_discovery_monitor_and_routing_id ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22640;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (discovery, "disc-mon", 8));

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (discovery, &rid));
    assert_routing_id_bytes (&rid, "disc-mon");

    monitor_probe_t probe;
    g_monitor_a = &probe;
    void *monitor =
      zlink_discovery_monitor_open (discovery, ZLINK_DISCOVERY_SERVICE_UP,
                                    &service_monitor_handler_a, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22600;
    init_gateway_server_with_port_seed (&server, ctx, registry_router, "svcmon",
                                         "svcmon-gw", "tcp", &bind_seed,
                                         endpoint, sizeof (endpoint),
                                         &discard_gateway_message);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 3000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY,
                              probe.last_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_DISCOVERY_SERVICE_UP,
                              probe.last_event.event_type);
    TEST_ASSERT_EQUAL_STRING ("svcmon", probe.last_event.service_name);
    assert_routing_id_bytes (&probe.last_event.routing_id, "disc-mon");

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1,
                           zlink_set_routing_id (discovery, "late", 4));
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

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22642;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      client_discovery, registry_router, 2000));
    void *client =
      create_gateway_attached (ctx, client_discovery, "svc-int-opt", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22602;
    init_gateway_server_with_port_seed (&server, ctx, registry_router,
                                         "svc-int-opt", "gw-server-int",
                                         "tcp", &bind_seed, endpoint,
                                         sizeof (endpoint),
                                         &discard_gateway_message);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (client, "gw-int", 6));
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (client, &rid));
    assert_routing_id_bytes (&rid, "gw-int");
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (server.gateway, &rid));
    assert_routing_id_bytes (&rid, "gw-server-int");

    const int hwm = 321;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (client, ZLINK_OPT_SNDHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (client, ZLINK_OPT_RCVHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server.gateway, ZLINK_OPT_SNDHWM, &hwm,
                                sizeof (hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_option (server.gateway, ZLINK_OPT_RCVHWM, &hwm,
                                sizeof (hwm)));

    TEST_ASSERT_TRUE (
      wait_gateway_connection_count (client, 1, 3000));

    errno = 0;
    TEST_ASSERT_EQUAL_INT (-1, zlink_set_routing_id (client, "late", 4));
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

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22644;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      client_discovery, registry_router, 2000));
    void *client =
      create_gateway_attached (ctx, client_discovery, "svc-int", NULL,
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22601;
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
      client, ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP,
      &service_monitor_handler_a, NULL);
    void *server_monitor = zlink_gateway_monitor_open (
      server.gateway, ZLINK_GATEWAY_SERVICE_READY,
      &service_monitor_handler_b, NULL);
    TEST_ASSERT_NOT_NULL (client_monitor);
    TEST_ASSERT_NOT_NULL (server_monitor);
    bind_gateway_with_port_seed (server.gateway, "tcp", &bind_seed, endpoint,
                                  sizeof (endpoint), 3000);

    TEST_ASSERT_TRUE (wait_for_calls (&server_probe.calls, 1, 10000));

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

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22646;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22603;
    init_gateway_server_with_port_seed (&server, ctx, registry_router,
                                         "svc-topology", "gw-topology",
                                         "tcp", &bind_seed, endpoint,
                                         sizeof (endpoint),
                                         &discard_gateway_message);
    zlink_routing_id_t gateway_rid;
    memset (&gateway_rid, 0, sizeof (gateway_rid));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_routing_id (server.gateway, &gateway_rid));

    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_GATEWAY, "svc-topology", &gateway_rid,
      ZLINK_TOPOLOGY_STATE_READY, 10000));

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
      ZLINK_TOPOLOGY_STATE_STOPPED, 10000));
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

static void test_registry_gateway_peer_snapshot_and_remote_query ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22666;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22623;
    init_gateway_server_with_port_seed (&server, ctx, registry_router,
                                        "svc-peer-query", "gw-peer-server",
                                        "tcp", &bind_seed, endpoint,
                                        sizeof (endpoint),
                                        &discard_gateway_message);

    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      client_discovery, registry_router, 2000));
    void *client_gateway =
      create_gateway_attached (ctx, client_discovery, "svc-peer-query",
                               "gw-peer-client", &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client_gateway);

    TEST_ASSERT_TRUE (wait_gateway_connection_count (client_gateway, 1, 3000));

    zlink_routing_id_t client_rid;
    zlink_routing_id_t server_rid;
    memset (&client_rid, 0, sizeof (client_rid));
    memset (&server_rid, 0, sizeof (server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_routing_id (client_gateway, &client_rid));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_get_routing_id (server.gateway, &server_rid));

    TEST_ASSERT_TRUE (wait_for_gateway_peer_state (
      registry, &client_rid, &server_rid, ZLINK_TOPOLOGY_STATE_READY, 3000));

    zlink_registry_gateway_peer_filter_t filter;
    memset (&filter, 0, sizeof (filter));
    filter.gateway_routing_id = client_rid;
    filter.peer_routing_id = server_rid;
    filter.state = ZLINK_TOPOLOGY_STATE_READY;

    size_t count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_gateway_peers_query (registry, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);

    std::vector<zlink_registry_gateway_peer_entry_t> entries (count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_gateway_peers_query (
      registry, &filter, &entries[0], &count));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_STATE_READY, entries[0].state);
    TEST_ASSERT_EQUAL_STRING ("svc-peer-query", entries[0].service_name);
    TEST_ASSERT_EQUAL_STRING (endpoint, entries[0].peer_endpoint);
    TEST_ASSERT_EQUAL_UINT32 (0, entries[0].weight);

    void *client = zlink_registry_query_client_new (ctx);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_query_client_connect (client, registry_router));
    count = 0;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_gateway_peers_snapshot (
      client, &filter, NULL, &count));
    TEST_ASSERT_EQUAL_UINT (1, count);

    std::vector<zlink_registry_gateway_peer_entry_t> remote_entries (count);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_gateway_peers_snapshot (
      client, &filter, &remote_entries[0], &count));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_TOPOLOGY_STATE_READY,
                              remote_entries[0].state);
    TEST_ASSERT_EQUAL_STRING (endpoint, remote_entries[0].peer_endpoint);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client_gateway));
    TEST_ASSERT_TRUE (wait_for_gateway_peer_state (
      registry, &client_rid, &server_rid, ZLINK_TOPOLOGY_STATE_STOPPED, 3000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_query_destroy (&client));
    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_monitor_closed_event_on_service_destroy ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (discovery, "disc-close", 10));

    monitor_probe_t probe;
    g_monitor_a = &probe;
    void *monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_MONITOR_EVENT_CLOSED, &service_monitor_handler_a, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_EQUAL_INT (-1, zlink_discovery_destroy (&discovery));
    TEST_ASSERT_EQUAL_INT (EBUSY, zlink_errno ());
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    g_monitor_a = NULL;
}

static void test_discovery_monitor_reports_service_up_then_down_in_order ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22668;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (discovery, "disc-order", 10));

    event_sequence_probe_t probe;
    g_event_sequence_probe = &probe;
    void *monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_DISCOVERY_SERVICE_UP | ZLINK_DISCOVERY_SERVICE_DOWN,
      &service_monitor_sequence_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22624;
    init_gateway_server_with_port_seed (&server, ctx, registry_router,
                                        "svc-ordering", "gw-ordering", "tcp",
                                        &bind_seed, endpoint,
                                        sizeof (endpoint),
                                        &discard_gateway_message);

    TEST_ASSERT_TRUE (wait_for_event_sequence_count (&probe, 1, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server.gateway));
    TEST_ASSERT_TRUE (wait_for_event_sequence_count (&probe, 2, 10000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server.discovery));

    {
        std::lock_guard<std::mutex> lock (probe.sync);
        TEST_ASSERT_GREATER_OR_EQUAL_UINT (2, probe.events.size ());

        bool saw_up = false;
        bool saw_down_after_up = false;
        for (size_t i = 0; i < probe.events.size (); ++i) {
            if (strcmp (probe.events[i].service_name, "svc-ordering") != 0)
                continue;
            if (probe.events[i].event_type == ZLINK_DISCOVERY_SERVICE_UP)
                saw_up = true;
            if (saw_up
                && probe.events[i].event_type == ZLINK_DISCOVERY_SERVICE_DOWN) {
                saw_down_after_up = true;
                break;
            }
        }
        TEST_ASSERT_TRUE (saw_up);
        TEST_ASSERT_TRUE (saw_down_after_up);
    }

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_event_sequence_probe = NULL;
}

static void test_discovery_monitor_callback_parent_destroy_returns_ebusy ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22670;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (discovery, "disc-self-destroy", 17));

    std::atomic<int> callback_calls (0);
    g_discovery_self_close_subject = discovery;
    g_discovery_self_close_calls = &callback_calls;
    g_discovery_self_close_rc = 0;
    g_discovery_self_close_errno = 0;

    void *monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_DISCOVERY_SERVICE_UP,
      &discovery_monitor_parent_self_close_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22626;
    init_gateway_server_with_port_seed (&server, ctx, registry_router,
                                        "svc-self-destroy",
                                        "gw-self-destroy", "tcp", &bind_seed,
                                        endpoint, sizeof (endpoint),
                                        &discard_gateway_message);

    TEST_ASSERT_TRUE (wait_for_calls (&callback_calls, 1, 5000));
    TEST_ASSERT_EQUAL_INT (-1, g_discovery_self_close_rc);
    TEST_ASSERT_EQUAL_INT (EBUSY, g_discovery_self_close_errno);

    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_get_routing_id (discovery, &rid));
    assert_routing_id_bytes (&rid, "disc-self-destroy");

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_discovery_self_close_subject = NULL;
    g_discovery_self_close_calls = NULL;
}

static void test_registry_control_path_queries_are_safe_during_concurrent_updates ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22743;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    gateway_server_t server;
    int bind_seed = 22843;
    char bind_endpoint[MAX_SOCKET_STRING];
    init_gateway_server_with_port_seed (
      &server, ctx, registry_router, "svc-registry-ctrl", "gw-registry-ctrl",
      "tcp", &bind_seed, bind_endpoint, sizeof (bind_endpoint),
      &discard_gateway_message);

    TEST_ASSERT_TRUE (wait_for_topology_state (
      registry, ZLINK_SERVICE_KIND_GATEWAY, "svc-registry-ctrl", NULL,
      ZLINK_TOPOLOGY_STATE_READY, 3000));

    std::atomic<int> update_failures (0);
    std::atomic<int> query_failures (0);
    std::atomic<int> query_iterations (0);

    std::thread updater ([&] () {
        for (uint32_t i = 0; i < 128; ++i) {
            if (zlink_registry_set_id (registry, 1000u + i) != 0) {
                update_failures.fetch_add (1);
                return;
            }
            msleep (1);
        }
    });

    std::thread reader ([&] () {
        zlink_registry_topology_filter_t filter;
        memset (&filter, 0, sizeof (filter));
        filter.service_kind = ZLINK_SERVICE_KIND_GATEWAY;
        strncpy (filter.service_name, "svc-registry-ctrl",
                 sizeof (filter.service_name) - 1);

        for (int i = 0; i < 128; ++i) {
            size_t count = 0;
            if (zlink_registry_topology_query (registry, &filter, NULL, &count)
                != 0) {
                query_failures.fetch_add (1);
                return;
            }

            if (count > 0) {
                std::vector<zlink_registry_topology_entry_t> entries (count);
                if (zlink_registry_topology_query (registry, &filter,
                                                   &entries[0], &count)
                    != 0) {
                    query_failures.fetch_add (1);
                    return;
                }
            }

            query_iterations.fetch_add (1);
            msleep (1);
        }
    });

    updater.join ();
    reader.join ();

    TEST_ASSERT_EQUAL_INT (0, update_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, query_failures.load ());
    TEST_ASSERT_GREATER_THAN_INT (0, query_iterations.load ());

    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
}

static void test_discovery_control_path_reads_are_safe_during_concurrent_updates ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);

    std::atomic<int> update_failures (0);
    std::atomic<int> read_failures (0);
    std::atomic<int> read_iterations (0);
    const int iterations = 16;

    std::thread updater ([&] () {
        char routing_id[32];
        for (int i = 0; i < iterations; ++i) {
            snprintf (routing_id, sizeof (routing_id), "disc-%03d", i);
            if (zlink_set_routing_id (discovery, routing_id,
                                                strlen (routing_id))
                != 0) {
                update_failures.fetch_add (1);
                return;
            }
        }
    });

    std::thread reader ([&] () {
        for (int i = 0; i < iterations; ++i) {
            zlink_routing_id_t rid;
            memset (&rid, 0, sizeof (rid));
            errno = 0;
            const int rc = zlink_get_routing_id (discovery, &rid);
            const int err = zlink_errno ();
            if (rc != 0 && err != EAGAIN && err != 0) {
                read_failures.fetch_add (1);
                return;
            }
            read_iterations.fetch_add (1);
        }
    });

    updater.join ();
    reader.join ();

    TEST_ASSERT_EQUAL_INT (0, update_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, read_failures.load ());
    TEST_ASSERT_GREATER_THAN_INT (0, read_iterations.load ());
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
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

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22659;
    void *registry = create_started_registry_with_port_seed_transport (
      ctx, "ws", &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

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

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22661;
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        void *r = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (r);
        snprintf (registry_pub, sizeof (registry_pub), "tls://127.0.0.1:%d",
                  test_port (registry_seed));
        snprintf (registry_router, sizeof (registry_router),
                  "tls://127.0.0.1:%d", test_port (registry_seed + 1));
        if (zlink_registry_set_broadcast_interval (r, 50) != 0) {
            zlink_registry_destroy (&r);
            registry_seed += 2;
            continue;
        }
        set_registry_tls_server_opts (r, ZLINK_REGISTRY_SOCKET_PUB, files);
        set_registry_tls_server_opts (r, ZLINK_REGISTRY_SOCKET_ROUTER, files);
        if (zlink_registry_bind (r, registry_pub, registry_router) == 0) {
            registry = r;
            registry_seed += 2;
            break;
        }
        zlink_registry_destroy (&r);
        registry_seed += 2;
    }
    TEST_ASSERT_NOT_NULL (registry);

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

    char registry_a_pub[MAX_SOCKET_STRING];
    char registry_a_router[MAX_SOCKET_STRING];
    char registry_b_pub[MAX_SOCKET_STRING];
    char registry_b_router[MAX_SOCKET_STRING];
    int peer_seed_a = 22654;
    int peer_seed_b = 22656;
    void *registry_a = NULL;
    void *registry_b = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        void *ra = zlink_registry_new (ctx);
        void *rb = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (ra);
        TEST_ASSERT_NOT_NULL (rb);
        snprintf (registry_a_pub, sizeof (registry_a_pub),
                  "tcp://127.0.0.1:%d", test_port (peer_seed_a));
        snprintf (registry_a_router, sizeof (registry_a_router),
                  "tcp://127.0.0.1:%d", test_port (peer_seed_a + 1));
        snprintf (registry_b_pub, sizeof (registry_b_pub),
                  "tcp://127.0.0.1:%d", test_port (peer_seed_b));
        snprintf (registry_b_router, sizeof (registry_b_router),
                  "tcp://127.0.0.1:%d", test_port (peer_seed_b + 1));
        if (zlink_registry_set_broadcast_interval (ra, 50) != 0
            || zlink_registry_set_broadcast_interval (rb, 50) != 0
            || zlink_registry_add_peer (ra, registry_b_pub) != 0
            || zlink_registry_bind (rb, registry_b_pub, registry_b_router)
                 != 0
            || zlink_registry_bind (ra, registry_a_pub, registry_a_router)
                 != 0) {
            zlink_registry_destroy (&ra);
            zlink_registry_destroy (&rb);
            peer_seed_a += 2;
            peer_seed_b += 2;
            continue;
        }
        registry_a = ra;
        registry_b = rb;
        peer_seed_a += 2;
        peer_seed_b += 2;
        break;
    }
    TEST_ASSERT_NOT_NULL (registry_a);
    TEST_ASSERT_NOT_NULL (registry_b);

    errno = 0;
    TEST_ASSERT_EQUAL_INT (
      -1, zlink_registry_add_peer (registry_a, "inproc://not-allowed"));
    TEST_ASSERT_EQUAL_INT (EPROTONOSUPPORT, zlink_errno ());

    void *discovery_a = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery_a);
    monitor_probe_t probe;
    g_monitor_a = &probe;
    void *monitor =
      zlink_discovery_monitor_open (discovery_a, ZLINK_DISCOVERY_SERVICE_UP,
                                    &service_monitor_handler_a, NULL);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery_a, registry_a_router, 2000));
    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22658;
    init_gateway_server_with_port_seed (&server, ctx, registry_b_router,
                                         "svc-peer-sync", "gw-peer-sync",
                                         "tcp", &bind_seed, endpoint,
                                         sizeof (endpoint),
                                         &discard_gateway_message);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 10000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY,
                              probe.last_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_DISCOVERY_SERVICE_UP,
                              probe.last_event.event_type);
    TEST_ASSERT_EQUAL_STRING ("svc-peer-sync", probe.last_event.service_name);
    TEST_ASSERT_TRUE (probe.last_event.value > 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_a));
    g_monitor_a = NULL;
}

static void test_registry_peer_transport_mixed_ws_sync ()
{
    if (!zlink_has ("ws")) {
        TEST_IGNORE_MESSAGE ("WS transport unavailable");
    }

    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_a_pub[MAX_SOCKET_STRING];
    char registry_a_router[MAX_SOCKET_STRING];
    char registry_b_pub[MAX_SOCKET_STRING];
    char registry_b_router[MAX_SOCKET_STRING];
    int peer_seed_a = 22663;
    int peer_seed_b = 22665;
    void *registry_a = NULL;
    void *registry_b = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        void *ra = zlink_registry_new (ctx);
        void *rb = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (ra);
        TEST_ASSERT_NOT_NULL (rb);
        snprintf (registry_a_pub, sizeof (registry_a_pub),
                  "tcp://127.0.0.1:%d", test_port (peer_seed_a));
        snprintf (registry_a_router, sizeof (registry_a_router),
                  "tcp://127.0.0.1:%d", test_port (peer_seed_a + 1));
        snprintf (registry_b_pub, sizeof (registry_b_pub),
                  "ws://127.0.0.1:%d", test_port (peer_seed_b));
        snprintf (registry_b_router, sizeof (registry_b_router),
                  "ws://127.0.0.1:%d", test_port (peer_seed_b + 1));
        if (zlink_registry_set_broadcast_interval (ra, 50) != 0
            || zlink_registry_set_broadcast_interval (rb, 50) != 0
            || zlink_registry_add_peer (ra, registry_b_pub) != 0
            || zlink_registry_bind (rb, registry_b_pub, registry_b_router)
                 != 0
            || zlink_registry_bind (ra, registry_a_pub, registry_a_router)
                 != 0) {
            zlink_registry_destroy (&ra);
            zlink_registry_destroy (&rb);
            peer_seed_a += 2;
            peer_seed_b += 2;
            continue;
        }
        registry_a = ra;
        registry_b = rb;
        peer_seed_a += 2;
        peer_seed_b += 2;
        break;
    }
    TEST_ASSERT_NOT_NULL (registry_a);
    TEST_ASSERT_NOT_NULL (registry_b);

    void *discovery_a = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery_a);
    monitor_probe_t probe;
    g_monitor_a = &probe;
    void *monitor =
      zlink_discovery_monitor_open (discovery_a, ZLINK_DISCOVERY_SERVICE_UP,
                                    &service_monitor_handler_a, NULL);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (connect_discovery_registry_with_retry (
      discovery_a, registry_a_router, 2000));
    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22667;
    init_gateway_server_with_port_seed (&server, ctx, registry_b_router,
                                         "svc-peer-mixed-ws",
                                         "gw-peer-mixed-ws", "tcp",
                                         &bind_seed, endpoint,
                                         sizeof (endpoint),
                                         &discard_gateway_message);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.calls, 1, 10000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY,
                              probe.last_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_DISCOVERY_SERVICE_UP,
                              probe.last_event.event_type);
    TEST_ASSERT_EQUAL_STRING ("svc-peer-mixed-ws",
                              probe.last_event.service_name);
    TEST_ASSERT_TRUE (probe.last_event.value > 0);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    destroy_gateway_server (&server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery_a));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_b));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry_a));
    g_monitor_a = NULL;
}

static void test_registry_topology_reports_discovery_and_gateway ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22648;
    void *registry = create_started_registry_with_port_seed (
      ctx, &registry_seed, registry_pub, sizeof (registry_pub),
      registry_router, sizeof (registry_router));
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_set_routing_id (discovery, "disc-topology", 13));
    TEST_ASSERT_SUCCESS_ERRNO (
      connect_discovery_registry_with_retry (discovery, registry_router, 2000));
    void *client =
      create_gateway_attached (ctx, discovery, "svc-gw-topology", "gw-topology",
                         &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (client);

    gateway_server_t server;
    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22604;
    init_gateway_server_with_port_seed (&server, ctx, registry_router,
                                         "svc-gw-topology",
                                         "gw-server-topology", "tcp",
                                         &bind_seed, endpoint,
                                         sizeof (endpoint),
                                         &discard_gateway_message);
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
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_gateway_peer_snapshot_and_remote_query);
    RUN_SERVICE_INTROSPECTION_TEST (test_monitor_closed_event_on_service_destroy);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_monitor_reports_service_up_then_down_in_order);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_monitor_callback_parent_destroy_returns_ebusy);
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_control_path_queries_are_safe_during_concurrent_updates);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_control_path_reads_are_safe_during_concurrent_updates);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_registry_transport_restriction);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_registry_transport_allowed_ws);
    RUN_SERVICE_INTROSPECTION_TEST (test_discovery_registry_transport_allowed_tls);
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_peer_transport_restriction_and_tcp_sync);
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_peer_transport_mixed_ws_sync);
    RUN_SERVICE_INTROSPECTION_TEST (test_registry_topology_reports_discovery_and_gateway);
#undef RUN_SERVICE_INTROSPECTION_TEST
    return UNITY_END ();
}
