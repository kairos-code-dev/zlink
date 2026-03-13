/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <vector>

namespace
{
struct gateway_probe_t
{
    gateway_probe_t () :
        request_calls (0),
        reply_calls (0),
        other_calls (0),
        close_failures (0),
        send_failures (0),
        request_part_count_failures (0),
        reply_part_count_failures (0),
        request_size_failures (0),
        reply_size_failures (0),
        server (NULL),
        echo_request_back (false),
        expected_request_size (4),
        expected_reply_size (4)
    {
        memset (&request_rid, 0, sizeof (request_rid));
        memset (request_payload, 0, sizeof (request_payload));
        memset (reply_payload, 0, sizeof (reply_payload));
    }

    std::atomic<int> request_calls;
    std::atomic<int> reply_calls;
    std::atomic<int> other_calls;
    std::atomic<int> close_failures;
    std::atomic<int> send_failures;
    std::atomic<int> request_part_count_failures;
    std::atomic<int> reply_part_count_failures;
    std::atomic<int> request_size_failures;
    std::atomic<int> reply_size_failures;
    void *server;
    bool echo_request_back;
    size_t expected_request_size;
    size_t expected_reply_size;
    std::mutex mutex;
    std::condition_variable cv;
    zlink_routing_id_t request_rid;
    char request_payload[64];
    char reply_payload[64];
};

gateway_probe_t *g_probe = NULL;

void close_parts (gateway_probe_t *probe_, zlink_msg_t *parts_, size_t count_)
{
    for (size_t i = 0; i < count_; ++i) {
        if (zlink_msg_close (&parts_[i]) != 0 && probe_)
            probe_->close_failures.fetch_add (1);
    }
    if (probe_)
        probe_->cv.notify_all ();
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

bool wait_for_calls (std::atomic<int> *calls_, int expected_, int timeout_ms_)
{
    if (!g_probe) {
        const int step_ms = 10;
        const int attempts = timeout_ms_ / step_ms;
        for (int i = 0; i < attempts; ++i) {
            if (calls_->load () >= expected_)
                return true;
            msleep (step_ms);
        }
        return calls_->load () >= expected_;
    }

    std::unique_lock<std::mutex> lock (g_probe->mutex);
    if (calls_->load () >= expected_)
        return true;
    return g_probe->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [calls_, expected_]() { return calls_->load () >= expected_; });
}

bool wait_for_gateway_connections (void *gateway_,
                                   int expected_,
                                   int timeout_ms_)
{
    const int step_ms = 10;
    const int attempts = timeout_ms_ / step_ms;
    for (int i = 0; i < attempts; ++i) {
        zlink_gateway_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (zlink_gateway_monitor_snapshot (gateway_, &snapshot) == 0
            && static_cast<int> (snapshot.ready_peer_count) >= expected_) {
            return true;
        }
        msleep (step_ms);
    }
    zlink_gateway_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    return zlink_gateway_monitor_snapshot (gateway_, &snapshot) == 0
           && static_cast<int> (snapshot.ready_peer_count) >= expected_;
}

void gateway_server_handler (const zlink_routing_id_t *source_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    gateway_probe_t *probe = g_probe;
    if (!probe) {
        close_parts (probe, parts_, part_count_);
        return;
    }

    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[gw-handler] server request parts=%zu\n", part_count_);
        fflush (stderr);
    }
    if (part_count_ != 1)
        probe->request_part_count_failures.fetch_add (1);
    if (source_rid_ || part_count_ > 0) {
        std::lock_guard<std::mutex> lock (probe->mutex);
        if (source_rid_)
            probe->request_rid = *source_rid_;
        if (part_count_ > 0) {
            const size_t size = zlink_msg_size (&parts_[0]);
            const size_t copy_size =
              size < sizeof (probe->request_payload) - 1
                ? size
                : sizeof (probe->request_payload) - 1;
            memcpy (probe->request_payload, zlink_msg_data (&parts_[0]),
                    copy_size);
            probe->request_payload[copy_size] = '\0';
        }
    }
    if (part_count_ > 0 && probe->expected_request_size > 0
        && zlink_msg_size (&parts_[0]) != probe->expected_request_size) {
        probe->request_size_failures.fetch_add (1);
    }
    probe->request_calls.fetch_add (1);
    probe->cv.notify_all ();
    zlink_routing_id_t reply_rid;
    memset (&reply_rid, 0, sizeof (reply_rid));
    if (source_rid_)
        reply_rid = *source_rid_;
    std::vector<unsigned char> reply_payload;
    if (probe->echo_request_back && part_count_ > 0) {
        const size_t size = zlink_msg_size (&parts_[0]);
        reply_payload.resize (size);
        if (size > 0) {
            memcpy (&reply_payload[0], zlink_msg_data (&parts_[0]), size);
        }
    } else {
        static const char k_reply[] = "pong";
        reply_payload.resize (sizeof (k_reply) - 1);
        memcpy (&reply_payload[0], k_reply, sizeof (k_reply) - 1);
    }
    zlink_msg_t reply_part;
    if (zlink_msg_init_size (&reply_part, reply_payload.size ()) != 0) {
        probe->send_failures.fetch_add (1);
        close_parts (probe, parts_, part_count_);
        return;
    }
    if (!reply_payload.empty ()) {
        memcpy (zlink_msg_data (&reply_part), &reply_payload[0],
                reply_payload.size ());
    }
    if (zlink_gateway_send_rid (probe->server, &reply_rid,
                                &reply_part, 1, 0)
        != 0) {
        (void) zlink_msg_close (&reply_part);
        probe->send_failures.fetch_add (1);
        probe->cv.notify_all ();
        if (getenv ("ZLINK_TEST_DEBUG")) {
            fprintf (stderr, "[gw-handler] reply send failed errno=%d\n", errno);
            fflush (stderr);
        }
    }
    close_parts (probe, parts_, part_count_);
}

