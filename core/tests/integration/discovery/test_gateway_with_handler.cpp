/* SPDX-License-Identifier: MPL-2.0 */

#include "../../testutil.hpp"
#include "../../testutil_unity.hpp"

#include <atomic>
#include <climits>
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
        memset (&reply_target_rid, 0, sizeof (reply_target_rid));
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
    zlink_routing_id_t reply_target_rid;
    char request_payload[64];
    char reply_payload[64];
};

gateway_probe_t *g_probe = NULL;

struct gateway_ready_monitor_state_t
{
    gateway_ready_monitor_state_t () :
        ready_peer_count (0),
        send_ready (false),
        error_code (0)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    size_t ready_peer_count;
    bool send_ready;
    int error_code;
};

void gateway_ready_monitor_handler (const zlink_service_event_t *event_,
                                    void *userdata_)
{
    gateway_ready_monitor_state_t *state =
      static_cast<gateway_ready_monitor_state_t *> (userdata_);
    if (!state || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        switch (event_->event_type) {
            case ZLINK_GATEWAY_ROUTE_UP:
            case ZLINK_GATEWAY_ROUTE_DOWN:
                state->ready_peer_count =
                  static_cast<size_t> (event_->value);
                break;
            case ZLINK_GATEWAY_SEND_READY_CHANGED:
                state->send_ready = event_->value > 0;
                break;
            case ZLINK_GATEWAY_MONITOR_EVENT_ERROR:
                state->error_code =
                  event_->error_code != 0 ? event_->error_code : EIO;
                break;
            default:
                break;
        }
    }

    state->cv.notify_all ();
}

bool wait_for_gateway_send_ready (gateway_ready_monitor_state_t *state_,
                                  size_t expected_,
                                  int timeout_ms_)
{
    if (!state_)
        return false;

    std::unique_lock<std::mutex> lock (state_->mutex);
    if (state_->error_code != 0)
        return false;
    if (state_->ready_peer_count >= expected_ && state_->send_ready)
        return true;

    return state_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [state_, expected_] () {
          return state_->error_code != 0
                 || (state_->ready_peer_count >= expected_
                     && state_->send_ready);
      });
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
        zlink_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (read_gateway_snapshot (gateway_, &snapshot)
            && static_cast<int> (snapshot.ready_peer_count) >= expected_) {
            return true;
        }
        msleep (step_ms);
    }
    zlink_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    return read_gateway_snapshot (gateway_, &snapshot)
           && static_cast<int> (snapshot.ready_peer_count) >= expected_;
}

bool wait_for_reply_target_rid (gateway_probe_t *probe_, int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->mutex);
    if (probe_->reply_target_rid.size > 0)
        return true;
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_]() { return probe_->reply_target_rid.size > 0; });
}

void gateway_server_handler (const zlink_routing_id_t *source_rid_,
                             zlink_msg_t *parts_,
                             size_t part_count_,
                          void *)
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
                             size_t part_count_,
                          void *)
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

void gateway_capture_only_handler (const zlink_routing_id_t *source_rid_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                          void *)
{
    gateway_probe_t *probe = g_probe;
    if (!probe) {
        close_parts (probe, parts_, part_count_);
        return;
    }

    if (source_rid_) {
        std::lock_guard<std::mutex> lock (probe->mutex);
        probe->reply_target_rid = *source_rid_;
    }
    probe->request_calls.fetch_add (1);
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
      zlink_registry_set_broadcast_interval (registry, 50));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_registry_bind (registry, pub_ep_, router_ep_));
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

    zlink_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    for (int i = 0; i < 400; ++i) {
        if (read_gateway_snapshot (client, &snapshot)
            && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0) {
            break;
        }
        msleep (10);
    }
    TEST_ASSERT_TRUE (read_gateway_snapshot (client, &snapshot));
    TEST_ASSERT_TRUE ((snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0);

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

struct gateway_destroy_probe_t
{
    gateway_destroy_probe_t () :
        client (NULL),
        server (NULL),
        client_rc (INT_MIN),
        server_rc (INT_MIN),
        client_errno (0),
        server_errno (0),
        done (false)
    {
    }

    void *client;
    void *server;
    int client_rc;
    int server_rc;
    int client_errno;
    int server_errno;
    bool done;
    std::mutex mutex;
    std::condition_variable cv;
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
                                      &gateway_server_handler, NULL);
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
                                        &gateway_client_handler, NULL);
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

