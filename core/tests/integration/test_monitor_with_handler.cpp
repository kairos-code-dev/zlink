/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"
#include "../../src/sockets/socket_base.hpp"

#include <atomic>
#include <climits>
#include <condition_variable>
#include <errno.h>
#include <mutex>
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

zlink_socket_handler_t make_spot_handler (zlink_spot_handler_fn fn_)
{
    zlink_socket_handler_t handler;
    memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_SPOT;
    handler.fn.spot = fn_;
    return handler;
}

void discard_socket_message (const zlink_routing_id_t *,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                          void *)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void discard_gateway_message (const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                          void *)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void discard_spot_message (const zlink_routing_id_t *,
                           const char *,
                           size_t,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                          void *)
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

struct callback_close_probe_t
{
    callback_close_probe_t () :
        monitor (NULL),
        close_rc (INT_MIN),
        close_errno (0),
        entered (false),
        returned (false),
        proceed (false),
        wait_completed (false)
    {
    }

    void *monitor;
    int close_rc;
    int close_errno;
    bool entered;
    bool returned;
    bool proceed;
    bool wait_completed;
    std::mutex sync;
    std::condition_variable cv;
};

struct monitor_parent_query_probe_t
{
    monitor_parent_query_probe_t () :
        discovery (NULL),
        calls (0),
        query_routing_id_size (0),
        query_errno (0),
        done (false)
    {
        service_name[0] = '\0';
        memset (&event, 0, sizeof (event));
        memset (&query_routing_id, 0, sizeof (query_routing_id));
    }

    void *discovery;
    std::atomic<int> calls;
    zlink_routing_id_t query_routing_id;
    size_t query_routing_id_size;
    int query_errno;
    bool done;
    char service_name[64];
    zlink_service_event_t event;
    std::mutex sync;
    std::condition_variable cv;
};

raw_monitor_probe_t *g_raw_monitor_probe = NULL;
service_monitor_probe_t *g_service_monitor_probe = NULL;
callback_close_probe_t *g_raw_monitor_self_close_probe = NULL;
callback_close_probe_t *g_raw_monitor_blocking_probe = NULL;
callback_close_probe_t *g_service_monitor_self_close_probe = NULL;
callback_close_probe_t *g_service_monitor_blocking_probe = NULL;
monitor_parent_query_probe_t *g_service_monitor_query_probe = NULL;
void *g_raw_send_ready_subject = NULL;
std::atomic<int> *g_raw_send_ready_calls = NULL;
int g_raw_send_ready_rc = 0;
int g_raw_send_ready_errno = 0;

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

bool wait_for_probe_flag (callback_close_probe_t *probe_,
                          bool callback_close_probe_t::*flag_,
                          int timeout_ms_)
{
    if (!probe_ || !flag_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->sync);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, flag_]() { return probe_->*flag_; });
}

void *create_gateway_attached (void *ctx_,
                               void *discovery_,
                               const char *service_name_,
                               const char *routing_id_,
                               zlink_socket_msg_handler_fn handler_)
{
    void *gateway =
      zlink_gateway_new (ctx_, service_name_, routing_id_, handler_, NULL);
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

void raw_monitor_primary_handler (const zlink_monitor_event_t *event_, void *)
{
    if (!g_raw_monitor_probe || !event_)
        return;
    g_raw_monitor_probe->primary_event = *event_;
    g_raw_monitor_probe->primary_calls.fetch_add (1);
}

void raw_monitor_replacement_handler (const zlink_monitor_event_t *event_, void *)
{
    if (!g_raw_monitor_probe || !event_)
        return;
    g_raw_monitor_probe->replacement_event = *event_;
    g_raw_monitor_probe->replacement_calls.fetch_add (1);
}

void service_monitor_primary_handler (const zlink_service_event_t *event_, void *)
{
    if (!g_service_monitor_probe || !event_)
        return;
    g_service_monitor_probe->primary_event = *event_;
    g_service_monitor_probe->primary_calls.fetch_add (1);
}

void service_monitor_replacement_handler (const zlink_service_event_t *event_, void *)
{
    if (!g_service_monitor_probe || !event_)
        return;
    g_service_monitor_probe->replacement_event = *event_;
    g_service_monitor_probe->replacement_calls.fetch_add (1);
}

void service_monitor_parent_query_handler (const zlink_service_event_t *event_, void *)
{
    monitor_parent_query_probe_t *probe = g_service_monitor_query_probe;
    if (!probe || !event_)
        return;

    errno = 0;
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    const int rc = zlink_discovery_routing_id (probe->discovery, &rid);
    const int query_errno = rc == 0 ? 0 : errno;

    {
        std::lock_guard<std::mutex> lock (probe->sync);
        probe->event = *event_;
        probe->query_routing_id = rid;
        probe->query_routing_id_size = rc == 0 ? rid.size : 0;
        probe->query_errno = query_errno;
        probe->done = true;
    }

    probe->calls.fetch_add (1);
    probe->cv.notify_all ();
}

void raw_monitor_self_close_handler (const zlink_monitor_event_t *, void *)
{
    callback_close_probe_t *probe = g_raw_monitor_self_close_probe;
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->sync);
        probe->entered = true;
        probe->cv.notify_all ();
    }

    errno = 0;
    const int rc = zlink_close (probe->monitor);
    const int err = errno;

    {
        std::lock_guard<std::mutex> lock (probe->sync);
        if (rc == 0)
            probe->monitor = NULL;
        probe->close_rc = rc;
        probe->close_errno = err;
        probe->returned = true;
        probe->cv.notify_all ();
    }
}