void gateway_client_handler (const zlink_routing_id_t *source_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_)
{
    LIBZLINK_UNUSED (source_rid_);

    gateway_probe_t *probe = g_probe;
    if (!probe) {
        close_parts (probe, parts_, part_count_);
        return;
    }

    if (getenv ("ZLINK_TEST_DEBUG")) {
        fprintf (stderr, "[gw-handler] client reply parts=%zu\n", part_count_);
        fflush (stderr);
    }
    if (part_count_ != 1)
        probe->reply_part_count_failures.fetch_add (1);
    if (part_count_ > 0) {
        const size_t size = zlink_msg_size (&parts_[0]);
        if (probe->expected_reply_size > 0
            && size != probe->expected_reply_size)
            probe->reply_size_failures.fetch_add (1);
        std::lock_guard<std::mutex> lock (probe->mutex);
        const size_t copy_size =
          size < sizeof (probe->reply_payload) - 1
            ? size
            : sizeof (probe->reply_payload) - 1;
        memcpy (probe->reply_payload, zlink_msg_data (&parts_[0]), copy_size);
        probe->reply_payload[copy_size] = '\0';
    }
    probe->reply_calls.fetch_add (1);
    probe->cv.notify_all ();

    close_parts (probe, parts_, part_count_);
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

void test_gateway_handler_dispatches_request_and_reply ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    char registry_pub[MAX_SOCKET_STRING];
    char registry_router[MAX_SOCKET_STRING];
    int registry_seed = 22610;
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

    void *client_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    void *server_discovery =
      zlink_discovery_new (ctx, ZLINK_SERVICE_TYPE_GATEWAY);
    TEST_ASSERT_NOT_NULL (client_discovery);
    TEST_ASSERT_NOT_NULL (server_discovery);

    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      client_discovery, registry_router));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_connect_registry (
      server_discovery, registry_router));
    void *client = create_gateway_attached (ctx, client_discovery, "svc-handler",
                                      "gw-client-h",
                                      &gateway_client_handler);
    void *server = create_gateway_attached (ctx, server_discovery, "svc-handler",
                                      "gw-server-h",
                                      &gateway_server_handler);
    TEST_ASSERT_NOT_NULL (client);
    TEST_ASSERT_NOT_NULL (server);

    gateway_probe_t probe;
    probe.server = server;
    g_probe = &probe;

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22620;
    for (int ba = 0; ba < 32; ++ba) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (bind_seed));
        bool bound = false;
        for (int bi = 0; bi < 300; ++bi) {
            if (zlink_gateway_bind (server, endpoint) == 0) {
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

    zlink_gateway_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    for (int i = 0; i < 400; ++i) {
        if (zlink_gateway_monitor_snapshot (client, &snapshot) == 0
            && snapshot.send_ready != 0) {
            break;
        }
        msleep (10);
    }
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_monitor_snapshot (client, &snapshot));
    TEST_ASSERT_TRUE (snapshot.send_ready != 0);

    zlink_msg_t request_part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&request_part, 4));
    memcpy (zlink_msg_data (&request_part), "ping", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (client, &request_part, 1, 0));

    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, 1, 3000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.reply_calls, 1, 3000));
    TEST_ASSERT_EQUAL_STRING ("ping", probe.request_payload);
    TEST_ASSERT_EQUAL_STRING ("pong", probe.reply_payload);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.send_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.other_calls.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&server_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_discovery_destroy (&client_discovery));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_registry_destroy (&registry));
    g_probe = NULL;
}