void test_gateway_send_rid_same_handle_concurrent_send_is_thread_safe ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_gateway_new (ctx, "svc-handler-rid",
                                      "gw-server-rid",
                                      &gateway_capture_only_handler, NULL);
    TEST_ASSERT_NOT_NULL (server);
    void *client = zlink_gateway_new (ctx, "svc-handler-rid",
                                      "gw-client-rid",
                                      &gateway_client_handler, NULL);
    TEST_ASSERT_NOT_NULL (client);

    zlink_routing_id_t server_rid;
    memset (&server_rid, 0, sizeof (server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server, &server_rid));

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22680;
    bool bound = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (bind_seed));
        if (zlink_gateway_bind (server, endpoint) == 0) {
            bound = true;
            break;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway rid bind failed");
        bind_seed += 1;
    }
    TEST_ASSERT_TRUE (bound);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint, &server_rid));

    gateway_probe_t probe;
    g_probe = &probe;

    TEST_ASSERT_TRUE (wait_for_gateway_connections (client, 1, 5000));
    {
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (5);
        int rc = -1;
        do {
            zlink_msg_t part;
            TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&part, 4));
            memcpy (zlink_msg_data (&part), "init", 4);
            rc = zlink_gateway_send_rid (client, &server_rid, &part, 1, 0);
            if (rc == 0)
                break;
            zlink_msg_close (&part);
            msleep (1);
        } while (std::chrono::steady_clock::now () < deadline);
        TEST_ASSERT_EQUAL_INT (0, rc);
    }
    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, 1, 5000));

    const int sender_count = 4;
    const int messages_per_sender = 32;
    std::vector<int> worker_errno (sender_count, 0);
    std::vector<int> worker_progress (sender_count, 0);
    std::vector<std::thread> workers;
    std::mutex start_mutex;
    std::condition_variable start_cv;
    int ready_threads = 0;
    bool start = false;

    for (int i = 0; i < sender_count; ++i) {
        workers.push_back (std::thread ([&, i]() {
            {
                std::unique_lock<std::mutex> lock (start_mutex);
                ++ready_threads;
                start_cv.notify_all ();
                start_cv.wait (lock, [&start]() { return start; });
            }

            for (int seq = 0; seq < messages_per_sender; ++seq) {
                char payload[32];
                const int len = snprintf (payload, sizeof (payload),
                                          "rid-%d-%02d", i, seq);
                zlink_msg_t part;
                if (zlink_msg_init_size (&part, static_cast<size_t> (len))
                    != 0) {
                    worker_errno[i] = errno;
                    return;
                }
                memcpy (zlink_msg_data (&part), payload,
                        static_cast<size_t> (len));
                if (zlink_gateway_send_rid (client, &server_rid,
                                            &part, 1, 0)
                    != 0) {
                    worker_errno[i] = errno;
                    zlink_msg_close (&part);
                    return;
                }
                worker_progress[i] += 1;
            }
        }));
    }

    {
        std::unique_lock<std::mutex> lock (start_mutex);
        TEST_ASSERT_TRUE (start_cv.wait_for (
          lock, std::chrono::seconds (5),
          [&ready_threads, sender_count] () { return ready_threads == sender_count; }));
        start = true;
        start_cv.notify_all ();
    }

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    for (int i = 0; i < sender_count; ++i) {
        TEST_ASSERT_EQUAL_INT (0, worker_errno[i]);
        TEST_ASSERT_EQUAL_INT (messages_per_sender, worker_progress[i]);
    }
    const int expected_requests = 1 + sender_count * messages_per_sender;
    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, expected_requests, 30000));
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.send_failures.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
    g_probe = NULL;
}

