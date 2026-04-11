#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_handshake.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../common/perf_multi_spot_control.hpp"
#include "../common/perf_multi_spot_handle.hpp"
#include "../common/perf_multi_spot_handshake.hpp"
#include "../../common/perf_tls_setup.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const size_t k_topic_len = sizeof("bench") - 1;
static const char *k_control_ready_prefix = "CLIENT_CONTROL_ENDPOINT,";
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::print_client_result_lines;
using perf_multi_client::resolve_case_msg_sizes;

struct spot_client_slot_t
{
    spot_client_slot_t() :
        node(NULL),
        handle(NULL),
        control_pub(NULL),
        index(0),
        stop(false),
        phase_trace_msg_size(0),
        first_active_ns(0)
    {
    }

    void *node;
    void *handle;
    void *control_pub;
    std::string endpoint;
    size_t index;
    std::atomic<bool> stop;
    std::atomic<size_t> phase_trace_msg_size;
    std::atomic<uint64_t> first_active_ns;
};

struct spot_recv_worker_t;
struct spot_thread_metrics_t;

struct spot_client_state_t
{
    spot_client_state_t() :
        control_node(NULL),
        control_pub(NULL),
        control_sub(NULL),
        expected_msg_size(0),
        active_duration_ns(0),
        sender_window_start_ns(0),
        sender_window_end_ns(0),
        collect_active(false),
        expected_run_id(1U),
        fatal(false),
        ready_barrier_settled(false),
        control_connected(false),
        control_link_ready(false),
        control_started_msg_size(0),
        control_start_recv_ns(0),
        seen_msg_size(0),
        seen_phase(static_cast<int>(perf_multi_metric::phase_unknown)),
        start_gate(),
        recv_workers_stop(false),
        metrics_epoch(1)
    {
    }

    void *control_node;
    void *control_pub;
    void *control_sub;
    std::string control_endpoint;
    std::string server_control_endpoint;
    std::vector<spot_client_slot_t *> slots;
    std::vector<spot_recv_worker_t *> recv_workers;
    std::mutex mutex;
    std::condition_variable cv;
    std::mutex metrics_mutex;
    std::vector<spot_thread_metrics_t *> thread_metrics;
    std::atomic<size_t> expected_msg_size;
    std::atomic<uint64_t> active_duration_ns;
    std::atomic<uint64_t> sender_window_start_ns;
    std::atomic<uint64_t> sender_window_end_ns;
    std::atomic<bool> collect_active;
    std::atomic<uint32_t> expected_run_id;
    std::atomic<bool> fatal;
    std::atomic<bool> ready_barrier_settled;
    std::atomic<bool> control_connected;
    std::atomic<bool> control_link_ready;
    std::atomic<size_t> control_started_msg_size;
    std::atomic<uint64_t> control_start_recv_ns;
    std::atomic<size_t> seen_msg_size;
    std::atomic<int> seen_phase;
    perf_multi_handshake::start_signal_state_t start_gate;
    std::atomic<bool> recv_workers_stop;
    std::atomic<uint64_t> metrics_epoch;
};

spot_client_state_t *g_client_state = NULL;

struct spot_recv_worker_t
{
    spot_recv_worker_t() :
        state(NULL),
        poller(NULL)
    {
    }

    spot_client_state_t *state;
    void *poller;
    std::vector<spot_client_slot_t *> slots;
    std::vector<zlink_poller_event_t> events;
    std::thread thread;
};

struct spot_thread_metrics_t
{
    spot_thread_metrics_t() :
        owner(NULL),
        registered(false),
        epoch(0),
        active_received(0),
        sample_index(0)
    {
    }

    spot_client_state_t *owner;
    bool registered;
    std::mutex mutex;
    uint64_t epoch;
    unsigned long long active_received;
    unsigned long long sample_index;
    bench_latency_sampler_t latency;
};

static thread_local spot_thread_metrics_t g_spot_thread_metrics;

void fast_exit_process(int exit_code)
{
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
}

void mark_fatal()
{
    spot_client_state_t *state = g_client_state;
    if (!state)
        return;

    state->fatal.store(true, std::memory_order_release);
    state->cv.notify_all();
}

void reset_spot_thread_metrics(spot_thread_metrics_t *metrics, uint64_t epoch)
{
    if (!metrics)
        return;

    std::lock_guard<std::mutex> lock(metrics->mutex);
    metrics->epoch = epoch;
    metrics->active_received = 0;
    metrics->sample_index = 0;
    metrics->latency.reset();
}

spot_thread_metrics_t *bind_spot_thread_metrics(spot_client_state_t *state)
{
    if (!state)
        return NULL;

    spot_thread_metrics_t &metrics = g_spot_thread_metrics;
    if (!metrics.registered || metrics.owner != state) {
        metrics.owner = state;
        metrics.registered = true;
        std::lock_guard<std::mutex> lock(state->metrics_mutex);
        if (std::find(state->thread_metrics.begin(),
                      state->thread_metrics.end(),
                      &metrics)
            == state->thread_metrics.end()) {
            state->thread_metrics.push_back(&metrics);
        }
    }

    const uint64_t epoch = state->metrics_epoch.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(metrics.mutex);
        if (metrics.epoch != epoch) {
            metrics.epoch = epoch;
            metrics.active_received = 0;
            metrics.sample_index = 0;
            metrics.latency.reset();
        }
    }
    return &metrics;
}

