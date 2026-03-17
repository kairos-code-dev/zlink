/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "testutil_unity.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

SETUP_TEARDOWN_TESTCONTEXT

namespace
{
std::atomic<unsigned int> g_perf_endpoint_counter (0);

struct count_probe_t
{
    count_probe_t () : calls (0) {}

    std::atomic<int> calls;
    std::mutex sync;
    std::condition_variable cv;
};

count_probe_t *g_raw_probe = NULL;
count_probe_t *g_gateway_probe = NULL;
count_probe_t *g_spot_probe = NULL;

const int kPerfHandleCounts[] = {1, 4, 16, 64};
const int kRawMessagesPerHandle = 128;
const int kGatewayMessagesPerHandle = 64;
const int kSpotMessagesPerHandle = 64;

bool should_run_named_test (const char *name_)
{
    const char *selected = getenv ("ZLINK_TEST_CASE");
    return selected && *selected && strcmp (selected, name_) == 0;
}

double perf_ratio_threshold ()
{
    const char *value = getenv ("ZLINK_PERF_MIN_RATIO");
    if (!value || !*value)
        return 0.80;

    const double parsed = atof (value);
    return parsed > 0.0 ? parsed : 0.80;
}

void notify_probe (count_probe_t *probe_)
{
    if (!probe_)
        return;

    {
        std::lock_guard<std::mutex> lock (probe_->sync);
        probe_->calls.fetch_add (1);
    }
    probe_->cv.notify_all ();
}

bool wait_for_probe_count (count_probe_t *probe_,
                           int expected_,
                           int timeout_ms_)
{
    if (!probe_)
        return false;

    std::unique_lock<std::mutex> lock (probe_->sync);
    return probe_->cv.wait_for (
      lock, std::chrono::milliseconds (timeout_ms_),
      [probe_, expected_]() {
          return probe_->calls.load (std::memory_order_acquire) >= expected_;
      });
}

void discard_socket_parts (const zlink_routing_id_t *,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                          void *)
{
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

void scaling_raw_handler (const zlink_routing_id_t *,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    discard_socket_parts (NULL, parts_, part_count_, NULL);
    notify_probe (g_raw_probe);
}

void scaling_gateway_handler (const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                          void *)
{
    discard_socket_parts (NULL, parts_, part_count_, NULL);
    notify_probe (g_gateway_probe);
}

void discard_gateway_message (const zlink_routing_id_t *,
                              zlink_msg_t *parts_,
                              size_t part_count_,
                          void *)
{
    discard_socket_parts (NULL, parts_, part_count_, NULL);
}

void ignore_spot_handler (const zlink_routing_id_t *,
                          const char *,
                          size_t,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    discard_socket_parts (NULL, parts_, part_count_, NULL);
}

void scaling_spot_handler (const zlink_routing_id_t *,
                           const char *,
                           size_t,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                          void *)
{
    discard_socket_parts (NULL, parts_, part_count_, NULL);
    notify_probe (g_spot_probe);
}

zlink_socket_handler_t make_msg_handler (zlink_socket_msg_handler_fn fn_)
{
    zlink_socket_handler_t handler;
    memset (&handler, 0, sizeof (handler));
    handler.kind = ZLINK_SOCKET_HANDLER_MSG;
    handler.fn.msg = fn_;
    return handler;
}

void make_unique_inproc_endpoint (char *endpoint_out_, size_t size_)
{
    const unsigned int id =
      g_perf_endpoint_counter.fetch_add (1, std::memory_order_acq_rel) + 1;
    snprintf (endpoint_out_, size_, "inproc://threadsafe-perf-%u", id);
}

void close_socket_zero_linger (void *socket_)
{
    if (!socket_)
        return;
    const int zero = 0;
    (void) zlink_setsockopt (socket_, ZLINK_LINGER, &zero, sizeof (zero));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_close (socket_));
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

void wait_gateway_ready (void *gateway_, int timeout_ms_)
{
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (read_gateway_snapshot (gateway_, &snapshot)
            && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0) {
            return;
        }
        msleep (10);
    }
    TEST_FAIL_MESSAGE ("gateway perf ready timeout");
}

int gateway_send_text (void *gateway_, const char *payload_)
{
    const size_t size = strlen (payload_);
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    memcpy (zlink_msg_data (&part), payload_, size);
    const int rc = zlink_gateway_send (gateway_, &part, 1, 0);
    if (rc != 0) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
    }
    return rc;
}

