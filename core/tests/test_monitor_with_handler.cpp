/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <atomic>
#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace
{
zlink_socket_handler_t make_msg_handler (zlink_socket_msg_handler_fn fn_)
{
    zlink_socket_handler_t handler;
    memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_MSG;
    handler.fn.msg = fn_;
    return handler;
}

void discard_socket_message (const zlink_routing_id_t *,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void discard_gateway_message (const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

struct raw_monitor_probe_t
{
    raw_monitor_probe_t () : primary_calls (0), replacement_calls (0)
    {
        memset (&primary_event, 0, sizeof (primary_event));
        memset (&replacement_event, 0, sizeof (replacement_event));
    }

    std::atomic<int> primary_calls;
    std::atomic<int> replacement_calls;
    zlink_monitor_event_t primary_event;
    zlink_monitor_event_t replacement_event;
};

struct service_monitor_probe_t
{
    service_monitor_probe_t () : primary_calls (0), replacement_calls (0)
    {
        memset (&primary_event, 0, sizeof (primary_event));
        memset (&replacement_event, 0, sizeof (replacement_event));
    }

    std::atomic<int> primary_calls;
    std::atomic<int> replacement_calls;
    zlink_service_event_t primary_event;
    zlink_service_event_t replacement_event;
};

raw_monitor_probe_t *g_raw_monitor_probe = NULL;
service_monitor_probe_t *g_service_monitor_probe = NULL;

bool wait_for_calls (std::atomic<int> *calls_, int expected_, int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;

    for (int i = 0; i < attempts; ++i) {
        if (calls_->load () >= expected_)
            return true;
        msleep (step_ms);
    }

    return calls_->load () >= expected_;
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

void raw_monitor_primary_handler (const zlink_monitor_event_t *event_)
{
    if (!g_raw_monitor_probe || !event_)
        return;
    g_raw_monitor_probe->primary_event = *event_;
    g_raw_monitor_probe->primary_calls.fetch_add (1);
}

void raw_monitor_replacement_handler (const zlink_monitor_event_t *event_)
{
    if (!g_raw_monitor_probe || !event_)
        return;
    g_raw_monitor_probe->replacement_event = *event_;
    g_raw_monitor_probe->replacement_calls.fetch_add (1);
}

void service_monitor_primary_handler (const zlink_service_event_t *event_)
{
    if (!g_service_monitor_probe || !event_)
        return;
    g_service_monitor_probe->primary_event = *event_;
    g_service_monitor_probe->primary_calls.fetch_add (1);
}

void service_monitor_replacement_handler (const zlink_service_event_t *event_)
{
    if (!g_service_monitor_probe || !event_)
        return;
    g_service_monitor_probe->replacement_event = *event_;
    g_service_monitor_probe->replacement_calls.fetch_add (1);
}

void close_socket_zero_linger (void *socket_)
{
    const int linger = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket_));
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

void connect_discovery_registry_with_retry (void *discovery_,
                                            const char *endpoint_,
                                            int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        if (zlink_discovery_connect_registry (discovery_, endpoint_) == 0)
            return;
        TEST_ASSERT_EQUAL_INT (EAGAIN, zlink_errno ());
        msleep (step_ms);
    }
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (discovery_, endpoint_));
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
}

SETUP_TEARDOWN_TESTCONTEXT

void test_socket_monitor_open_dispatches_events ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *server =
      zlink_socket (ctx, ZLINK_ROUTER, &msg_handler);
    void *client =
      zlink_socket (ctx, ZLINK_DEALER, &msg_handler);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    raw_monitor_probe_t probe;
    g_raw_monitor_probe = &probe;

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED,
      &raw_monitor_primary_handler);
    TEST_ASSERT_NOT_NULL (monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.primary_calls, 1, 3000));
    TEST_ASSERT_EQUAL_UINT64 (ZLINK_EVENT_CONNECTION_READY,
                              probe.primary_event.event);
    TEST_ASSERT_TRUE (probe.primary_event.routing_id.size > 0);

    close_socket_zero_linger (client);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.primary_calls, 2, 3000));
    TEST_ASSERT_EQUAL_UINT64 (ZLINK_EVENT_DISCONNECTED,
                              probe.primary_event.event);

    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    close_socket_zero_linger (server);
    g_raw_monitor_probe = NULL;
}

void test_socket_monitor_open_requires_handler ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *server =
      zlink_socket (ctx, ZLINK_ROUTER, &msg_handler);
    TEST_ASSERT_NOT_NULL (server);

    TEST_ASSERT_NULL (
      zlink_socket_monitor_open (server, ZLINK_EVENT_CONNECTION_READY, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    close_socket_zero_linger (server);
}

void test_service_monitor_open_dispatches_events ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22612;
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        registry = zlink_registry_new (ctx);
        if (!registry)
            break;
        snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
                  test_port (registry_seed));
        snprintf (registry_router, sizeof (registry_router),
                  "tcp://127.0.0.1:%d", test_port (registry_seed + 1));
        if (zlink_registry_set_endpoints (registry, registry_pub,
                                          registry_router)
              == 0
            && zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_start (registry) == 0) {
            registry_seed += 2;
            break;
        }
        zlink_registry_destroy (&registry);
        registry = NULL;
        registry_seed += 2;
    }
    TEST_ASSERT_NOT_NULL (registry);

    service_monitor_probe_t probe;
    g_service_monitor_probe = &probe;

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-handler", 12));

    void *monitor =
      zlink_discovery_monitor_open (discovery, ZLINK_DISCOVERY_SERVICE_UP
                                               | ZLINK_MONITOR_EVENT_CLOSED,
                                    &service_monitor_primary_handler);
    TEST_ASSERT_NOT_NULL (monitor);

    connect_discovery_registry_with_retry (discovery, registry_router, 3000);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-handler"));

    void *server_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      server_discovery, registry_router));
    void *gateway = create_gateway_attached (ctx, server_discovery, "svc-handler",
                                       "rx-handler", &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (gateway);

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22610;
    for (int ba = 0; ba < 32; ++ba) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (bind_seed));
        bool bound = false;
        for (int bi = 0; bi < 300; ++bi) {
            if (zlink_gateway_bind (gateway, endpoint) == 0) {
                bound = true;
                break;
            }
            if (errno == EADDRINUSE)
                break;
            if (errno != EAGAIN)
                break;
            msleep (10);
        }
        if (bound)
            break;
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway bind failed");
        bind_seed += 1;
    }

    TEST_ASSERT_TRUE (wait_for_calls (&probe.primary_calls, 1, 3000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY,
                              probe.primary_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_DISCOVERY_SERVICE_UP,
                              probe.primary_event.event_type);
    TEST_ASSERT_EQUAL_STRING ("svc-handler", probe.primary_event.service_name);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));

    TEST_ASSERT_TRUE (wait_for_calls (&probe.primary_calls, 2, 3000));
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_MONITOR_EVENT_CLOSED,
                              probe.primary_event.event_type);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_service_monitor_probe = NULL;
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_socket_monitor_open_requires_handler);
    RUN_TEST (test_socket_monitor_open_dispatches_events);
    RUN_TEST (test_service_monitor_open_dispatches_events);
    return UNITY_END ();
}
