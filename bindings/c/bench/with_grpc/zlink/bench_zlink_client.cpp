#include "../common/bench_common.hpp"

#include <zlink.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

namespace
{
struct callback_state_t
{
    std::atomic<uint64_t> completed {0};
    std::atomic<uint64_t> errors {0};
    std::atomic<uint64_t> outstanding {0};
    zlink_c_bench::latency_sampler_t *latency = nullptr;
    std::mutex *latency_gate = nullptr;
};

struct request_tag_t
{
    callback_state_t *state;
};

struct request_metrics_t
{
    uint64_t submitted = 0;
    uint64_t blocked = 0;
    uint64_t submit_errors = 0;
    uint64_t max_outstanding = 0;
    double submit_wait_ms = 0.0;
};

void on_reply (zlink_request_result_t result, zlink_msg_t *parts, size_t part_count, void *userdata)
{
    request_tag_t *tag = static_cast<request_tag_t *> (userdata);
    callback_state_t *state = tag ? tag->state : nullptr;
    if (state && result == ZLINK_REQUEST_OK && parts && part_count > 0) {
        zlink_c_bench::decoded_header_t header {};
        if (zlink_c_bench::decode_payload (zlink_msg_data (&parts[0]), zlink_msg_size (&parts[0]),
                                           &header)) {
            const uint64_t now = zlink_c_bench::now_ns ();
            const double us = now >= header.sent_ns ? static_cast<double> (now - header.sent_ns) / 1000.0 : 0.0;
            std::lock_guard<std::mutex> lock (*state->latency_gate);
            state->latency->add_us (us);
            state->completed.fetch_add (1, std::memory_order_relaxed);
        } else {
            state->errors.fetch_add (1, std::memory_order_relaxed);
        }
    } else if (state) {
        state->errors.fetch_add (1, std::memory_order_relaxed);
    }
    if (state)
        state->outstanding.fetch_sub (1, std::memory_order_release);
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close (&parts[i]);
    delete tag;
}

bool make_msg (size_t size, uint32_t run_id, zlink_c_bench::phase_t phase, uint64_t seq, zlink_msg_t *msg)
{
    const size_t payload_size = std::max (size, zlink_c_bench::k_header_size);
    if (zlink_msg_init_size (msg, payload_size) != ZLINK_CONFIG_OK)
        return false;
    std::memset (zlink_msg_data (msg), 0xab, payload_size);
    return zlink_c_bench::stamp_payload (zlink_msg_data (msg), payload_size, run_id, phase, seq);
}

bool poll_once (void *poller, long timeout_ms)
{
    zlink_poller_event_t event {};
    zlink_config_result_t error = ZLINK_CONFIG_OK;
    const int rc = zlink_poller_wait (poller, &event, 1, timeout_ms, &error);
    if (rc < 0)
        return zlink_errno () == EINTR;
    return true;
}

bool submit_request_once (void *dealer,
                          size_t size,
                          uint32_t run_id,
                          uint64_t seq,
                          zlink_send_flags_t flags,
                          callback_state_t *cb,
                          request_metrics_t *metrics)
{
    zlink_msg_t msg;
    if (!make_msg (size, run_id, zlink_c_bench::phase_active, seq, &msg)) {
        ++metrics->submit_errors;
        return false;
    }

    auto *tag = new request_tag_t {cb};
    cb->outstanding.fetch_add (1, std::memory_order_release);
    metrics->max_outstanding = std::max<uint64_t> (
      metrics->max_outstanding, cb->outstanding.load (std::memory_order_acquire));
    const uint64_t submit_start = zlink_c_bench::now_ns ();
    const zlink_submit_result_t rc =
      zlink_dealer_request_part (dealer, &msg, flags, ZLINK_PART_FINAL, 5000, on_reply, tag);
    const uint64_t submit_stop = zlink_c_bench::now_ns ();
    metrics->submit_wait_ms += submit_stop >= submit_start
                                 ? static_cast<double> (submit_stop - submit_start) / 1000000.0
                                 : 0.0;
    if (rc == ZLINK_SUBMIT_OK) {
        ++metrics->submitted;
        return true;
    }

    cb->outstanding.fetch_sub (1, std::memory_order_release);
    delete tag;
    zlink_msg_close (&msg);
    if (rc == ZLINK_SUBMIT_BACKPRESSURED || zlink_errno () == EAGAIN || zlink_errno () == EWOULDBLOCK)
        ++metrics->blocked;
    else
        ++metrics->submit_errors;
    return false;
}

void drain_requests (void *poller, callback_state_t *cb)
{
    const int drain_ms = zlink_c_bench::env_int ("DRAIN_TIMEOUT_MS", 5000);
    const auto deadline = std::chrono::steady_clock::now () + std::chrono::milliseconds (drain_ms);
    while (cb->outstanding.load (std::memory_order_acquire) > 0
           && std::chrono::steady_clock::now () < deadline) {
        (void) poll_once (poller, 50);
    }
}

zlink_c_bench::result_t finish_request_result (const char *scenario,
                                               size_t size,
                                               const std::chrono::steady_clock::time_point &start,
                                               const std::chrono::steady_clock::time_point &stop,
                                               const zlink_c_bench::resource_sample_t &resources,
                                               callback_state_t *cb,
                                               zlink_c_bench::latency_sampler_t *latency,
                                               const request_metrics_t &metrics)
{
    const uint64_t pending = cb->outstanding.load (std::memory_order_acquire);
    zlink_c_bench::result_t r;
    r.scenario = scenario;
    r.size = size;
    r.unit = "KOPS";
    r.completed = cb->completed.load (std::memory_order_relaxed);
    r.errors = cb->errors.load (std::memory_order_relaxed) + metrics.submit_errors + pending;
    r.elapsed_s = std::chrono::duration<double> (stop - start).count ();
    r.mean_us = latency->mean_us ();
    r.p95_us = latency->percentile (0.95);
    r.p99_us = latency->percentile (0.99);
    r.cpu_percent = zlink_c_bench::cpu_percent (resources, r.elapsed_s);
    r.mem_mb = zlink_c_bench::rss_mb ();
    r.server_cpu_percent = zlink_c_bench::server_cpu_percent (resources, r.elapsed_s);
    r.server_mem_mb = zlink_c_bench::server_mem_mb (resources);
    r.submitted = metrics.submitted;
    r.blocked = metrics.blocked;
    r.max_outstanding = metrics.max_outstanding;
    r.submit_wait_ms = metrics.submit_wait_ms;
    return r;
}

zlink_c_bench::result_t run_request_serial (void *dealer, void *poller, size_t size)
{
    const int duration_s = zlink_c_bench::env_int ("DURATION_SECONDS", 3);
    zlink_c_bench::latency_sampler_t latency (200000);
    std::mutex latency_gate;
    callback_state_t cb;
    cb.latency = &latency;
    cb.latency_gate = &latency_gate;
    request_metrics_t metrics;
    const uint32_t run_id = static_cast<uint32_t> (zlink_c_bench::now_ns ());
    auto resources = zlink_c_bench::resource_start ();
    const auto start = std::chrono::steady_clock::now ();
    const auto deadline = start + std::chrono::seconds (duration_s);
    uint64_t seq = 0;
    while (std::chrono::steady_clock::now () < deadline) {
        if (submit_request_once (dealer, size, run_id, seq++, ZLINK_SEND_FLAGS_NONE, &cb, &metrics)) {
            while (cb.outstanding.load (std::memory_order_acquire) > 0)
                (void) poll_once (poller, 50);
        }
    }
    const auto stop = std::chrono::steady_clock::now ();
    return finish_request_result ("zlink-c-request-serial", size, start, stop, resources, &cb,
                                  &latency, metrics);
}

zlink_c_bench::result_t run_request_window (void *dealer,
                                            void *poller,
                                            size_t size,
                                            uint64_t window,
                                            const char *scenario)
{
    const int duration_s = zlink_c_bench::env_int ("DURATION_SECONDS", 3);
    zlink_c_bench::latency_sampler_t latency (200000);
    std::mutex latency_gate;
    callback_state_t cb;
    cb.latency = &latency;
    cb.latency_gate = &latency_gate;
    request_metrics_t metrics;
    const uint32_t run_id = static_cast<uint32_t> (zlink_c_bench::now_ns ());
    auto resources = zlink_c_bench::resource_start ();
    const auto start = std::chrono::steady_clock::now ();
    const auto deadline = start + std::chrono::seconds (duration_s);
    uint64_t seq = 0;
    uint64_t submitted_since_poll = 0;
    while (std::chrono::steady_clock::now () < deadline) {
        bool submitted_any = false;
        while (std::chrono::steady_clock::now () < deadline
               && cb.outstanding.load (std::memory_order_acquire) < window) {
            if (!submit_request_once (dealer, size, run_id, seq++, ZLINK_SEND_FLAGS_DONTWAIT, &cb,
                                      &metrics))
                break;
            submitted_any = true;
            if (++submitted_since_poll >= 64) {
                submitted_since_poll = 0;
                (void) poll_once (poller, 0);
            }
        }
        if (!submitted_any && cb.outstanding.load (std::memory_order_acquire) == 0) {
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
            continue;
        }
        (void) poll_once (poller, 1);
    }
    drain_requests (poller, &cb);
    const auto stop = std::chrono::steady_clock::now ();
    return finish_request_result (scenario, size, start, stop, resources, &cb, &latency, metrics);
}

zlink_c_bench::result_t run_send_loop (void *dealer,
                                       size_t size,
                                       zlink_send_flags_t flags,
                                       const char *scenario)
{
    const int duration_s = zlink_c_bench::env_int ("DURATION_SECONDS", 3);
    const uint32_t run_id = static_cast<uint32_t> (zlink_c_bench::now_ns ());
    auto resources = zlink_c_bench::resource_start ();
    const auto start = std::chrono::steady_clock::now ();
    const auto deadline = start + std::chrono::seconds (duration_s);
    uint64_t seq = 0;
    uint64_t blocked = 0;
    uint64_t errors = 0;
    double submit_wait_ms = 0.0;
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_msg_t msg;
        if (!make_msg (size, run_id, zlink_c_bench::phase_active, seq, &msg)) {
            ++errors;
            continue;
        }
        const uint64_t submit_start = zlink_c_bench::now_ns ();
        const zlink_submit_result_t rc = zlink_send_part (dealer, &msg, flags, ZLINK_PART_FINAL);
        const uint64_t submit_stop = zlink_c_bench::now_ns ();
        submit_wait_ms += submit_stop >= submit_start
                            ? static_cast<double> (submit_stop - submit_start) / 1000000.0
                            : 0.0;
        if (rc == ZLINK_SUBMIT_OK) {
            ++seq;
        } else {
            zlink_msg_close (&msg);
            if (rc == ZLINK_SUBMIT_BACKPRESSURED || zlink_errno () == EAGAIN
                || zlink_errno () == EWOULDBLOCK)
                ++blocked;
            else
                ++errors;
        }
    }
    const auto stop = std::chrono::steady_clock::now ();
    zlink_c_bench::result_t r;
    r.scenario = scenario;
    r.size = size;
    r.unit = "KMSG/s";
    r.completed = seq;
    r.errors = errors;
    r.elapsed_s = std::chrono::duration<double> (stop - start).count ();
    r.cpu_percent = zlink_c_bench::cpu_percent (resources, r.elapsed_s);
    r.mem_mb = zlink_c_bench::rss_mb ();
    r.server_cpu_percent = zlink_c_bench::server_cpu_percent (resources, r.elapsed_s);
    r.server_mem_mb = zlink_c_bench::server_mem_mb (resources);
    r.submitted = seq;
    r.blocked = blocked;
    r.submit_wait_ms = submit_wait_ms;
    return r;
}
}