void collect_spot_thread_metrics(spot_client_state_t *state,
                                 unsigned long long *active_received_out,
                                 bench_latency_stats_t *latency_out)
{
    if (active_received_out)
        *active_received_out = 0;
    if (latency_out)
        *latency_out = bench_latency_stats_t();
    if (!state)
        return;

    const uint64_t epoch = state->metrics_epoch.load(std::memory_order_acquire);
    bench_latency_sampler_t merged_latency;
    unsigned long long active_received = 0;
    {
        std::lock_guard<std::mutex> lock(state->metrics_mutex);
        for (size_t i = 0; i < state->thread_metrics.size(); ++i) {
            spot_thread_metrics_t *metrics = state->thread_metrics[i];
            if (!metrics)
                continue;
            std::lock_guard<std::mutex> metrics_lock(metrics->mutex);
            if (metrics->owner != state || metrics->epoch != epoch)
                continue;
            active_received += metrics->active_received;
            merged_latency.merge_from(metrics->latency);
        }
    }

    if (active_received_out)
        *active_received_out = active_received;
    if (latency_out)
        *latency_out = merged_latency.snapshot();
}

unsigned int resolve_spot_latency_sample_stride()
{
    return static_cast<unsigned int>(
      resolve_multi_int_env("PERF_MULTI_SPOT_LATENCY_SAMPLE_STRIDE", 32, 1));
}

int resolve_spot_ready_settle_ms()
{
    return resolve_multi_int_env("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000, 0);
}

int resolve_spot_control_settle_ms()
{
    return resolve_multi_int_env("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25, 0);
}

bool should_sample_spot_latency(unsigned long long sample_index)
{
    static const unsigned int stride = resolve_spot_latency_sample_stride();
    return stride <= 1 || sample_index == 1
           || (sample_index % static_cast<unsigned long long>(stride)) == 0;
}

size_t resolve_spot_recv_worker_count(size_t slot_count)
{
    if (slot_count == 0)
        return 0;

    const size_t configured = static_cast<size_t>(
      resolve_multi_int_env("PERF_MULTI_SPOT_RECV_WORKERS", 0, 0));
    if (configured > 0)
        return std::min(slot_count, configured);

    const size_t scaled =
      std::max<size_t>(4, std::min<size_t>(128, (slot_count + 15) / 16));
    return std::min(slot_count, scaled);
}

bool apply_spot_sub_options(void *sub, const multi_bench_settings_t &settings)
{
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_RCVTIMEO_MS", 200);
    apply_benchmark_socket_options(sub, settings.hwm, "tcp");
    return zlink_set_option(
             sub, ZLINK_OPT_RCVTIMEO, &rcvtimeo_ms, sizeof(rcvtimeo_ms))
           == 0;
}

bool wait_for_spot_ready_settle(spot_client_state_t *state)
{
    if (!state)
        return false;
    if (state->ready_barrier_settled.load(std::memory_order_acquire))
        return true;

    const int settle_ms = resolve_spot_ready_settle_ms();
    if (settle_ms <= 0) {
        state->ready_barrier_settled.store(true, std::memory_order_release);
        return true;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(settle_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        zlink_pollitem_t item = {NULL, 0, 0, 0};
        if (zlink_poll(&item, 0, 5) < 0 && zlink_errno() != EINTR)
            return false;
    }

    state->ready_barrier_settled.store(true, std::memory_order_release);
    return true;
}

bool publish_control_ready_count(spot_client_state_t *state,
                                 size_t msg_size,
                                 size_t ready_count)
{
    return state
           && perf_multi_spot_control::publish_ready_count(
             state->control_pub, k_topic, msg_size, ready_count);
}

std::string bind_client_spot_endpoint(void *node,
                                      const std::string &transport,
                                      size_t slot_index)
{
    if (!node) {
        errno = EINVAL;
        return std::string();
    }

    const int base_bind_port =
      resolve_multi_int_env("PERF_MULTI_CLIENT_BIND_PORT", 0, 0);
    std::string bind_endpoint;
    if (base_bind_port > 0) {
        bind_endpoint = make_fixed_endpoint(
          transport, base_bind_port + static_cast<int>(slot_index));
    } else {
        const int slot_offset = static_cast<int>(slot_index % 32) * 64;
        int range_base_port = 33000 + slot_offset;
#if !defined(_WIN32)
        range_base_port += static_cast<int>(::getpid() % 1000) * 16;
#endif
        bind_endpoint = perf_bind_fixed_endpoint_range(
          node, transport, range_base_port, 64, &perf_bind_spot_node_endpoint);
    }

    if (bind_endpoint.empty()) {
        errno = EINVAL;
        return std::string();
    }

    if (base_bind_port > 0 && zlink_spot_node_bind(node, bind_endpoint.c_str()) != 0)
        return std::string();

    zlink_spot_node_status_t status;
    std::memset(&status, 0, sizeof(status));
    if (zlink_spot_node_status_snapshot(node, &status) != 0
        || status.local_endpoint[0] == '\0') {
        errno = zlink_errno() != 0 ? zlink_errno() : EIO;
        return std::string();
    }

    return perf_normalize_bind_endpoint_host(status.local_endpoint, transport);
}

bool parse_control_endpoint_arg(int argc,
                                char **argv,
                                std::string *endpoint_out)
{
    if (!endpoint_out)
        return false;

    endpoint_out->clear();
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--control-endpoint") == 0) {
            *endpoint_out = argv[i + 1];
            return !endpoint_out->empty();
        }
    }

    return false;
}

void destroy_spot_slots(spot_client_state_t *state,
                        std::vector<spot_client_slot_t *> *slots)
{
    if (!slots || !state)
        return;

    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] destroy start ts_ns="
                  << perf_multi_metric::now_ns()
                  << " slots=" << slots->size() << std::endl;
    }

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        slot->stop.store(true, std::memory_order_release);
    }

    if (state)
        state->recv_workers_stop.store(true, std::memory_order_release);

    if (state) {
        for (size_t i = 0; i < state->recv_workers.size(); ++i) {
            spot_recv_worker_t *worker = state->recv_workers[i];
            if (!worker)
                continue;
            if (worker->thread.joinable())
                worker->thread.join();
            if (worker->poller)
                zlink_poller_destroy(&worker->poller);
            delete worker;
        }
        state->recv_workers.clear();
    }

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        if (bench_transition_debug_enabled() && i < 4) {
            std::cerr << "[multi-spot-client] destroy slot begin index=" << i
                      << " ts_ns=" << perf_multi_metric::now_ns()
                      << " node=" << (slot->node ? 1 : 0) << std::endl;
        }
        if (slot->control_pub)
            perf_destroy_default_spot_handle(&slot->control_pub);
        if (slot->handle)
            perf_destroy_default_spot_handle(&slot->handle);
        if (slot->node) {
            zlink_spot_node_destroy(&slot->node);
            if (bench_transition_debug_enabled() && i < 4) {
                std::cerr << "[multi-spot-client] destroy slot after node index="
                          << i << " ts_ns=" << perf_multi_metric::now_ns()
                          << std::endl;
            }
        }
        delete slot;
    }

    slots->clear();
    if (state->control_pub)
        perf_destroy_default_spot_handle(&state->control_pub);
    if (state->control_sub)
        perf_destroy_default_spot_handle(&state->control_sub);
    if (state->control_node)
        zlink_spot_node_destroy(&state->control_node);
    state->control_connected.store(false, std::memory_order_release);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] destroy done ts_ns="
                  << perf_multi_metric::now_ns() << std::endl;
    }
}