int spot_publish_text (void *spot_, const char *topic_, const char *payload_)
{
    const size_t size = strlen (payload_);
    zlink_msg_t part;
    if (zlink_msg_init_size (&part, size) != 0)
        return -1;
    memcpy (zlink_msg_data (&part), payload_, size);
    const int rc = zlink_spot_publish (spot_, topic_, &part, 1, 0);
    if (rc != 0) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
    }
    return rc;
}

void configure_gateway_linger_zero (void *gateway_)
{
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_gateway_set_option (gateway_, ZLINK_GATEWAY_OPT_LINGER, &zero,
                                sizeof (zero)));
}

void configure_spot_linger_zero (void *node_, void *spot_)
{
    const int zero = 0;
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_pub_option (node_, ZLINK_SPOT_PUB_OPT_LINGER, &zero,
                                      sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_node_set_sub_option (node_, ZLINK_SPOT_SUB_OPT_LINGER, &zero,
                                      sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_pub_option (spot_, ZLINK_SPOT_PUB_OPT_LINGER, &zero,
                                 sizeof (zero)));
    TEST_ASSERT_SUCCESS_ERRNO (
      zlink_spot_set_sub_option (spot_, ZLINK_SPOT_SUB_OPT_LINGER, &zero,
                                 sizeof (zero)));
}

double measure_raw_handle_scaling_once (int handle_count_, int messages_per_handle_)
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    count_probe_t probe;
    g_raw_probe = &probe;
    const zlink_socket_handler_t msg_handler =
      make_msg_handler (&scaling_raw_handler);
    const zlink_socket_handler_t discard_handler =
      make_msg_handler (&discard_socket_parts);
    const char payload[] = "raw-perf-payload";
    const int payload_size = static_cast<int> (sizeof (payload) - 1);

    std::vector<void *> servers (handle_count_, NULL);
    std::vector<void *> clients (handle_count_, NULL);
    for (int i = 0; i < handle_count_; ++i) {
        servers[i] = zlink_socket (ctx, ZLINK_PAIR);
        clients[i] = zlink_socket (ctx, ZLINK_PAIR);
        TEST_ASSERT_NOT_NULL (servers[i]);
        TEST_ASSERT_NOT_NULL (clients[i]);
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_socket_attach_handler (servers[i], &msg_handler));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_socket_attach_handler (clients[i], &discard_handler));
        char endpoint[MAX_SOCKET_STRING];
        bind_loopback_ipv4 (servers[i], endpoint, sizeof (endpoint));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_connect (clients[i], endpoint));
        msleep (SETTLE_TIME);
        const int before = probe.calls.load (std::memory_order_acquire);
        TEST_ASSERT_EQUAL_INT (
          payload_size, zlink_send (clients[i], payload, payload_size, 0));
        TEST_ASSERT_TRUE (wait_for_probe_count (&probe, before + 1, 3000));
    }

    const int baseline = probe.calls.load (std::memory_order_acquire);

    std::mutex start_mutex;
    std::condition_variable start_cv;
    int ready_threads = 0;
    bool start = false;
    std::vector<int> worker_errno (handle_count_, 0);
    std::vector<std::thread> workers;

    for (int i = 0; i < handle_count_; ++i) {
        workers.push_back (std::thread ([&, i] () {
            {
                std::unique_lock<std::mutex> lock (start_mutex);
                ++ready_threads;
                start_cv.notify_all ();
                start_cv.wait (lock, [&start] () { return start; });
            }

            for (int seq = 0; seq < messages_per_handle_; ++seq) {
                if (zlink_send (clients[i], payload, payload_size, 0)
                    != payload_size) {
                    worker_errno[i] = errno;
                    return;
                }
            }
        }));
    }

    {
        std::unique_lock<std::mutex> lock (start_mutex);
        TEST_ASSERT_TRUE (start_cv.wait_for (
          lock, std::chrono::seconds (5),
          [&ready_threads, handle_count_] () {
              return ready_threads == handle_count_;
          }));
    }

    const std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now ();
    {
        std::lock_guard<std::mutex> lock (start_mutex);
        start = true;
    }
    start_cv.notify_all ();

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    const int expected = baseline + handle_count_ * messages_per_handle_;
    TEST_ASSERT_TRUE (wait_for_probe_count (&probe, expected, 10000));
    const std::chrono::steady_clock::time_point finished =
      std::chrono::steady_clock::now ();

    for (int i = 0; i < handle_count_; ++i)
        TEST_ASSERT_EQUAL_INT (0, worker_errno[i]);

    for (int i = 0; i < handle_count_; ++i) {
        close_socket_zero_linger (clients[i]);
        close_socket_zero_linger (servers[i]);
    }
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_raw_probe = NULL;

    const double elapsed_s =
      std::chrono::duration<double> (finished - started).count ();
    TEST_ASSERT_TRUE (elapsed_s > 0.0);
    return static_cast<double> (handle_count_ * messages_per_handle_) / elapsed_s;
}