void raw_monitor_blocking_handler (const zlink_monitor_event_t *, void *)
{
    callback_close_probe_t *probe = g_raw_monitor_blocking_probe;
    if (!probe)
        return;

    std::unique_lock<std::mutex> lock (probe->sync);
    probe->entered = true;
    probe->cv.notify_all ();
    probe->wait_completed = probe->cv.wait_for (
      lock, std::chrono::seconds (5), [probe]() { return probe->proceed; });
    probe->returned = true;
    probe->cv.notify_all ();
}

void service_monitor_self_close_handler (const zlink_service_event_t *, void *)
{
    callback_close_probe_t *probe = g_service_monitor_self_close_probe;
    if (!probe)
        return;

    {
        std::lock_guard<std::mutex> lock (probe->sync);
        probe->entered = true;
        probe->cv.notify_all ();
    }

    errno = 0;
    const int rc = zlink_service_monitor_close (&probe->monitor);
    const int err = errno;

    {
        std::lock_guard<std::mutex> lock (probe->sync);
        probe->close_rc = rc;
        probe->close_errno = err;
        probe->returned = true;
        probe->cv.notify_all ();
    }
}

void service_monitor_blocking_handler (const zlink_service_event_t *, void *)
{
    callback_close_probe_t *probe = g_service_monitor_blocking_probe;
    if (!probe)
        return;

    std::unique_lock<std::mutex> lock (probe->sync);
    probe->entered = true;
    probe->cv.notify_all ();
    probe->wait_completed = probe->cv.wait_for (
      lock, std::chrono::seconds (5), [probe]() { return probe->proceed; });
    probe->returned = true;
    probe->cv.notify_all ();
}

void raw_send_ready_reentrant_handler (void *subject_, void *)
{
    g_raw_send_ready_subject = subject_;
    if (g_raw_send_ready_calls)
        g_raw_send_ready_calls->fetch_add (1);
    errno = 0;
    g_raw_send_ready_rc = zlink_socket_set_send_ready_handler (
      subject_, &raw_send_ready_reentrant_handler, NULL);
    g_raw_send_ready_errno = errno;
}

void raw_send_ready_counting_handler (void *subject_, void *)
{
    g_raw_send_ready_subject = subject_;
    if (g_raw_send_ready_calls)
        g_raw_send_ready_calls->fetch_add (1);
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
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        registry = zlink_registry_new (ctx_);
        TEST_ASSERT_NOT_NULL (registry);
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, pub_ep_, router_ep_) == 0) {
            *registry_out_ = registry;
            return;
        }

        const int err = errno;
        zlink_registry_destroy (&registry);
        if (err != EADDRINUSE) {
            errno = err;
            TEST_FAIL_MESSAGE ("registry setup failed");
        }
        msleep (10);
    }

    TEST_FAIL_MESSAGE ("registry setup timeout");
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

void bind_gateway_with_port_seed (void *gateway_,
                                  int *port_seed_,
                                  char *endpoint_out_,
                                  size_t endpoint_size_,
                                  int timeout_ms_)
{
    TEST_ASSERT_NOT_NULL (gateway_);
    TEST_ASSERT_NOT_NULL (port_seed_);
    TEST_ASSERT_NOT_NULL (endpoint_out_);
    TEST_ASSERT_TRUE (endpoint_size_ > 0);

    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint_out_, endpoint_size_, "tcp://127.0.0.1:%d",
                  test_port (*port_seed_));
        if (zlink_gateway_bind (gateway_, endpoint_out_) == 0)
            return;
        if (errno != EADDRINUSE && errno != EAGAIN)
            break;
        ++(*port_seed_);
    }

    bind_gateway_with_timeout (gateway_, endpoint_out_, timeout_ms_);
}
}

