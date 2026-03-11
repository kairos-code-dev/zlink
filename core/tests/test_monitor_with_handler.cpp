/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <atomic>
#include <errno.h>
#include <stdio.h>
#include <string.h>

namespace
{
void discard_socket_message (const zlink_routing_id_t *,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
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
}

SETUP_TEARDOWN_TESTCONTEXT

void test_socket_monitor_set_handler_replaces_dispatch_and_blocks_recv ()
{
    void *ctx = get_test_context ();
    void *server =
      zlink_socket_with_handler (ctx, ZLINK_ROUTER, &discard_socket_message);
    void *client =
      zlink_socket_with_handler (ctx, ZLINK_DEALER, &discard_socket_message);
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

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_monitor_set_handler (monitor, &raw_monitor_replacement_handler));
    close_socket_zero_linger (client);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.replacement_calls, 1, 3000));
    TEST_ASSERT_EQUAL_UINT64 (ZLINK_EVENT_DISCONNECTED,
                              probe.replacement_event.event);

    zlink_socket_monitor (server, NULL, 0);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    close_socket_zero_linger (server);
    g_raw_monitor_probe = NULL;
}

void test_socket_monitor_open_requires_handler ()
{
    void *ctx = get_test_context ();
    void *server =
      zlink_socket_with_handler (ctx, ZLINK_ROUTER, &discard_socket_message);
    TEST_ASSERT_NOT_NULL (server);

    TEST_ASSERT_NULL (
      zlink_socket_monitor_open (server, ZLINK_EVENT_CONNECTION_READY, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    close_socket_zero_linger (server);
}

void test_service_monitor_set_handler_replaces_dispatch_and_blocks_recv ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *registry = NULL;
    setup_registry (ctx, &registry, "inproc://handler-svcmon-pub",
                    "inproc://handler-svcmon-router");

    service_monitor_probe_t probe;
    g_service_monitor_probe = &probe;

    void *discovery = zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-handler", 12));

    void *monitor =
      zlink_discovery_monitor_open (discovery, ZLINK_DISCOVERY_SERVICE_UP
                                               | ZLINK_MONITOR_EVENT_CLOSED,
                                    &service_monitor_primary_handler);
    TEST_ASSERT_NOT_NULL (monitor);

    connect_discovery_registry_with_retry (discovery,
                                           "inproc://handler-svcmon-router",
                                           3000);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_subscribe (discovery, "svc-handler"));

    void *server_discovery =
      zlink_discovery_new_typed (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      server_discovery, "inproc://handler-svcmon-router"));
    void *gateway = zlink_gateway_new (ctx, server_discovery, "rx-handler",
                                       &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (gateway);

    char endpoint[MAX_SOCKET_STRING];
    snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
              test_port (22610));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_bind (gateway, endpoint));
    register_gateway_with_timeout (gateway, "svc-handler", endpoint, 1, 3000);

    TEST_ASSERT_TRUE (wait_for_calls (&probe.primary_calls, 1, 3000));
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY,
                              probe.primary_event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_DISCOVERY_SERVICE_UP,
                              probe.primary_event.event_type);
    TEST_ASSERT_EQUAL_STRING ("svc-handler", probe.primary_event.service_name);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_set_handler (
      monitor, &service_monitor_replacement_handler));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));

    TEST_ASSERT_TRUE (wait_for_calls (&probe.replacement_calls, 1, 3000));
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_MONITOR_EVENT_CLOSED,
                              probe.replacement_event.event_type);

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
    RUN_TEST (test_socket_monitor_set_handler_replaces_dispatch_and_blocks_recv);
    RUN_TEST (
      test_service_monitor_set_handler_replaces_dispatch_and_blocks_recv);
    return UNITY_END ();
}