double measure_gateway_handle_scaling_once (int handle_count_,
                                            int messages_per_handle_)
{
    void *ctx = get_test_context ();
    TEST_ASSERT_NOT_NULL (ctx);

    count_probe_t probe;
    g_gateway_probe = &probe;

    std::vector<void *> servers (handle_count_, NULL);
    std::vector<void *> clients (handle_count_, NULL);
    std::vector<std::string> endpoints (handle_count_);

    int port_seed = 24000;
    for (int i = 0; i < handle_count_; ++i) {
        char service_name[32];
        char server_rid_name[32];
        char client_rid_name[32];
        snprintf (service_name, sizeof (service_name), "perf-gw-%d", i);
        snprintf (server_rid_name, sizeof (server_rid_name), "gw-srv-%d", i);
        snprintf (client_rid_name, sizeof (client_rid_name), "gw-cli-%d", i);

        servers[i] = zlink_gateway_new (ctx, service_name, server_rid_name,
                                        &scaling_gateway_handler, NULL);
        clients[i] = zlink_gateway_new (ctx, service_name, client_rid_name,
                                        &discard_gateway_message, NULL);
        TEST_ASSERT_NOT_NULL (servers[i]);
        TEST_ASSERT_NOT_NULL (clients[i]);
        configure_gateway_linger_zero (servers[i]);
        configure_gateway_linger_zero (clients[i]);

        char endpoint[MAX_SOCKET_STRING];
        snprintf (endpoint, sizeof (endpoint), "tcp://127.0.0.1:%d",
                  test_port (port_seed++));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_bind (servers[i], endpoint));
        endpoints[i] = endpoint;

        zlink_routing_id_t rid;
        memset (&rid, 0, sizeof (rid));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_routing_id (servers[i], &rid));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_gateway_connect (clients[i], endpoints[i].c_str (), &rid));
        wait_gateway_ready (clients[i], 3000);
    }

    std::mutex start_mutex;
    std::condition_variable start_cv;
    int ready_threads = 0;
    bool start = false;
    std::vector<int> worker_errno (handle_count_, 0);
    std::vector<std::thread> workers;

    for (int i = 0; i < handle_count_; ++i) {
        workers.push_back (std::thread ([&, i] () {
            {
                std::unique_lock<std::mutex> lock (start_mutex);
                ++ready_threads;
                start_cv.notify_all ();
                start_cv.wait (lock, [&start] () { return start; });
            }

            for (int seq = 0; seq < messages_per_handle_; ++seq) {
                char payload[32];
                snprintf (payload, sizeof (payload), "gw-%d-%02d", i, seq);
                if (gateway_send_text (clients[i], payload) != 0) {
                    worker_errno[i] = errno;
                    return;
                }
            }
        }));
    }

    {
        std::unique_lock<std::mutex> lock (start_mutex);
        TEST_ASSERT_TRUE (start_cv.wait_for (
          lock, std::chrono::seconds (5),
          [&ready_threads, handle_count_] () {
              return ready_threads == handle_count_;
          }));
    }

    const std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now ();
    {
        std::lock_guard<std::mutex> lock (start_mutex);
        start = true;
    }
    start_cv.notify_all ();

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    const int expected = handle_count_ * messages_per_handle_;
    TEST_ASSERT_TRUE (wait_for_probe_count (&probe, expected, 15000));
    const std::chrono::steady_clock::time_point finished =
      std::chrono::steady_clock::now ();

    for (int i = 0; i < handle_count_; ++i)
        TEST_ASSERT_EQUAL_INT (0, worker_errno[i]);

    for (int i = 0; i < handle_count_; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&clients[i]));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_gateway_destroy (&servers[i]));
    }
    g_gateway_probe = NULL;

    const double elapsed_s =
      std::chrono::duration<double> (finished - started).count ();
    TEST_ASSERT_TRUE (elapsed_s > 0.0);
    return static_cast<double> (expected) / elapsed_s;
}