int main ()
{
    const std::string request_endpoint =
      zlink_c_bench::env_string ("ZLINK_REQUEST_ENDPOINT", "tcp://127.0.0.1:6075");
    const std::string send_endpoint =
      zlink_c_bench::env_string ("ZLINK_SEND_ENDPOINT", "tcp://127.0.0.1:6077");
    const uint64_t window = static_cast<uint64_t> (zlink_c_bench::env_int ("WINDOW_SIZE", 100));
    const uint64_t saturation_window =
      static_cast<uint64_t> (zlink_c_bench::env_int ("MAX_OUTSTANDING", 4096));
    void *ctx = zlink_ctx_new ();
    void *request_dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *send_dealer = zlink_socket (ctx, ZLINK_SOCKET_DEALER);
    void *poller = zlink_poller_new ();
    if (!ctx || !request_dealer || !send_dealer || !poller)
        return 2;
    zlink_connect (request_dealer, request_endpoint.c_str ());
    zlink_connect (send_dealer, send_endpoint.c_str ());
    zlink_poller_add (poller, request_dealer, request_dealer, ZLINK_POLLCOMPLETION);
    std::this_thread::sleep_for (std::chrono::milliseconds (500));
    for (const size_t size : zlink_c_bench::parse_sizes ()) {
        zlink_c_bench::print_result (run_request_serial (request_dealer, poller, size));
        zlink_c_bench::print_result (
          run_request_window (request_dealer, poller, size, window, "zlink-c-request-window"));
        zlink_c_bench::print_result (run_request_window (
          request_dealer, poller, size, saturation_window, "zlink-c-request-saturation"));
        zlink_c_bench::print_result (
          run_send_loop (send_dealer, size, ZLINK_SEND_FLAGS_NONE, "zlink-c-send-blocking"));
        zlink_c_bench::print_result (run_send_loop (
          send_dealer, size, ZLINK_SEND_FLAGS_DONTWAIT, "zlink-c-send-saturation"));
    }
    zlink_poller_destroy (&poller);
    zlink_close (request_dealer);
    zlink_close (send_dealer);
    zlink_ctx_term (ctx);
    return 0;
}