struct gateway_stress_worker_t
{
    gateway_stress_worker_t () :
        gateway (NULL),
        requests (0),
        payload_size (0),
        failures (0)
    {
    }

    void *gateway;
    size_t requests;
    size_t payload_size;
    std::atomic<int> failures;
};

void gateway_stress_send_worker (gateway_stress_worker_t *worker_)
{
    if (!worker_ || !worker_->gateway || worker_->payload_size == 0)
        return;

    std::vector<unsigned char> payload (worker_->payload_size);
    for (size_t i = 0; i < payload.size (); ++i)
        payload[i] = static_cast<unsigned char> (i & 0xff);

    for (size_t i = 0; i < worker_->requests; ++i) {
        zlink_msg_t part;
        if (zlink_msg_init_size (&part, worker_->payload_size) != 0) {
            worker_->failures.fetch_add (1);
            return;
        }
        memcpy (zlink_msg_data (&part), &payload[0], payload.size ());
        if (zlink_gateway_send (worker_->gateway, &part, 1, 0) != 0) {
            const int err = errno;
            (void) zlink_msg_close (&part);
            worker_->failures.fetch_add (1);
            errno = err;
            return;
        }
    }
}

void test_gateway_handler_reply_stress_multi_client_manual_connect ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    const size_t client_count = 16;
    const size_t requests_per_client = 32;
    const size_t payload_size = 1024;
    const int timeout_ms = 10000;

    void *server = zlink_gateway_new (ctx, "svc-handler-stress",
                                      "gw-server-stress",
                                      &gateway_server_handler);
    TEST_ASSERT_NOT_NULL (server);

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22640;
    bool bound = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (bind_seed));
        if (zlink_gateway_bind (server, endpoint) == 0) {
            bound = true;
            break;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway stress bind failed");
        bind_seed += 1;
    }
    TEST_ASSERT_TRUE (bound);

    zlink_routing_id_t server_rid;
    memset (&server_rid, 0, sizeof (server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server, &server_rid));

    gateway_probe_t probe;
    probe.server = server;
    probe.echo_request_back = true;
    probe.expected_request_size = payload_size;
    probe.expected_reply_size = payload_size;
    g_probe = &probe;

    std::vector<void *> clients (client_count, NULL);
    for (size_t i = 0; i < client_count; ++i) {
        char routing_id[32];
        snprintf (routing_id, sizeof (routing_id), "gw-client-%zu", i);
        clients[i] = zlink_gateway_new (ctx, "svc-handler-stress",
                                        routing_id,
                                        &gateway_client_handler);
        TEST_ASSERT_NOT_NULL (clients[i]);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_gateway_connect (clients[i], endpoint, &server_rid));
    }

    for (size_t i = 0; i < client_count; ++i) {
        TEST_ASSERT_TRUE (wait_for_gateway_connections (
          clients[i], 1, timeout_ms));
    }

    std::vector<gateway_stress_worker_t> workers (client_count);
    std::vector<std::thread> threads;
    threads.reserve (client_count);
    for (size_t i = 0; i < client_count; ++i) {
        workers[i].gateway = clients[i];
        workers[i].requests = requests_per_client;
        workers[i].payload_size = payload_size;
        threads.push_back (
          std::thread (&gateway_stress_send_worker, &workers[i]));
    }

    for (size_t i = 0; i < threads.size (); ++i)
        threads[i].join ();

    const int expected = static_cast<int> (client_count * requests_per_client);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, expected,
                                      timeout_ms));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.reply_calls, expected,
                                      timeout_ms));

    for (size_t i = 0; i < workers.size (); ++i)
        TEST_ASSERT_EQUAL_INT (0, workers[i].failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.send_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.request_part_count_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.reply_part_count_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.request_size_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.reply_size_failures.load ());

    for (size_t i = 0; i < clients.size (); ++i)
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&clients[i]));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
    g_probe = NULL;
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_handler_dispatches_request_and_reply);
    RUN_TEST (test_gateway_handler_reply_stress_multi_client_manual_connect);
    return UNITY_END ();
}