double measure_spot_handle_scaling_once (int handle_count_,
                                         int messages_per_handle_)
{
    void *ctx = zlink_ctx_new ();
    TEST_ASSERT_NOT_NULL (ctx);

    count_probe_t probe;
    g_spot_probe = &probe;

    std::vector<void *> pub_nodes (handle_count_, NULL);
    std::vector<void *> sub_nodes (handle_count_, NULL);
    std::vector<void *> pubs (handle_count_, NULL);
    std::vector<void *> subs (handle_count_, NULL);
    std::vector<std::string> endpoints (handle_count_);

    for (int i = 0; i < handle_count_; ++i) {
        char service_name[32];
        char topic[32];
        snprintf (service_name, sizeof (service_name), "perf-spot-%d", i);
        snprintf (topic, sizeof (topic), "perf-topic-%d", i);

        pub_nodes[i] = zlink_spot_node_new (ctx, service_name,
                                            &ignore_spot_handler, NULL);
        sub_nodes[i] = zlink_spot_node_new (ctx, service_name,
                                            &ignore_spot_handler, NULL);
        TEST_ASSERT_NOT_NULL (pub_nodes[i]);
        TEST_ASSERT_NOT_NULL (sub_nodes[i]);

        pubs[i] = zlink_spot_new (pub_nodes[i], &ignore_spot_handler, NULL);
        subs[i] = zlink_spot_new (sub_nodes[i], &scaling_spot_handler, NULL);
        TEST_ASSERT_NOT_NULL (pubs[i]);
        TEST_ASSERT_NOT_NULL (subs[i]);
        configure_spot_linger_zero (pub_nodes[i], pubs[i]);
        configure_spot_linger_zero (sub_nodes[i], subs[i]);

        char endpoint[MAX_SOCKET_STRING];
        make_unique_inproc_endpoint (endpoint, sizeof (endpoint));
        endpoints[i] = endpoint;
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_bind (pub_nodes[i], endpoints[i].c_str ()));
        TEST_ASSERT_SUCCESS_ERRNO (
          zlink_spot_node_connect_peer_pub (sub_nodes[i], endpoints[i].c_str ()));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_subscribe (subs[i], topic));
        msleep (20);

        const int before = probe.calls.load (std::memory_order_acquire);
        TEST_ASSERT_SUCCESS_ERRNO (
          spot_publish_text (pubs[i], topic, "warm"));
        TEST_ASSERT_TRUE (
          wait_for_probe_count (&probe, before + 1, 3000));
    }

    const int baseline = probe.calls.load (std::memory_order_acquire);
    std::mutex start_mutex;
    std::condition_variable start_cv;
    int ready_threads = 0;
    bool start = false;
    std::vector<int> worker_errno (handle_count_, 0);
    std::vector<std::thread> workers;

    for (int i = 0; i < handle_count_; ++i) {
        workers.push_back (std::thread ([&, i] () {
            char topic[32];
            snprintf (topic, sizeof (topic), "perf-topic-%d", i);

            {
                std::unique_lock<std::mutex> lock (start_mutex);
                ++ready_threads;
                start_cv.notify_all ();
                start_cv.wait (lock, [&start] () { return start; });
            }

            for (int seq = 0; seq < messages_per_handle_; ++seq) {
                char payload[32];
                snprintf (payload, sizeof (payload), "sp-%d-%02d", i, seq);
                if (spot_publish_text (pubs[i], topic, payload) != 0) {
                    worker_errno[i] = errno;
                    return;
                }
            }
        }));
    }

    {
        std::unique_lock<std::mutex> lock (start_mutex);
        TEST_ASSERT_TRUE (start_cv.wait_for (
          lock, std::chrono::seconds (5),
          [&ready_threads, handle_count_] () {
              return ready_threads == handle_count_;
          }));
    }

    const std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now ();
    {
        std::lock_guard<std::mutex> lock (start_mutex);
        start = true;
    }
    start_cv.notify_all ();

    for (size_t i = 0; i < workers.size (); ++i)
        workers[i].join ();

    const int expected = baseline + handle_count_ * messages_per_handle_;
    TEST_ASSERT_TRUE (wait_for_probe_count (&probe, expected, 15000));
    const std::chrono::steady_clock::time_point finished =
      std::chrono::steady_clock::now ();

    for (int i = 0; i < handle_count_; ++i)
        TEST_ASSERT_EQUAL_INT (0, worker_errno[i]);

    for (int i = 0; i < handle_count_; ++i) {
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&subs[i]));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_destroy (&pubs[i]));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&sub_nodes[i]));
        TEST_ASSERT_SUCCESS_ERRNO (zlink_spot_node_destroy (&pub_nodes[i]));
    }
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_shutdown (ctx));
    TEST_ASSERT_SUCCESS_ERRNO (zlink_ctx_term (ctx));
    g_spot_probe = NULL;

    const double elapsed_s =
      std::chrono::duration<double> (finished - started).count ();
    TEST_ASSERT_TRUE (elapsed_s > 0.0);
    return static_cast<double> (handle_count_ * messages_per_handle_) / elapsed_s;
}