SETUP_TEARDOWN_TESTCONTEXT

void test_socket_monitor_open_dispatches_events ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *server = zlink_socket (ctx, ZLINK_ROUTER);
    void *client = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (server, &msg_handler));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (client, &msg_handler));

    raw_monitor_probe_t probe;
    g_raw_monitor_probe = &probe;

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED,
      &raw_monitor_primary_handler, NULL);
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

void test_socket_send_ready_handler_reentrant_replace_returns_edeadlk ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *socket = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (socket);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (socket, &msg_handler));

    std::atomic<int> ready_calls (0);
    g_raw_send_ready_subject = NULL;
    g_raw_send_ready_calls = &ready_calls;
    g_raw_send_ready_rc = 0;
    g_raw_send_ready_errno = 0;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_set_send_ready_handler (
      socket, &raw_send_ready_reentrant_handler, NULL));

    zlink::socket_base_t *socket_base =
      static_cast<zlink::socket_base_t *> (socket);
    socket_base->invoke_send_ready_handler_for_testing ();

    TEST_ASSERT_EQUAL_INT (1, ready_calls.load ());
    TEST_ASSERT_EQUAL_PTR (socket, g_raw_send_ready_subject);
    TEST_ASSERT_EQUAL_INT (-1, g_raw_send_ready_rc);
    TEST_ASSERT_EQUAL_INT (EDEADLK, g_raw_send_ready_errno);

    close_socket_zero_linger (socket);
    g_raw_send_ready_calls = NULL;
    g_raw_send_ready_subject = NULL;
}

void test_socket_send_ready_handler_requires_handler ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *socket = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (socket);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (socket, &msg_handler));

    TEST_ASSERT_EQUAL_INT (-1, zlink_socket_set_send_ready_handler (socket, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    close_socket_zero_linger (socket);
}

void test_socket_send_ready_handler_failed_replace_keeps_previous_handler ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *socket = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (socket);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (socket, &msg_handler));

    std::atomic<int> ready_calls (0);
    g_raw_send_ready_subject = NULL;
    g_raw_send_ready_calls = &ready_calls;

    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_set_send_ready_handler (
      socket, &raw_send_ready_counting_handler, NULL));
    TEST_ASSERT_EQUAL_INT (-1, zlink_socket_set_send_ready_handler (socket, NULL, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    zlink::socket_base_t *socket_base =
      static_cast<zlink::socket_base_t *> (socket);
    socket_base->invoke_send_ready_handler_for_testing ();

    TEST_ASSERT_EQUAL_INT (1, ready_calls.load ());
    TEST_ASSERT_EQUAL_PTR (socket, g_raw_send_ready_subject);

    close_socket_zero_linger (socket);
    g_raw_send_ready_calls = NULL;
    g_raw_send_ready_subject = NULL;
}

void test_socket_send_ready_handler_rejects_sub_and_xsub ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t spot_handler =
      make_spot_handler (&discard_spot_message);
    void *sub = zlink_socket (ctx, ZLINK_SUB);
    void *xsub = zlink_socket (ctx, ZLINK_XSUB);
    TEST_ASSERT_NOT_NULL (sub);
    TEST_ASSERT_NOT_NULL (xsub);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (sub, &spot_handler));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (xsub, &spot_handler));

    TEST_ASSERT_EQUAL_INT (
      -1, zlink_socket_set_send_ready_handler (sub, &raw_send_ready_counting_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);
    TEST_ASSERT_EQUAL_INT (
      -1,
      zlink_socket_set_send_ready_handler (xsub, &raw_send_ready_counting_handler, NULL));
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    close_socket_zero_linger (sub);
    close_socket_zero_linger (xsub);
}

void test_socket_monitor_open_requires_handler ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *server = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (server, &msg_handler));

    errno = 0;
    void *monitor =
      zlink_socket_monitor_open (server, ZLINK_EVENT_CONNECTION_READY, NULL, NULL);
    TEST_ASSERT_NULL (monitor);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    close_socket_zero_linger (server);
}