void handle_spot_client_parts(const char *topic,
                              size_t topic_len,
                              zlink_msg_t *parts,
                              size_t part_count,
                              spot_client_slot_t *slot)
{
    spot_client_state_t *state = g_client_state;
    if (!state || !topic || part_count == 0) {
        perf_close_multipart(parts, part_count);
        return;
    }

    if (topic_len != k_topic_len
        || std::memcmp(topic, k_topic, k_topic_len) != 0) {
        perf_close_multipart(parts, part_count);
        return;
    }

    size_t started_size = 0;
    if (perf_multi_spot_handshake::parse_start_command(zlink_msg_data(&parts[0]),
                                                       zlink_msg_size(&parts[0]),
                                                       &started_size)) {
        const uint64_t start_recv_ns =
          bench_transition_debug_enabled() ? perf_multi_metric::now_ns() : 0;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->control_started_msg_size.store(started_size,
                                                  std::memory_order_relaxed);
            if (start_recv_ns != 0)
                state->control_start_recv_ns.store(start_recv_ns,
                                                   std::memory_order_relaxed);
            state->seen_msg_size.store(started_size,
                                       std::memory_order_relaxed);
            state->seen_phase.store(
              static_cast<int>(perf_multi_metric::phase_active),
              std::memory_order_relaxed);
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-client] start recv ts_ns="
                      << perf_multi_metric::now_ns()
                      << " size=" << started_size
                      << " slot=" << (slot ? slot->index : 0) << std::endl;
        }
        perf_close_multipart(parts, part_count);
        state->cv.notify_all();
        return;
    }

    perf_multi_metric::header_t header;
    const bool header_ok =
      perf_multi_metric::decode_payload_header(zlink_msg_data(&parts[0]),
                                               zlink_msg_size(&parts[0]),
                                               &header);
    perf_close_multipart(parts, part_count);
    if (!header_ok)
        return;

    const size_t expected_msg_size =
      state->expected_msg_size.load(std::memory_order_acquire);
    if (expected_msg_size != 0 && header.msg_size != expected_msg_size)
        return;

    const int header_phase = static_cast<int>(header.phase);
    size_t previous_msg_size = 0;
    int previous_phase = static_cast<int>(perf_multi_metric::phase_unknown);
    bool phase_changed = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        previous_msg_size =
          state->seen_msg_size.load(std::memory_order_relaxed);
        previous_phase = state->seen_phase.load(std::memory_order_relaxed);
        const bool phase_regressed =
          previous_msg_size == header.msg_size && previous_phase > header_phase;
        phase_changed =
          !phase_regressed
          && (previous_msg_size != header.msg_size
              || previous_phase != header_phase);
        if (phase_changed) {
            state->seen_msg_size.store(header.msg_size,
                                       std::memory_order_relaxed);
            state->seen_phase.store(header_phase, std::memory_order_relaxed);
        }
    }
    if (phase_changed && bench_transition_debug_enabled()
        && previous_msg_size != header.msg_size) {
        std::cerr << "[multi-spot-client] transition recv ts_ns="
                  << perf_multi_metric::now_ns()
                  << " size=" << header.msg_size
                  << " phase=" << header.phase
                  << " prev_size=" << previous_msg_size
                  << " prev_phase=" << previous_phase << std::endl;
    }
    const bool collect_active =
      state->collect_active.load(std::memory_order_acquire);
    const bool trace_phases = bench_transition_debug_enabled();
    const uint64_t recv_ts_ns =
      trace_phases ? perf_multi_metric::now_ns() : 0;

    if (trace_phases && slot
        && header.msg_size
             == state->expected_msg_size.load(std::memory_order_acquire)) {
        if (header.phase
              == static_cast<uint32_t>(perf_multi_metric::phase_active)
            && slot->first_active_ns.load(std::memory_order_acquire) == 0) {
            uint64_t expected = 0;
            (void) slot->first_active_ns.compare_exchange_strong(
              expected, recv_ts_ns, std::memory_order_acq_rel);
        }
    }

    if (bench_debug_enabled() && previous_msg_size == 0) {
        std::cerr << "[multi-spot-client] first recv size="
                  << header.msg_size << " phase=" << header.phase
                  << " run=" << header.run_id << std::endl;
    }

    if (collect_active
        && header.magic == perf_multi_metric::k_magic
        && header.run_id
             == state->expected_run_id.load(std::memory_order_acquire)
        && header.phase
             == static_cast<uint32_t> (perf_multi_metric::phase_active)
        && header.msg_size
             == state->expected_msg_size.load(std::memory_order_acquire)) {
        uint64_t sender_window_end_ns =
          state->sender_window_end_ns.load(std::memory_order_acquire);
        if (sender_window_end_ns == 0) {
            const uint64_t duration_ns =
              state->active_duration_ns.load(std::memory_order_acquire);
            const uint64_t sender_window_start_ns =
              header.sent_ts_ns > 0 ? header.sent_ts_ns : recv_ts_ns;
            uint64_t expected = 0;
            if (state->sender_window_start_ns.compare_exchange_strong(
                  expected, sender_window_start_ns, std::memory_order_acq_rel)) {
                sender_window_end_ns = sender_window_start_ns + duration_ns;
                state->sender_window_end_ns.store(sender_window_end_ns,
                                                  std::memory_order_release);
                state->cv.notify_all();
            } else {
                sender_window_end_ns =
                  state->sender_window_end_ns.load(std::memory_order_acquire);
            }
        }
        if (sender_window_end_ns != 0 && header.sent_ts_ns > 0
            && header.sent_ts_ns > sender_window_end_ns) {
            return;
        }

        spot_thread_metrics_t *metrics = bind_spot_thread_metrics(state);
        if (metrics) {
            std::lock_guard<std::mutex> metrics_lock(metrics->mutex);
            ++metrics->active_received;
            if (should_sample_spot_latency(++metrics->sample_index)) {
                const uint64_t sample_ts_ns =
                  trace_phases ? recv_ts_ns : perf_multi_metric::now_ns();
                const double latency_ns =
                  header.sent_ts_ns > 0 && sample_ts_ns >= header.sent_ts_ns
                    ? static_cast<double>(sample_ts_ns - header.sent_ts_ns)
                    : 0.0;
                metrics->latency.add(latency_ns);
            }
        }
    }

    if (phase_changed)
        state->cv.notify_all();
}