void test_gateway_send_after_ready_monitor_close_keeps_working ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_gateway_new (ctx, "svc-handler-monitor-close",
                                      "gw-server-monitor-close",
                                      &gateway_server_handler, NULL);
    void *client = zlink_gateway_new (ctx, "svc-handler-monitor-close",
                                      "gw-client-monitor-close",
                                      &gateway_client_handler, NULL);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    probe.server = server;
    g_probe = &probe;

    void *monitor = zlink_gateway_monitor_open (
      client, ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
                | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &zlink_service_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    zlink_routing_id_t server_rid;
    memset (&server_rid, 0, sizeof (server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server, &server_rid));

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22720;
    bool bound = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (bind_seed));
        if (zlink_gateway_bind (server, endpoint) == 0) {
            bound = true;
            break;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway monitor-close bind failed");
        bind_seed += 1;
    }
    TEST_ASSERT_TRUE (bound);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint, &server_rid));
    TEST_ASSERT_TRUE (wait_for_gateway_connections (client, 1, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));

    zlink_msg_t request_part;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&request_part, 4));
    memcpy (zlink_msg_data (&request_part), "ping", 4);
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_send (client, &request_part, 1, 0));

    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, 1, 5000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.reply_calls, 1, 5000));
    TEST_ASSERT_EQUAL_STRING ("ping", probe.request_payload);
    TEST_ASSERT_EQUAL_STRING ("pong", probe.reply_payload);
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.send_failures.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
    g_probe = NULL;
}

void destroy_gateway_pair_async (gateway_destroy_probe_t *probe_)
{
    if (!probe_)
        return;

    errno = 0;
    const int client_rc = zlink_gateway_destroy (&probe_->client);
    const int client_errno = client_rc == 0 ? 0 : errno;
    errno = 0;
    const int server_rc = zlink_gateway_destroy (&probe_->server);
    const int server_errno = server_rc == 0 ? 0 : errno;

    {
        std::lock_guard<std::mutex> lock (probe_->mutex);
        probe_->client_rc = client_rc;
        probe_->server_rc = server_rc;
        probe_->client_errno = client_errno;
        probe_->server_errno = server_errno;
        probe_->done = true;
    }
    probe_->cv.notify_all ();
}

void run_gateway_monitor_close_destroy_cycle (void *ctx_,
                                              int bind_seed_,
                                              size_t payload_size_,
                                              int send_count_)
{
    TEST_ASSERT_NOT_NULL (ctx_);

    void *server = zlink_gateway_new (ctx_, "svc-handler-monitor-loop",
                                      "gw-server-monitor-loop",
                                      &gateway_server_handler, NULL);
    void *client = zlink_gateway_new (ctx_, "svc-handler-monitor-loop",
                                      "gw-client-monitor-loop",
                                      &gateway_client_handler, NULL);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    probe.server = server;
    probe.echo_request_back = true;
    probe.expected_request_size = payload_size_;
    probe.expected_reply_size = payload_size_;
    g_probe = &probe;

    void *monitor = zlink_gateway_monitor_open (
      client, ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
                | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &zlink_service_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    zlink_routing_id_t server_rid;
    memset (&server_rid, 0, sizeof (server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server, &server_rid));

    char endpoint[MAX_SOCKET_STRING];
    bool bound = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (bind_seed_ + attempt));
        if (zlink_gateway_bind (server, endpoint) == 0) {
            bound = true;
            break;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway monitor-loop bind failed");
    }
    TEST_ASSERT_TRUE (bound);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint, &server_rid));
    TEST_ASSERT_TRUE (wait_for_gateway_connections (client, 1, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));

    std::vector<unsigned char> payload (payload_size_);
    for (size_t i = 0; i < payload.size (); ++i)
        payload[i] = static_cast<unsigned char> (i & 0xff);

    for (int i = 0; i < send_count_; ++i) {
        zlink_msg_t request_part;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_msg_init_size (&request_part, payload_size_));
        if (!payload.empty ()) {
            memcpy (zlink_msg_data (&request_part), &payload[0],
                    payload.size ());
        }
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_gateway_send (client, &request_part, 1, 0));
    }

    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, send_count_, 10000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.reply_calls, send_count_, 10000));
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.send_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.request_part_count_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.reply_part_count_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.request_size_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.reply_size_failures.load ());

    gateway_destroy_probe_t destroy_probe;
    destroy_probe.client = client;
    destroy_probe.server = server;
    std::thread destroy_thread (
      &destroy_gateway_pair_async, &destroy_probe);

    {
        std::unique_lock<std::mutex> lock (destroy_probe.mutex);
        const bool completed = destroy_probe.cv.wait_for (
          lock, std::chrono::seconds (15),
          [&destroy_probe] () { return destroy_probe.done; });
        TEST_ASSERT_TRUE_MESSAGE (
          completed,
          "gateway destroy exceeded timeout during repeated monitor-close loop");
    }

    destroy_thread.join ();
    TEST_ASSERT_EQUAL_INT (0, destroy_probe.client_rc);
    TEST_ASSERT_EQUAL_INT (0, destroy_probe.server_rc);
    TEST_ASSERT_EQUAL_INT (0, destroy_probe.client_errno);
    TEST_ASSERT_EQUAL_INT (0, destroy_probe.server_errno);
    g_probe = NULL;
}

