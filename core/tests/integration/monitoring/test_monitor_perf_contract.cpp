/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include "../../../src/core/monitor_dispatch_internal.hpp"
#include "../../../src/core/recv_internal.hpp"
#include "../../../src/sockets/socket_base.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string.h>
#include <thread>
#include <vector>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
std::atomic<unsigned int> g_inproc_endpoint_counter (0);
std::atomic<int> g_send_ready_self_close_rc (0);
std::atomic<int> g_send_ready_self_close_errno (0);
std::atomic<int> g_send_ready_post_close_send_rc (0);
std::atomic<int> g_send_ready_post_close_send_errno (0);

struct connect_monitor_state_t
{
    connect_monitor_state_t () :
        connection_ready_count (0),
        accepted_count (0),
        connected_count (0),
        error_code (0)
    {
    }

    std::mutex sync;
    std::condition_variable cv;
    size_t connection_ready_count;
    size_t accepted_count;
    size_t connected_count;
    int error_code;
};

struct connect_monitor_t
{
    connect_monitor_t () : monitor (NULL), state (NULL) {}

    void *monitor;
    connect_monitor_state_t *state;
};

struct queue_probe_t
{
    queue_probe_t (void *send_socket_,
                   void *recv_socket_,
                   bool enable_send_sampling_,
                   bool enable_recv_sampling_) :
        send_socket (send_socket_),
        recv_socket (recv_socket_),
        enable_send_sampling (enable_send_sampling_),
        enable_recv_sampling (enable_recv_sampling_),
        send_samples (0),
        recv_samples (0),
        sample_failed (false)
    {
    }

    void force_sample_send ()
    {
        if (!enable_send_sampling)
            return;
        sample (send_socket, true);
    }

    void force_sample_recv ()
    {
        if (!enable_recv_sampling)
            return;
        sample (recv_socket, false);
    }

    void sample_send_if_due ()
    {
        if (!enable_send_sampling) {
            ++send_samples;
            return;
        }
        if ((send_samples + 1) % 8 == 0)
            sample (send_socket, true);
        ++send_samples;
    }

    void sample_recv_if_due ()
    {
        if (!enable_recv_sampling) {
            ++recv_samples;
            return;
        }
        if ((recv_samples + 1) % 8 == 0)
            sample (recv_socket, false);
        ++recv_samples;
    }

    void assert_ok () const
    {
        TEST_ASSERT_FALSE (sample_failed);
    }

    void *send_socket;
    void *recv_socket;
    bool enable_send_sampling;
    bool enable_recv_sampling;
    size_t send_samples;
    size_t recv_samples;
    bool sample_failed;

  private:
    static bool read_socket_snapshot_once (void *socket_,
                                           zlink_monitor_snapshot_t *out_)
    {
        if (!socket_ || !out_)
            return false;

        void *monitor = zlink_socket_monitor_open (
          socket_, ZLINK_EVENT_ALL, &zlink_monitor_ignore_handler, NULL);
        if (!monitor)
            return false;

        const int zero = 0;
        if (zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)) != 0) {
            (void) zlink_close (monitor);
            return false;
        }

        memset (out_, 0, sizeof (*out_));
        const int rc = zlink_monitor_snapshot (monitor, out_);
        const int close_rc = zlink_close (monitor);
        return rc == 0 && close_rc == 0;
    }

    void sample (void *socket_, bool send_path_)
    {
        zlink_monitor_snapshot_t snapshot;
        if (!read_socket_snapshot_once (socket_, &snapshot)) {
            sample_failed = true;
            return;
        }

        if ((snapshot.detail_flags & ZLINK_MONITOR_SNAPSHOT_DETAIL_READY_PEER_COUNT)
            == 0
            || snapshot.ready_peer_count < 1) {
            sample_failed = true;
            return;
        }

        const zlink_monitor_snapshot_detail_mask_t expected_detail =
          send_path_ ? ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS
                     : ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS;
        if ((snapshot.detail_flags & expected_detail) == 0) {
            sample_failed = true;
            return;
        }
    }
};

std::mutex g_connect_monitor_registry_sync;
std::map<void *, connect_monitor_state_t *> g_connect_monitor_registry;