void test_socket_monitor_open_accepts_ignore_handler ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *server = zlink_socket (ctx, ZLINK_ROUTER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (server, &msg_handler));

    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY, &zlink_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);
    close_socket_zero_linger (monitor);

    close_socket_zero_linger (server);
}

void test_discovery_monitor_open_requires_handler ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);

    errno = 0;
    void *monitor =
      zlink_discovery_monitor_open (discovery, ZLINK_DISCOVERY_SERVICE_UP, NULL, NULL);
    TEST_ASSERT_NULL (monitor);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
}

void test_discovery_monitor_open_accepts_ignore_handler ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);

    void *monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_DISCOVERY_SERVICE_UP,
      &zlink_service_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
}

void test_gateway_monitor_open_requires_handler ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *gateway =
      zlink_gateway_new (ctx, "gw-monitor-null", "gw-monitor-null",
                         &discard_gateway_message, NULL);
    TEST_ASSERT_NOT_NULL (gateway);

    errno = 0;
    void *monitor = zlink_gateway_monitor_open (
      gateway, ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_READY, NULL, NULL);
    TEST_ASSERT_NULL (monitor);
    TEST_ASSERT_EQUAL_INT (EINVAL, errno);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
}

void test_gateway_monitor_open_accepts_ignore_handler ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *gateway =
      zlink_gateway_new (ctx, "gw-monitor-ignore", "gw-monitor-ignore",
                         &discard_gateway_message, NULL);
    TEST_ASSERT_NOT_NULL (gateway);

    void *monitor = zlink_gateway_monitor_open (
      gateway, ZLINK_GATEWAY_MONITOR_EVENT_SERVICE_READY,
      &zlink_service_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
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
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, registry_pub, registry_router)
                 == 0) {
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
                                    &service_monitor_primary_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    connect_discovery_registry_with_retry (discovery, registry_router, 3000);
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

    TEST_ASSERT_EQUAL_INT (-1, zlink_discovery_destroy (&discovery));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_service_monitor_probe = NULL;
}

void test_socket_monitor_self_close_defers_until_callback_return ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *server = zlink_socket (ctx, ZLINK_ROUTER);
    void *client = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (server, &msg_handler));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (client, &msg_handler));

    callback_close_probe_t probe;
    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY, &raw_monitor_self_close_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);
    probe.monitor = monitor;
    g_raw_monitor_self_close_probe = &probe;

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    TEST_ASSERT_TRUE (wait_for_probe_flag (&probe, &callback_close_probe_t::returned,
                                           3000));
    TEST_ASSERT_TRUE (wait_for_probe_flag (&probe, &callback_close_probe_t::entered,
                                           0));
    TEST_ASSERT_EQUAL_INT (0, probe.close_rc);
    TEST_ASSERT_EQUAL_PTR (NULL, probe.monitor);

    close_socket_zero_linger (client);
    close_socket_zero_linger (server);
    g_raw_monitor_self_close_probe = NULL;
}

void test_socket_monitor_close_during_callback_returns_ebusy ()
{
    void *ctx = get_test_context ();
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&discard_socket_message);
    void *server = zlink_socket (ctx, ZLINK_ROUTER);
    void *client = zlink_socket (ctx, ZLINK_DEALER);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (server, &msg_handler));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_socket_attach_handler (client, &msg_handler));

    callback_close_probe_t probe;
    void *monitor = zlink_socket_monitor_open (
      server, ZLINK_EVENT_CONNECTION_READY, &raw_monitor_blocking_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);
    probe.monitor = monitor;
    g_raw_monitor_blocking_probe = &probe;

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));
    TEST_ASSERT_TRUE (wait_for_probe_flag (&probe, &callback_close_probe_t::entered,
                                           3000));

    TEST_ASSERT_EQUAL_INT (-1, zlink_close (monitor));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);

    {
        std::lock_guard<std::mutex> lock (probe.sync);
        probe.proceed = true;
        probe.cv.notify_all ();
    }

    TEST_ASSERT_TRUE (wait_for_probe_flag (&probe, &callback_close_probe_t::returned,
                                           3000));
    TEST_ASSERT_TRUE (probe.wait_completed);

    close_socket_zero_linger (monitor);
    close_socket_zero_linger (client);
    close_socket_zero_linger (server);
    g_raw_monitor_blocking_probe = NULL;
}