void assert_scaling_contract (const char *label_,
                              const std::vector<double> &throughputs_)
{
    TEST_ASSERT_EQUAL_INT (4, static_cast<int> (throughputs_.size ()));

    const double baseline_per_handle = throughputs_[0];
    const double scaled_per_handle =
      throughputs_[3] / static_cast<double> (kPerfHandleCounts[3]);
    const double ratio = scaled_per_handle / baseline_per_handle;
    const double threshold = perf_ratio_threshold ();

    std::fprintf (stderr,
                  "[perf-contract] %s baseline_per_handle=%.2f scaled_per_handle=%.2f ratio=%.3f threshold=%.3f\n",
                  label_, baseline_per_handle, scaled_per_handle, ratio,
                  threshold);
    std::fflush (stderr);

    TEST_ASSERT_TRUE (baseline_per_handle > 0.0);
    TEST_ASSERT_TRUE (scaled_per_handle > 0.0);
    TEST_ASSERT_TRUE (ratio >= threshold);
}

void test_raw_handle_scaling_contract ()
{
    std::vector<double> throughputs;
    for (size_t i = 0; i < sizeof (kPerfHandleCounts) / sizeof (kPerfHandleCounts[0]);
         ++i) {
        throughputs.push_back (measure_raw_handle_scaling_once (
          kPerfHandleCounts[i], kRawMessagesPerHandle));
    }
    assert_scaling_contract ("raw", throughputs);
}

void test_gateway_handle_scaling_contract ()
{
    std::vector<double> throughputs;
    for (size_t i = 0; i < sizeof (kPerfHandleCounts) / sizeof (kPerfHandleCounts[0]);
         ++i) {
        throughputs.push_back (measure_gateway_handle_scaling_once (
          kPerfHandleCounts[i], kGatewayMessagesPerHandle));
    }
    assert_scaling_contract ("gateway", throughputs);
}

void test_spot_handle_scaling_contract ()
{
    std::vector<double> throughputs;
    for (size_t i = 0; i < sizeof (kPerfHandleCounts) / sizeof (kPerfHandleCounts[0]);
         ++i) {
        throughputs.push_back (measure_spot_handle_scaling_once (
          kPerfHandleCounts[i], kSpotMessagesPerHandle));
    }
    assert_scaling_contract ("spot", throughputs);
}
}

int main ()
{
    setup_test_environment ();

    const char *selected = getenv ("ZLINK_TEST_CASE");
    if (!selected || !*selected)
        return 77;

    UNITY_BEGIN ();
    if (should_run_named_test ("test_raw_handle_scaling_contract"))
        RUN_TEST (test_raw_handle_scaling_contract);
    if (should_run_named_test ("test_gateway_handle_scaling_contract"))
        RUN_TEST (test_gateway_handle_scaling_contract);
    if (should_run_named_test ("test_spot_handle_scaling_contract"))
        RUN_TEST (test_spot_handle_scaling_contract);
    return UNITY_END ();
}