void close_socket_zero_linger (void *socket_)
{
    if (!socket_)
        return;
    const int zero = 0;
    (void) zlink_setsockopt (socket_, ZLINK_LINGER, &zero, sizeof (zero));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket_));
}

void configure_bounded_pair_socket (void *socket_, int timeout_ms_)
{
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_LINGER, &zero, sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_SNDTIMEO, &timeout_ms_,
                        sizeof (timeout_ms_)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_setsockopt (socket_, ZLINK_RCVTIMEO, &timeout_ms_,
                        sizeof (timeout_ms_)));
}

void register_connect_monitor (void *monitor_, connect_monitor_state_t *state_)
{
    std::lock_guard<std::mutex> lock (g_connect_monitor_registry_sync);
    g_connect_monitor_registry[monitor_] = state_;
}

void unregister_connect_monitor (void *monitor_)
{
    std::lock_guard<std::mutex> lock (g_connect_monitor_registry_sync);
    g_connect_monitor_registry.erase (monitor_);
}

connect_monitor_state_t *find_connect_monitor_state_for_dispatch ()
{
    void *monitor = zlink::current_monitor_dispatch_handle ();
    if (!monitor)
        return NULL;

    std::lock_guard<std::mutex> lock (g_connect_monitor_registry_sync);
    std::map<void *, connect_monitor_state_t *>::iterator it =
      g_connect_monitor_registry.find (monitor);
    return it != g_connect_monitor_registry.end () ? it->second : NULL;
}

void perf_like_connect_monitor_handler (const zlink_monitor_event_t *event_, void *)
{
    connect_monitor_state_t *state = find_connect_monitor_state_for_dispatch ();
    if (!state || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (state->sync);
        switch (event_->event) {
            case ZLINK_EVENT_CONNECTION_READY:
                ++state->connection_ready_count;
                break;

            case ZLINK_EVENT_ACCEPTED:
                ++state->accepted_count;
                break;

            case ZLINK_EVENT_CONNECTED:
                ++state->connected_count;
                break;

            case ZLINK_EVENT_BIND_FAILED:
            case ZLINK_EVENT_ACCEPT_FAILED:
            case ZLINK_EVENT_CLOSE_FAILED:
            case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
                if (state->error_code == 0) {
                    state->error_code =
                      event_->value > 0 ? static_cast<int> (event_->value) : EIO;
                }
                break;

            default:
                break;
        }
    }

    state->cv.notify_all ();
}

size_t connect_ready_count (const connect_monitor_state_t *state_)
{
    if (!state_)
        return 0;
    return std::max (state_->connection_ready_count, state_->accepted_count);
}

bool open_perf_like_connect_monitor (void *socket_, connect_monitor_t *out_)
{
    if (!socket_ || !out_)
        return false;

    connect_monitor_state_t *state = new (std::nothrow) connect_monitor_state_t;
    if (!state)
        return false;

    void *monitor = zlink_socket_monitor_open (
      socket_,
      ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_CONNECTED
        | ZLINK_EVENT_ACCEPTED | ZLINK_EVENT_BIND_FAILED
        | ZLINK_EVENT_ACCEPT_FAILED | ZLINK_EVENT_CLOSE_FAILED
        | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
        | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
        | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH,
      &perf_like_connect_monitor_handler, NULL);
    if (!monitor) {
        delete state;
        return false;
    }

    const int zero = 0;
    if (zlink_setsockopt (monitor, ZLINK_LINGER, &zero, sizeof (zero)) != 0) {
        (void) zlink_close (monitor);
        delete state;
        return false;
    }

    register_connect_monitor (monitor, state);
    out_->monitor = monitor;
    out_->state = state;
    return true;
}

bool wait_perf_like_connect_ready (connect_monitor_t *monitor_, int timeout_ms_)
{
    if (!monitor_ || !monitor_->state)
        return false;

    std::unique_lock<std::mutex> lock (monitor_->state->sync);
    return monitor_->state->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [monitor_]() {
          return monitor_->state->error_code != 0
                 || connect_ready_count (monitor_->state) >= 1;
      })
           && monitor_->state->error_code == 0
           && connect_ready_count (monitor_->state) >= 1;
}