void spot_client_sub_handler(const zlink_routing_id_t *,
                             const char *topic,
                             size_t topic_len,
                             zlink_msg_t *parts,
                             size_t part_count,
                             void *user_data)
{
    handle_spot_client_parts(topic, topic_len, parts, part_count,
                             static_cast<spot_client_slot_t *>(user_data));
}

bool recv_one_spot_message(spot_client_slot_t *slot, int flags, bool *received)
{
    if (!slot || !slot->handle)
        return false;

    if (received)
        *received = false;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof(topic) - 1;
    const int rc = zlink_subscribe(
      slot->handle, &parts, &part_count, flags, topic, &topic_len);
    if (rc != 0) {
        int err = zlink_errno();
        if (err == 0)
            err = errno;
        if (slot->stop.load(std::memory_order_acquire))
            return false;
        if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
            || err == ETIMEDOUT) {
            return true;
        }
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] recv fatal slot=" << slot->index
                      << " err=" << err << std::endl;
        }
        mark_fatal();
        return false;
    }

    if (received)
        *received = true;
    if (topic_len < sizeof(topic))
        topic[topic_len] = '\0';
    handle_spot_client_parts(topic, topic_len, parts, part_count, slot);
    return true;
}

bool drain_spot_client_slot(spot_client_slot_t *slot, bool *progressed)
{
    if (!slot)
        return false;

    while (!slot->stop.load(std::memory_order_acquire)) {
        bool received = false;
        if (!recv_one_spot_message(slot, ZLINK_DONTWAIT, &received))
            return false;
        if (!received)
            break;
        if (progressed)
            *progressed = true;
    }

    return true;
}