void test_service_monitor_self_close_defers_until_callback_return ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22632;
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        registry = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (registry);
        snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
                  test_port (registry_seed));
        snprintf (registry_router, sizeof (registry_router),
                  "tcp://127.0.0.1:%d", test_port (registry_seed + 1));
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, registry_pub, registry_router)
                 == 0) {
            break;
        }
        zlink_registry_destroy (&registry);
        registry = NULL;
        registry_seed += 2;
    }
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-self-close", 15));

    callback_close_probe_t probe;
    probe.monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_DISCOVERY_SERVICE_UP,
      &service_monitor_self_close_handler, NULL);
    TEST_ASSERT_NOT_NULL (probe.monitor);
    g_service_monitor_self_close_probe = &probe;

    connect_discovery_registry_with_retry (discovery, registry_router, 3000);
    void *server_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_discovery, registry_router));
    void *gateway = create_gateway_attached (ctx, server_discovery,
                                             "svc-self-close", "rx-self-close",
                                             &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (gateway);

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22634;
    bind_gateway_with_port_seed (gateway, &bind_seed, endpoint, sizeof (endpoint),
                                 3000);

    TEST_ASSERT_TRUE (wait_for_probe_flag (&probe, &callback_close_probe_t::returned,
                                           3000));
    TEST_ASSERT_EQUAL_INT (0, probe.close_rc);
    TEST_ASSERT_EQUAL_PTR (NULL, probe.monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_service_monitor_self_close_probe = NULL;
}

void test_service_monitor_close_during_callback_returns_ebusy ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22642;
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        registry = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (registry);
        snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
                  test_port (registry_seed));
        snprintf (registry_router, sizeof (registry_router),
                  "tcp://127.0.0.1:%d", test_port (registry_seed + 1));
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, registry_pub, registry_router)
                 == 0) {
            break;
        }
        zlink_registry_destroy (&registry);
        registry = NULL;
        registry_seed += 2;
    }
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-close-ebusy", 17));

    callback_close_probe_t probe;
    probe.monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_DISCOVERY_SERVICE_UP, &service_monitor_blocking_handler, NULL);
    TEST_ASSERT_NOT_NULL (probe.monitor);
    g_service_monitor_blocking_probe = &probe;

    connect_discovery_registry_with_retry (discovery, registry_router, 3000);
    void *server_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_discovery, registry_router));
    void *gateway = create_gateway_attached (
      ctx, server_discovery, "svc-close-ebusy", "rx-close-ebusy",
      &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (gateway);

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22644;
    bind_gateway_with_port_seed (gateway, &bind_seed, endpoint, sizeof (endpoint),
                                 3000);
    TEST_ASSERT_TRUE (wait_for_probe_flag (&probe, &callback_close_probe_t::entered,
                                           3000));

    TEST_ASSERT_EQUAL_INT (-1, zlink_service_monitor_close (&probe.monitor));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    TEST_ASSERT_NOT_NULL (probe.monitor);

    {
        std::lock_guard<std::mutex> lock (probe.sync);
        probe.proceed = true;
        probe.cv.notify_all ();
    }

    TEST_ASSERT_TRUE (wait_for_probe_flag (&probe, &callback_close_probe_t::returned,
                                           3000));
    TEST_ASSERT_TRUE (probe.wait_completed);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&probe.monitor));
    TEST_ASSERT_EQUAL_PTR (NULL, probe.monitor);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_service_monitor_blocking_probe = NULL;
}