void close_perf_like_connect_monitor (connect_monitor_t *monitor_)
{
    if (!monitor_ || !monitor_->monitor)
        return;

    void *monitor = monitor_->monitor;
    connect_monitor_state_t *state = monitor_->state;
    monitor_->monitor = NULL;
    monitor_->state = NULL;

    unregister_connect_monitor (monitor);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (monitor));
    delete state;
}

bool recv_exact (void *socket_, const std::vector<char> &expected_)
{
    std::vector<char> actual (expected_.size (), 0);
    const int rc = zlink::recv_buffer_internal (
      socket_, &actual[0], actual.size (), 0);
    return rc == static_cast<int> (expected_.size ())
           && memcmp (&actual[0], &expected_[0], expected_.size ()) == 0;
}

void make_unique_inproc_endpoint (char *endpoint_, size_t size_)
{
    TEST_ASSERT_NOT_NULL (endpoint_);
    const unsigned int endpoint_id =
      g_inproc_endpoint_counter.fetch_add (1, std::memory_order_acq_rel) + 1;
    snprintf (endpoint_, size_, "inproc://monitor-perf-pair-%u", endpoint_id);
}

void send_ready_self_close_handler (void *subject_, void *)
{
    void *socket = subject_;
    g_send_ready_self_close_rc.store (zlink_close (socket),
                                      std::memory_order_release);
    g_send_ready_self_close_errno.store (errno, std::memory_order_release);

    static const char payload[] = "post-close-send";
    zlink::msg_t msg;
    TEST_ASSERT_EQUAL_INT (0, msg.init_buffer (payload, sizeof (payload)));
    zlink::socket_base_t *raw_socket =
      static_cast<zlink::socket_base_t *> (socket);
    const int send_rc = raw_socket->send (&msg, 0);
    g_send_ready_post_close_send_rc.store (send_rc, std::memory_order_release);
    g_send_ready_post_close_send_errno.store (errno,
                                              std::memory_order_release);
}
}

void run_pair_perf_like_monitor_sampling_case (bool sample_send_,
                                               bool sample_recv_)
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_socket (ctx, ZLINK_PAIR);
    void *client = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    configure_bounded_pair_socket (server, 200);
    configure_bounded_pair_socket (client, 200);

    connect_monitor_t server_monitor;
    connect_monitor_t client_monitor;
    TEST_ASSERT_TRUE (open_perf_like_connect_monitor (server, &server_monitor));
    TEST_ASSERT_TRUE (open_perf_like_connect_monitor (client, &client_monitor));

    char endpoint[MAX_SOCKET_STRING];
    bind_loopback_ipv4 (server, endpoint, sizeof endpoint);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    TEST_ASSERT_TRUE (wait_perf_like_connect_ready (&server_monitor, 3000));
    TEST_ASSERT_TRUE (wait_perf_like_connect_ready (&client_monitor, 3000));

    close_perf_like_connect_monitor (&client_monitor);
    close_perf_like_connect_monitor (&server_monitor);

    std::vector<char> payload (1024, 'p');
    queue_probe_t probe (client, server, sample_send_, sample_recv_);
    probe.force_sample_send ();
    probe.force_sample_recv ();
    probe.assert_ok ();

    const int message_count = 16;
    std::atomic<bool> recv_failed (false);

    std::thread receiver ([&]() {
        for (int i = 0; i < message_count; ++i) {
            if (!recv_exact (server, payload)) {
                recv_failed.store (true, std::memory_order_release);
                return;
            }
            probe.sample_recv_if_due ();
            if (probe.sample_failed) {
                recv_failed.store (true, std::memory_order_release);
                return;
            }
        }
    });

    bool send_failed = false;
    for (int i = 0; i < message_count; ++i) {
        const int rc = zlink_send (client, &payload[0], payload.size (), 0);
        if (rc != static_cast<int> (payload.size ())) {
            send_failed = true;
            break;
        }
        probe.sample_send_if_due ();
        if (probe.sample_failed) {
            send_failed = true;
            break;
        }
    }

    receiver.join ();
    probe.force_sample_send ();
    probe.force_sample_recv ();
    probe.assert_ok ();
    TEST_ASSERT_FALSE (send_failed);
    TEST_ASSERT_FALSE (recv_failed.load (std::memory_order_acquire));

    close_socket_zero_linger (client);
    close_socket_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_pair_perf_like_send_sampling_preserves_oneway_delivery ()
{
    run_pair_perf_like_monitor_sampling_case (true, false);
}