void spot_client_recv_worker_loop(spot_recv_worker_t *worker)
{
    if (!worker || !worker->state || !worker->poller)
        return;

    while (!worker->state->recv_workers_stop.load(std::memory_order_acquire)) {
        const int poll_rc =
          zlink_poller_wait_all(worker->poller,
                                worker->events.empty() ? NULL : &worker->events[0],
                                static_cast<int>(worker->events.size()),
                                5);
        if (poll_rc < 0) {
            const int err = zlink_errno();
            if (worker->state->recv_workers_stop.load(std::memory_order_acquire))
                break;
            if (err == EINTR || err == EAGAIN)
                continue;
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-client] poller fatal err=" << err
                          << std::endl;
            }
            mark_fatal();
            return;
        }

        for (int i = 0; i < poll_rc; ++i) {
            if ((worker->events[i].events & ZLINK_POLLIN) == 0)
                continue;

            spot_client_slot_t *slot =
              static_cast<spot_client_slot_t *>(worker->events[i].user_data);
            if (!slot || slot->stop.load(std::memory_order_acquire))
                continue;
            if (!drain_spot_client_slot(slot, NULL))
                return;
        }
    }
}

bool start_spot_recv_workers(spot_client_state_t *state)
{
    if (!state || state->slots.empty())
        return false;

    const size_t worker_count =
      resolve_spot_recv_worker_count(state->slots.size());
    if (worker_count == 0)
        return false;

    state->recv_workers_stop.store(false, std::memory_order_release);
    state->recv_workers.reserve(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        spot_recv_worker_t *worker = new (std::nothrow) spot_recv_worker_t();
        if (!worker)
            return false;
        worker->state = state;
        worker->poller = zlink_poller_new();
        if (!worker->poller) {
            delete worker;
            return false;
        }
        state->recv_workers.push_back(worker);
    }

    for (size_t i = 0; i < state->slots.size(); ++i) {
        spot_client_slot_t *slot = state->slots[i];
        spot_recv_worker_t *worker = state->recv_workers[i % worker_count];
        if (!slot || !worker || !worker->poller || !slot->handle
            || zlink_poller_add(worker->poller, slot->handle, slot, ZLINK_POLLIN)
                 != 0) {
            return false;
        }
        worker->slots.push_back(slot);
    }

    for (size_t i = 0; i < state->recv_workers.size(); ++i) {
        spot_recv_worker_t *worker = state->recv_workers[i];
        if (!worker || worker->slots.empty())
            continue;
        worker->events.resize(worker->slots.size());
        worker->thread = std::thread(spot_client_recv_worker_loop, worker);
    }

    return true;
}

bool recv_one_control_message(spot_client_state_t *state, bool *received)
{
    if (!state)
        return false;

    std::string payload;
    if (!perf_multi_spot_control::receive_control_payload(
          state->control_sub, k_topic, &payload, received)) {
        return false;
    }

    size_t started_size = 0;
    if (perf_multi_spot_handshake::parse_control_connected(payload.data(),
                                                           payload.size())) {
        state->control_link_ready.store(true, std::memory_order_release);
        state->cv.notify_all();
        return true;
    }
    if (perf_multi_spot_handshake::parse_start_command(payload.data(),
                                                       payload.size(),
                                                       &started_size)) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->control_started_msg_size.store(started_size,
                                                  std::memory_order_relaxed);
            state->seen_msg_size.store(started_size, std::memory_order_relaxed);
            state->seen_phase.store(
              static_cast<int>(perf_multi_metric::phase_active),
              std::memory_order_relaxed);
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-client] control start recv ts_ns="
                      << perf_multi_metric::now_ns()
                      << " size=" << started_size << std::endl;
        }
        state->cv.notify_all();
    }
    return true;
}

bool wait_for_control_link_ready(spot_client_state_t *state, int timeout_ms)
{
    return perf_multi_spot_control::wait_for_control_link_ready(
      state, timeout_ms, recv_one_control_message);
}

bool create_control_spot(ctx_guard_t &ctx,
                         const std::string &transport,
                         const std::string &server_control_endpoint,
                         const multi_bench_settings_t &settings,
                         spot_client_state_t *state)
{
    if (!state)
        return false;

    state->control_node = zlink_spot_node_new(ctx.get());
    if (!state->control_node
        || !setup_tls_server(state->control_node, transport)
        || !setup_tls_client(state->control_node, transport)) {
        if (state->control_node)
            zlink_spot_node_destroy(&state->control_node);
        return false;
    }

    state->control_pub = perf_create_default_spot_handle(state->control_node);
    state->control_sub = perf_create_default_spot_handle(state->control_node);
    if (!state->control_pub || !state->control_sub) {
        if (state->control_pub)
            perf_destroy_default_spot_handle(&state->control_pub);
        if (state->control_sub)
            perf_destroy_default_spot_handle(&state->control_sub);
        zlink_spot_node_destroy(&state->control_node);
        return false;
    }
    if (!perf_multi_spot_control::apply_control_options(
          state->control_pub, state->control_sub, settings)) {
        return false;
    }

    state->control_endpoint =
      bind_client_spot_endpoint(state->control_node, transport, 10000);
    state->server_control_endpoint = server_control_endpoint;
    if (state->control_endpoint.empty()) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control bind failed err="
                      << zlink_errno() << std::endl;
        }
        return false;
    }
    if (zlink_spot_node_connect_peer(state->control_node,
                                     server_control_endpoint.c_str())
        != 0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control connect failed endpoint="
                      << server_control_endpoint
                      << " err=" << zlink_errno() << std::endl;
        }
        return false;
    }
    if (!zlink_set_subscription(state->control_sub, k_topic)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control subscribe failed err="
                      << zlink_errno() << std::endl;
        }
        return false;
    }
    if (zlink_publish(state->control_pub, k_topic, NULL, 0, 0) != 0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control primer publish failed err="
                      << zlink_errno() << std::endl;
        }
        return false;
    }

    state->control_connected.store(true, std::memory_order_release);

    std::cout << k_control_ready_prefix << state->control_endpoint << std::endl;
    return true;
}