void test_gateway_destroy_after_ready_monitor_close_completes_promptly ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_gateway_new (ctx, "svc-handler-monitor-destroy",
                                      "gw-server-monitor-destroy",
                                      &gateway_server_handler, NULL);
    void *client = zlink_gateway_new (ctx, "svc-handler-monitor-destroy",
                                      "gw-client-monitor-destroy",
                                      &gateway_client_handler, NULL);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    gateway_probe_t probe;
    probe.server = server;
    probe.echo_request_back = true;
    probe.expected_request_size = 1024;
    probe.expected_reply_size = 1024;
    g_probe = &probe;

    void *monitor = zlink_gateway_monitor_open (
      client, ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
                | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &zlink_service_monitor_ignore_handler, NULL);
    TEST_ASSERT_NOT_NULL (monitor);

    zlink_routing_id_t server_rid;
    memset (&server_rid, 0, sizeof (server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server, &server_rid));

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22740;
    bool bound = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (bind_seed));
        if (zlink_gateway_bind (server, endpoint) == 0) {
            bound = true;
            break;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway destroy-after-monitor-close bind failed");
        bind_seed += 1;
    }
    TEST_ASSERT_TRUE (bound);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint, &server_rid));
    TEST_ASSERT_TRUE (wait_for_gateway_connections (client, 1, 5000));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));

    for (int i = 0; i < 32; ++i) {
        zlink_msg_t request_part;
        TEST_ASSERT_SUCCESS_ERRNO (zlink_msg_init_size (&request_part, 1024));
        memset (zlink_msg_data (&request_part), 'a' + (i % 26), 1024);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_gateway_send (client, &request_part, 1, 0));
    }

    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, 32, 5000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.reply_calls, 32, 5000));

    gateway_destroy_probe_t destroy_probe;
    destroy_probe.client = client;
    destroy_probe.server = server;
    std::thread destroy_thread (
      &destroy_gateway_pair_async, &destroy_probe);

    {
        std::unique_lock<std::mutex> lock (destroy_probe.mutex);
        const bool completed = destroy_probe.cv.wait_for (
          lock, std::chrono::seconds (15),
          [&destroy_probe] () { return destroy_probe.done; });
        TEST_ASSERT_TRUE_MESSAGE (
          completed, "gateway destroy exceeded service drain timeout after monitor close");
    }

    destroy_thread.join ();
    TEST_ASSERT_EQUAL_INT (0, destroy_probe.client_rc);
    TEST_ASSERT_EQUAL_INT (0, destroy_probe.server_rc);
    TEST_ASSERT_EQUAL_INT (0, destroy_probe.client_errno);
    TEST_ASSERT_EQUAL_INT (0, destroy_probe.server_errno);
    client = destroy_probe.client;
    server = destroy_probe.server;
    g_probe = NULL;
}

void test_gateway_repeated_monitor_close_destroy_cycles_remain_stable ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    const int iterations = 20;
    const size_t payload_size = 1024;
    const int send_count = 16;
    int bind_seed = 22800;

    for (int i = 0; i < iterations; ++i) {
        run_gateway_monitor_close_destroy_cycle (
          ctx, bind_seed, payload_size, send_count);
        bind_seed += 8;
    }
}