void test_pair_perf_like_recv_sampling_preserves_oneway_delivery ()
{
    run_pair_perf_like_monitor_sampling_case (false, true);
}

void test_pair_perf_like_bidirectional_sampling_preserves_oneway_delivery ()
{
    run_pair_perf_like_monitor_sampling_case (true, true);
}

void test_pair_inproc_perf_like_monitor_ready_implies_bidirectional_delivery ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_socket (ctx, ZLINK_PAIR);
    void *client = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    configure_bounded_pair_socket (server, 200);
    configure_bounded_pair_socket (client, 200);

    char endpoint[128];
    make_unique_inproc_endpoint (endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, endpoint));

    connect_monitor_t server_monitor;
    connect_monitor_t client_monitor;
    TEST_ASSERT_TRUE (open_perf_like_connect_monitor (server, &server_monitor));
    TEST_ASSERT_TRUE (open_perf_like_connect_monitor (client, &client_monitor));

    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    TEST_ASSERT_TRUE (wait_perf_like_connect_ready (&server_monitor, 3000));
    TEST_ASSERT_TRUE (wait_perf_like_connect_ready (&client_monitor, 3000));

    close_perf_like_connect_monitor (&client_monitor);
    close_perf_like_connect_monitor (&server_monitor);

    static const char hello[] = "pair-inproc-hello";
    static const char ack[] = "pair-inproc-ack";

    send_string_expect_success (client, hello, 0);
    recv_string_expect_success (server, hello, 0);

    send_string_expect_success (server, ack, 0);
    recv_string_expect_success (client, ack, 0);

    close_socket_zero_linger (client);
    close_socket_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

void test_send_ready_self_close_blocks_followup_operational_api ()
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    void *server = zlink_socket (ctx, ZLINK_PAIR);
    void *client = zlink_socket (ctx, ZLINK_PAIR);
    TEST_ASSERT_NOT_NULL (server);
    TEST_ASSERT_NOT_NULL (client);

    configure_bounded_pair_socket (server, 200);
    configure_bounded_pair_socket (client, 200);

    char endpoint[128];
    make_unique_inproc_endpoint (endpoint, sizeof (endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_bind (server, endpoint));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (client, endpoint));

    g_send_ready_self_close_rc.store (INT_MIN, std::memory_order_release);
    g_send_ready_self_close_errno.store (0, std::memory_order_release);
    g_send_ready_post_close_send_rc.store (INT_MIN, std::memory_order_release);
    g_send_ready_post_close_send_errno.store (0, std::memory_order_release);

    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_socket_set_send_ready_handler (client, &send_ready_self_close_handler, NULL));

    zlink::socket_base_t *raw_client =
      static_cast<zlink::socket_base_t *> (client);
    raw_client->invoke_send_ready_handler_for_testing ();

    TEST_ASSERT_EQUAL_INT (0,
                           g_send_ready_self_close_rc.load (
                             std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (-1,
                           g_send_ready_post_close_send_rc.load (
                             std::memory_order_acquire));
    TEST_ASSERT_EQUAL_INT (ESHUTDOWN,
                           g_send_ready_post_close_send_errno.load (
                             std::memory_order_acquire));

    close_socket_zero_linger (server);
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
}

int main ()
{
    setup_test_environment ();

    UNITY_BEGIN ();
    RUN_TEST (test_pair_perf_like_send_sampling_preserves_oneway_delivery);
    RUN_TEST (test_pair_perf_like_recv_sampling_preserves_oneway_delivery);
    RUN_TEST (test_pair_perf_like_bidirectional_sampling_preserves_oneway_delivery);
    RUN_TEST (
      test_pair_inproc_perf_like_monitor_ready_implies_bidirectional_delivery);
    RUN_TEST (test_send_ready_self_close_blocks_followup_operational_api);
    return UNITY_END ();
}