bool ensure_control_connected(spot_client_state_t *state)
{
    return state
           && perf_multi_spot_control::ensure_connected(
             state->control_node,
             state->server_control_endpoint,
             &state->control_connected);
}

bool create_spot_slots(ctx_guard_t &ctx,
                       const std::string &transport,
                       const std::string &endpoint,
                       const multi_bench_settings_t &settings,
                       spot_client_state_t *state,
                       std::vector<spot_client_slot_t *> *slots_out)
{
    if (!state || !slots_out)
        return false;

    const size_t service_clients =
      resolve_multi_service_clients(settings.clients);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] create slots begin ts_ns="
                  << perf_multi_metric::now_ns()
                  << " slots=" << service_clients
                  << " mode=recv" << std::endl;
    }
    for (size_t i = 0; i < service_clients; ++i) {
        spot_client_slot_t *slot = new (std::nothrow) spot_client_slot_t();
        if (!slot)
            return false;
        slot->index = i;

        slot->node = zlink_spot_node_new(ctx.get());
        if (!slot->node || !setup_tls_client(slot->node, transport)) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-client] node create/tls failed slot="
                          << i << " err=" << zlink_errno() << std::endl;
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        slot->handle = perf_create_default_spot_handle(slot->node);
        if (!slot->handle) {
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        if (!apply_spot_sub_options(slot->handle, settings)
            || zlink_spot_node_connect_peer(slot->node, endpoint.c_str()) != 0
            || zlink_set_subscription (slot->handle, k_topic)
                 != 0) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-client] slot create failed slot=" << i
                          << " node=" << (slot->node != NULL)
                          << " err=" << zlink_errno() << std::endl;
            if (slot->handle)
                perf_destroy_default_spot_handle(&slot->handle);
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        slots_out->push_back(slot);
        if (bench_transition_debug_enabled()
            && (((i + 1) % 25) == 0 || (i + 1) == service_clients)) {
            std::cerr << "[multi-spot-client] create slots progress ts_ns="
                      << perf_multi_metric::now_ns()
                      << " ready_slots=" << (i + 1)
                      << "/" << service_clients << std::endl;
        }
    }

    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] create slots done ts_ns="
                  << perf_multi_metric::now_ns()
                  << " slots=" << slots_out->size() << std::endl;
    }
    if (!start_spot_recv_workers(state))
        return false;
    return true;
}

void reset_metrics(spot_client_state_t *state, size_t msg_size)
{
    state->expected_msg_size.store(msg_size, std::memory_order_release);
    state->sender_window_start_ns.store(0, std::memory_order_release);
    state->sender_window_end_ns.store(0, std::memory_order_release);
    state->collect_active.store(false, std::memory_order_release);
    state->control_started_msg_size.store(0, std::memory_order_release);
    state->control_start_recv_ns.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->seen_msg_size.store(0, std::memory_order_relaxed);
        state->seen_phase.store(
          static_cast<int>(perf_multi_metric::phase_unknown),
          std::memory_order_relaxed);
    }
    for (size_t i = 0; i < state->slots.size(); ++i) {
        spot_client_slot_t *slot = state->slots[i];
        if (!slot)
            continue;
        slot->phase_trace_msg_size.store(msg_size, std::memory_order_release);
        slot->first_active_ns.store(0, std::memory_order_release);
    }
    state->metrics_epoch.fetch_add(1, std::memory_order_acq_rel);
}

void print_phase_spread_summary(spot_client_state_t *state, size_t msg_size)
{
    if (!state || !bench_transition_debug_enabled())
        return;

    uint64_t active_min_ns = 0;
    uint64_t active_max_ns = 0;
    size_t active_count = 0;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        spot_client_slot_t *slot = state->slots[i];
        if (!slot
            || slot->phase_trace_msg_size.load(std::memory_order_acquire)
                 != msg_size) {
            continue;
        }

        const uint64_t active_ns =
          slot->first_active_ns.load(std::memory_order_acquire);

        if (active_ns != 0) {
            ++active_count;
            if (active_min_ns == 0 || active_ns < active_min_ns)
                active_min_ns = active_ns;
            if (active_ns > active_max_ns)
                active_max_ns = active_ns;
        }
    }

    const uint64_t start_recv_ns =
      state->control_start_recv_ns.load(std::memory_order_acquire);
    const uint64_t active_spread_ns =
      active_max_ns >= active_min_ns ? active_max_ns - active_min_ns : 0;

    std::cerr << "[multi-spot-client] phase spread size=" << msg_size
              << " start_recv_ns=" << start_recv_ns
              << " active_slots=" << active_count
              << " active_first_delta_ns="
              << ((start_recv_ns != 0 && active_min_ns >= start_recv_ns)
                    ? (active_min_ns - start_recv_ns)
                    : 0)
              << " active_last_delta_ns="
              << ((start_recv_ns != 0 && active_max_ns >= start_recv_ns)
                    ? (active_max_ns - start_recv_ns)
                    : 0)
              << " active_spread_ns=" << active_spread_ns
              << std::endl;
}