void test_gateway_monitor_send_ready_drives_backpressure_progress ()
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_gateway_new (ctx, "svc-handler-send-ready",
                                      "gw-server-send-ready",
                                      &gateway_server_handler, NULL);
    void *client = zlink_gateway_new (ctx, "svc-handler-send-ready",
                                      "gw-client-send-ready",
                                      &gateway_client_handler, NULL);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    const int linger = 0;
    const int low_hwm = 8;
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      server, ZLINK_GATEWAY_OPT_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      server, ZLINK_GATEWAY_OPT_SNDHWM, &low_hwm, sizeof (low_hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      server, ZLINK_GATEWAY_OPT_RCVHWM, &low_hwm, sizeof (low_hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      client, ZLINK_GATEWAY_OPT_LINGER, &linger, sizeof (linger)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      client, ZLINK_GATEWAY_OPT_SNDHWM, &low_hwm, sizeof (low_hwm)));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_set_option (
      client, ZLINK_GATEWAY_OPT_RCVHWM, &low_hwm, sizeof (low_hwm)));

    gateway_probe_t probe;
    probe.server = server;
    probe.echo_request_back = true;
    probe.expected_request_size = 1024;
    probe.expected_reply_size = 1024;
    g_probe = &probe;

    gateway_ready_monitor_state_t monitor_state;
    void *monitor = zlink_gateway_monitor_open (
      client, ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
                | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &gateway_ready_monitor_handler, &monitor_state);
    TEST_ASSERT_NOT_NULL (monitor);

    zlink_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    if (zlink_monitor_snapshot (monitor, &snapshot) == 0) {
        std::lock_guard<std::mutex> lock (monitor_state.mutex);
        monitor_state.ready_peer_count = snapshot.ready_peer_count;
        monitor_state.send_ready =
          (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0;
    }

    zlink_routing_id_t server_rid;
    memset (&server_rid, 0, sizeof (server_rid));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (server, &server_rid));

    char endpoint[MAX_SOCKET_STRING];
    int bind_seed = 22880;
    bool bound = false;
    for (int attempt = 0; attempt < 32; ++attempt) {
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (bind_seed));
        if (zlink_gateway_bind (server, endpoint) == 0) {
            bound = true;
            break;
        }
        if (errno != EADDRINUSE)
            TEST_FAIL_MESSAGE ("gateway send-ready bind failed");
        bind_seed += 1;
    }
    TEST_ASSERT_TRUE (bound);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_connect (client, endpoint, &server_rid));
    TEST_ASSERT_TRUE (wait_for_gateway_send_ready (&monitor_state, 1, 5000));

    const int message_count = 256;
    int blocked_count = 0;
    std::vector<unsigned char> payload (1024, 's');
    for (int i = 0; i < message_count; ++i) {
        while (true) {
            zlink_msg_t request_part;
            TEST_ASSERT_SUCCESS_ERRNO (
              zlink_msg_init_size (&request_part, payload.size ()));
            memcpy (zlink_msg_data (&request_part), &payload[0], payload.size ());
            const int rc =
              zlink_gateway_send_rid (client, &server_rid, &request_part, 1,
                                      ZLINK_DONTWAIT);
            if (rc == 0)
                break;

            const int send_errno = errno;
            zlink_msg_close (&request_part);
            TEST_ASSERT_TRUE (send_errno == EAGAIN || send_errno == ENOTCONN
                              || send_errno == EHOSTUNREACH);
            ++blocked_count;
            TEST_ASSERT_TRUE (wait_for_gateway_send_ready (&monitor_state, 1, 5000));
        }
    }

    TEST_ASSERT_TRUE (blocked_count > 0);
    TEST_ASSERT_TRUE (wait_for_calls (&probe.request_calls, message_count, 10000));
    TEST_ASSERT_TRUE (wait_for_calls (&probe.reply_calls, message_count, 10000));
    TEST_ASSERT_EQUAL_INT (0, probe.close_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.send_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.request_part_count_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.reply_part_count_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.request_size_failures.load ());
    TEST_ASSERT_EQUAL_INT (0, probe.reply_size_failures.load ());

    TEST_ASSERT_SUCCESS_ERRNO (zlink_service_monitor_close (&monitor));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&client));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&server));
    g_probe = NULL;
}

int main (int, char **)
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_gateway_handler_dispatches_request_and_reply);
    RUN_TEST (test_gateway_handler_reply_stress_multi_client_manual_connect);
    RUN_TEST (test_gateway_send_rid_same_handle_concurrent_send_is_thread_safe);
    RUN_TEST (test_gateway_send_after_ready_monitor_close_keeps_working);
    RUN_TEST (test_gateway_destroy_after_ready_monitor_close_completes_promptly);
    RUN_TEST (test_gateway_repeated_monitor_close_destroy_cycles_remain_stable);
    RUN_TEST (test_gateway_monitor_send_ready_drives_backpressure_progress);
    return UNITY_END ();
}