void test_service_monitor_callback_can_query_parent_without_deadlock ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22652;
    void *registry = NULL;
    for (int attempt = 0; attempt < 32; ++attempt) {
        registry = zlink_registry_new (ctx);
        TEST_ASSERT_NOT_NULL (registry);
        snprintf (registry_pub, sizeof (registry_pub), "tcp://127.0.0.1:%d",
                  test_port (registry_seed));
        snprintf (registry_router, sizeof (registry_router),
                  "tcp://127.0.0.1:%d", test_port (registry_seed + 1));
        if (zlink_registry_set_broadcast_interval (registry, 50) == 0
            && zlink_registry_bind (registry, registry_pub, registry_router)
                 == 0) {
            break;
        }
        zlink_registry_destroy (&registry);
        registry = NULL;
        registry_seed += 2;
    }
    TEST_ASSERT_NOT_NULL (registry);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_set_routing_id (discovery, "disc-query", 10));

    monitor_parent_query_probe_t probe;
    probe.discovery = discovery;
    strncpy (probe.service_name, "svc-query-monitor",
             sizeof (probe.service_name) - 1);
    probe.service_name[sizeof (probe.service_name) - 1] = '\0';
    g_service_monitor_query_probe = &probe;

    void *monitor =
      zlink_discovery_monitor_open (discovery, ZLINK_DISCOVERY_SERVICE_UP,
                                    &service_monitor_parent_query_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    connect_discovery_registry_with_retry (discovery, registry_router, 3000);
    void *server_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (server_discovery);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_discovery_connect_registry (server_discovery, registry_router));
    void *gateway =
      create_gateway_attached (ctx, server_discovery, probe.service_name,
                               "rx-query-monitor", &discard_gateway_message);
    TEST_ASSERT_NOT_NULL (gateway);

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22654;
    bind_gateway_with_port_seed (gateway, &bind_seed, endpoint, sizeof (endpoint),
                                 3000);

    {
        std::unique_lock<std::mutex> lock (probe.sync);
        TEST_ASSERT_TRUE (probe.cv.wait_for (
          lock, std::chrono::seconds (5), [&probe]() { return probe.done; }));
    }

    TEST_ASSERT_EQUAL_INT (1, probe.calls.load ());
    TEST_ASSERT_EQUAL_UINT16 (ZLINK_SERVICE_KIND_DISCOVERY,
                              probe.event.service_kind);
    TEST_ASSERT_EQUAL_UINT32 (ZLINK_DISCOVERY_SERVICE_UP,
                              probe.event.event_type);
    TEST_ASSERT_EQUAL_STRING (probe.service_name, probe.event.service_name);
    TEST_ASSERT_EQUAL_INT (0, probe.query_errno);
    TEST_ASSERT_TRUE (probe.query_routing_id_size > 0);
    TEST_ASSERT_EQUAL_UINT8 (probe.event.routing_id.size,
                             probe.query_routing_id.size);
    TEST_ASSERT_EQUAL_MEMORY (probe.event.routing_id.data,
                              probe.query_routing_id.data,
                              probe.query_routing_id.size);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_service_monitor_query_probe = NULL;
}

void test_service_parent_destroy_rejects_open_monitor_children ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *discovery = zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (discovery);
    void *discovery_monitor = zlink_discovery_monitor_open (
      discovery, ZLINK_DISCOVERY_SERVICE_UP, &zlink_service_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (discovery_monitor);

    TEST_ASSERT_EQUAL_INT (-1, zlink_discovery_destroy (&discovery));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    TEST_ASSERT_NOT_NULL (discovery);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&discovery_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&discovery));

    void *gateway =
      zlink_gateway_new (ctx, "svc-monitor-destroy", "gw-monitor-destroy",
                         &discard_gateway_message, NULL);
    TEST_ASSERT_NOT_NULL (gateway);
    void *gateway_monitor = zlink_gateway_monitor_open (
      gateway, ZLINK_GATEWAY_SERVICE_READY, &zlink_service_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (gateway_monitor);

    TEST_ASSERT_EQUAL_INT (-1, zlink_gateway_destroy (&gateway));
    TEST_ASSERT_EQUAL_INT (EBUSY, errno);
    TEST_ASSERT_NOT_NULL (gateway);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&gateway_monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&gateway));
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_socket_monitor_open_requires_handler);
    RUN_TEST (test_socket_monitor_open_accepts_ignore_handler);
    RUN_TEST (test_discovery_monitor_open_requires_handler);
    RUN_TEST (test_discovery_monitor_open_accepts_ignore_handler);
    RUN_TEST (test_gateway_monitor_open_requires_handler);
    RUN_TEST (test_gateway_monitor_open_accepts_ignore_handler);
    RUN_TEST (test_socket_send_ready_handler_requires_handler);
    RUN_TEST (test_socket_send_ready_handler_reentrant_replace_returns_edeadlk);
    RUN_TEST (test_socket_send_ready_handler_failed_replace_keeps_previous_handler);
    RUN_TEST (test_socket_send_ready_handler_rejects_sub_and_xsub);
    RUN_TEST (test_socket_monitor_open_dispatches_events);
    RUN_TEST (test_socket_monitor_self_close_defers_until_callback_return);
    RUN_TEST (test_socket_monitor_close_during_callback_returns_ebusy);
    RUN_TEST (test_service_monitor_open_dispatches_events);
    RUN_TEST (test_service_monitor_self_close_defers_until_callback_return);
    RUN_TEST (test_service_monitor_close_during_callback_returns_ebusy);
    RUN_TEST (test_service_monitor_callback_can_query_parent_without_deadlock);
    RUN_TEST (test_service_parent_destroy_rejects_open_monitor_children);
    return UNITY_END ();
}