bool wait_msg_size_start(spot_client_state_t *state,
                         size_t msg_size,
                         int timeout_ms)
{
    const size_t ready_count = state ? state->slots.size() : 0;
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    auto next_ready_republish_at = std::chrono::steady_clock::now()
                                   + std::chrono::milliseconds(50);

    while (std::chrono::steady_clock::now() < deadline) {
        if (perf_multi_handshake::wait_for_start(&state->start_gate,
                                                 msg_size,
                                                 1)) {
            const uint64_t start_recv_ns =
              bench_transition_debug_enabled() ? perf_multi_metric::now_ns() : 0;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->control_started_msg_size.store(
                  msg_size, std::memory_order_relaxed);
                if (start_recv_ns != 0) {
                    state->control_start_recv_ns.store(
                      start_recv_ns, std::memory_order_relaxed);
                }
                state->seen_msg_size.store(msg_size, std::memory_order_relaxed);
                state->seen_phase.store(
                  static_cast<int>(perf_multi_metric::phase_active),
                  std::memory_order_relaxed);
            }
            state->cv.notify_all();
            return true;
        }
        if (state->fatal.load(std::memory_order_acquire)
            || state->control_started_msg_size.load(std::memory_order_acquire)
                 == msg_size) {
            return true;
        }

        if (ready_count > 0
            && std::chrono::steady_clock::now() >= next_ready_republish_at) {
            (void) publish_control_ready_count(state, msg_size, ready_count);
            next_ready_republish_at =
              std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        }

        bool received = false;
        if (!recv_one_control_message(state, &received))
            return false;
        if (state->fatal.load(std::memory_order_acquire)
            || state->control_started_msg_size.load(std::memory_order_acquire)
                 == msg_size) {
            return true;
        }

        zlink_pollitem_t item = {NULL, 0, 0, 0};
        if (zlink_poll(&item, 0, 5) < 0 && zlink_errno() != EINTR)
            return false;
    }

    return false;
}

bool wait_phase_start(spot_client_state_t *state,
                      size_t msg_size,
                      perf_multi_metric::phase_t expected_phase,
                      int timeout_ms)
{
    return perf_multi_spot_control::wait_for_phase(
      state, msg_size, static_cast<int>(expected_phase), timeout_ms);
}

int resolve_spot_phase_timeout_ms(const multi_bench_settings_t &settings,
                                  size_t msg_size)
{
    int timeout_ms =
      std::max(settings.connect_ready_timeout_ms,
               std::max(1, settings.duration_seconds) * 5000);

    if (settings.clients >= 400) {
        timeout_ms =
          std::max(timeout_ms,
                   std::max(30000, settings.connect_ready_timeout_ms * 3));
    }
    if (settings.clients >= 1000) {
        timeout_ms =
          std::max(timeout_ms,
                   std::max(60000, settings.connect_ready_timeout_ms * 6));
    }

    // Large recv-mode fan-in cases can accumulate a substantial ready-to-active
    // transition backlog before the active phase becomes visible on the client.
    // Give the phase transition enough time to drain queued traffic instead of failing
    // the benchmark while the service is still making forward progress.
    if (msg_size >= 65536) {
        const int base_large_timeout =
          settings.clients >= 100 ? 60000 : 30000;
        timeout_ms = std::max (
          timeout_ms,
          std::max (base_large_timeout,
                    settings.connect_ready_timeout_ms * 6));
    }
    if (msg_size >= 131072) {
        timeout_ms =
          std::max(timeout_ms,
                   std::max(90000, settings.connect_ready_timeout_ms * 12));
    }
    if (msg_size >= 262144) {
        timeout_ms =
          std::max(timeout_ms,
                   std::max(120000, settings.connect_ready_timeout_ms * 18));
    }

    return resolve_multi_int_env("PERF_MULTI_SPOT_PHASE_TIMEOUT_MS",
                                 timeout_ms,
                                 1);
}

bool wait_spot_sender_window_done(spot_client_state_t *state)
{
    if (!state)
        return false;

    const uint64_t grace_ns =
      state->active_duration_ns.load(std::memory_order_acquire);
    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->fatal.load(std::memory_order_acquire)) {
        const uint64_t sender_window_end_ns =
          state->sender_window_end_ns.load(std::memory_order_acquire);
        if (sender_window_end_ns != 0) {
            const uint64_t deadline_ns = sender_window_end_ns + grace_ns;
            if (perf_multi_metric::now_ns() >= deadline_ns)
                break;
        }
        state->cv.wait_for(lock, std::chrono::milliseconds(5));
    }

    return !state->fatal.load(std::memory_order_acquire);
}

bool run_single_size_case(spot_client_state_t *state,
                          const multi_bench_settings_t &settings,
                          const std::string &lib_name,
                          const std::string &transport,
                          size_t msg_size,
                          uint32_t run_id)
{
    const int phase_timeout_ms =
      resolve_spot_phase_timeout_ms(settings, msg_size);

    if (!wait_for_control_link_ready(state, settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control link ready timeout"
                      << " size=" << msg_size << std::endl;
        }
        return false;
    }

    if (!wait_for_spot_ready_settle(state)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] ready settle failed size="
                      << msg_size << " err=" << zlink_errno() << std::endl;
        }
        return false;
    }

    std::cout << "CLIENT_READY," << msg_size << std::endl;

    state->expected_run_id.store(run_id, std::memory_order_release);
    reset_metrics(state, msg_size);
    state->active_duration_ns.store(
      static_cast<uint64_t>(std::max(1, settings.duration_seconds))
      * 1000000000ULL,
      std::memory_order_release);
    if (!ensure_control_connected(state)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] ensure control connect failed"
                      << " size=" << msg_size
                      << " err=" << zlink_errno() << std::endl;
        }
        return false;
    }
    const int control_settle_ms = resolve_spot_control_settle_ms();
    if (control_settle_ms > 0
        && perf_socket_poll(NULL, 0, control_settle_ms) < 0
        && zlink_errno() != EINTR) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control settle failed"
                      << " size=" << msg_size
                      << " settle_ms=" << control_settle_ms
                      << " err=" << zlink_errno() << std::endl;
        }
        return false;
    }
    if (!publish_control_ready_count(state, msg_size, state->slots.size())) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control ready count publish failed"
                      << " size=" << msg_size
                      << " count=" << state->slots.size()
                      << " err=" << zlink_errno() << std::endl;
        }
        return false;
    }

    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] size wait start ts_ns="
                  << perf_multi_metric::now_ns()
                  << " size=" << msg_size
                  << " timeout_ms=" << phase_timeout_ms << std::endl;
    }

    if (!wait_msg_size_start(state, msg_size, phase_timeout_ms)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] size start timeout size="
                      << msg_size << std::endl;
        }
        return false;
    }
    if (bench_debug_enabled()) {
        std::cerr << "[multi-spot-client] collect start size=" << msg_size
                  << " any-phase=1" << std::endl;
    }
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] collect start ts_ns="
                  << perf_multi_metric::now_ns()
                  << " size=" << msg_size << std::endl;
    }

    state->collect_active.store(true, std::memory_order_release);
    if (!wait_phase_start(state,
                          msg_size,
                          perf_multi_metric::phase_active,
                          phase_timeout_ms)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] active start timeout size="
                      << msg_size << std::endl;
        }
        state->collect_active.store(false, std::memory_order_release);
        return false;
    }

    if (!wait_spot_sender_window_done(state)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-client] active wait failed size="
                      << msg_size << std::endl;
        return false;
    }
    state->collect_active.store(false, std::memory_order_release);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] collect done ts_ns="
                  << perf_multi_metric::now_ns()
                  << " size=" << msg_size << std::endl;
    }
    if (bench_transition_debug_enabled())
        print_phase_spread_summary(state, msg_size);

    bench_latency_stats_t latency;
    double throughput = 0.0;

    unsigned long long active_received = 0;
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] metrics merge start ts_ns="
                  << perf_multi_metric::now_ns()
                  << " size=" << msg_size << std::endl;
    }
    collect_spot_thread_metrics(state, &active_received, &latency);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] metrics merge done ts_ns="
                  << perf_multi_metric::now_ns()
                  << " size=" << msg_size
                  << " received=" << active_received << std::endl;
    }
    if (state->fatal.load(std::memory_order_acquire)
        || active_received == 0 || latency.mean_ns <= 0.0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] metrics invalid fatal="
                      << state->fatal.load(std::memory_order_acquire)
                      << " received=" << active_received
                      << " latency_mean=" << latency.mean_ns << std::endl;
        }
        return false;
    }
    throughput =
      static_cast<double>(active_received)
      / static_cast<double>(std::max(1, settings.duration_seconds));

    print_client_result_lines(k_pattern, lib_name, transport, msg_size,
                              throughput, latency);
    return true;
}

int run_client_benchmark(const std::string &lib_name,
                         const std::string &transport,
                         const std::string &endpoint,
                         const std::string &control_endpoint,
                         size_t fallback_size)
{
    set_perf_multi_pattern_env(k_pattern);

    if (!is_supported_transport(transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }
    if (!transport_available(transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    sync_spot_internal_mesh_pub_hwm(
      bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm));
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes(fallback_size);
    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    spot_client_state_t state;
    g_client_state = &state;
    if (!create_control_spot(ctx, transport, control_endpoint, settings,
                             &state)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-client] create_control_spot failed"
                      << std::endl;
        fast_exit_process(1);
    }
    if (!create_spot_slots(ctx, transport, endpoint, settings, &state,
                           &state.slots)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] create_spot_slots failed"
                      << " mode=recv"
                      << std::endl;
        }
        fast_exit_process(1);
    }
    // Keep control stdin handling dormant until the recv-side setup is ready.
    // The runner can forward CONTROL_CONNECTED as soon as the client publishes
    // its endpoint, and deferring the watcher avoids setup-time races without
    // changing the recv-only benchmark contract.
    perf_multi_spot_control::start_client_stdin_watcher(
      &state,
      [](spot_client_state_t *client_state, size_t start_size) {
          perf_multi_handshake::signal_start(
            &client_state->start_gate, start_size);
      });

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (!run_single_size_case(&state,
                                  settings,
                                  lib_name,
                                  transport,
                                  msg_sizes[i],
                                  static_cast<uint32_t>(i + 1))) {
            fast_exit_process(1);
        }
    }

    fast_exit_process(0);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t>(std::strtoull(argv[3], NULL, 10));

    std::string endpoint;
    std::string control_endpoint;
    if (!parse_endpoint_arg(argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }
    if (!parse_control_endpoint_arg(argc, argv, &control_endpoint)) {
        std::cerr << "missing --control-endpoint" << std::endl;
        return 1;
    }

    return run_client_benchmark(lib_name, transport, endpoint, control_endpoint,
                                fallback_size);
}
