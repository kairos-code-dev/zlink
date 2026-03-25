#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

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
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const size_t k_topic_len = sizeof("bench") - 1;
static const uint32_t k_metric_run_id = 1U;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::print_client_result_lines;
using perf_multi_client::resolve_case_msg_sizes;

enum spot_recv_mode_t
{
    spot_recv_recv = 0,
    spot_recv_callback = 1
};

struct spot_client_slot_t
{
    spot_client_slot_t() :
        node(NULL),
        index(0),
        stop(false)
    {
    }

    void *node;
    size_t index;
    ready_monitor_t monitor;
    std::atomic<bool> stop;
};

struct spot_recv_worker_t;
struct spot_thread_metrics_t;

struct spot_client_state_t
{
    spot_client_state_t() :
        expected_msg_size(0),
        collect_active(false),
        fatal(false),
        seen_msg_size(0),
        seen_phase(static_cast<int>(perf_multi_metric::phase_unknown)),
        recv_workers_stop(false),
        metrics_epoch(1),
        use_slot_barrier(false),
        required_slot_count(0)
    {
    }

    std::vector<spot_client_slot_t *> slots;
    std::vector<spot_recv_worker_t *> recv_workers;
    std::mutex mutex;
    std::condition_variable cv;
    std::mutex metrics_mutex;
    std::vector<spot_thread_metrics_t *> thread_metrics;
    std::vector<size_t> slot_seen_msg_sizes;
    std::vector<int> slot_seen_phases;
    std::atomic<size_t> expected_msg_size;
    std::atomic<bool> collect_active;
    std::atomic<bool> fatal;
    std::atomic<size_t> seen_msg_size;
    std::atomic<int> seen_phase;
    std::atomic<bool> recv_workers_stop;
    std::atomic<uint64_t> metrics_epoch;
    bool use_slot_barrier;
    size_t required_slot_count;
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
    if (metrics.epoch != epoch)
        reset_spot_thread_metrics(&metrics, epoch);
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
            if (!metrics || metrics->owner != state || metrics->epoch != epoch)
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

int resolve_spot_connect_ready_timeout_ms(const std::string &transport,
                                          int base_timeout_ms)
{
    int timeout_ms = std::max(1, base_timeout_ms);
    if (transport == "wss") {
        timeout_ms = std::max(timeout_ms, 20000);
    } else if (transport == "tls") {
        timeout_ms = std::max(timeout_ms, 10000);
    }

    return resolve_multi_int_env("PERF_MULTI_SPOT_CONNECT_READY_TIMEOUT_MS",
                                 timeout_ms,
                                 1);
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

    return std::min(slot_count, size_t(4));
}

int resolve_spot_phase_quorum_percent(const std::string &transport)
{
    int default_percent = 90;
    if (transport == "wss")
        default_percent = 75;
    else if (transport == "ws")
        default_percent = 85;

    return resolve_multi_int_env("PERF_MULTI_SPOT_PHASE_QUORUM_PERCENT",
                                 default_percent,
                                 1);
}

size_t resolve_required_slot_count(size_t slot_count,
                                   const std::string &transport)
{
    if (slot_count == 0)
        return 0;

    const int quorum_percent =
      std::min(100, resolve_spot_phase_quorum_percent(transport));
    const size_t required =
      static_cast<size_t>(
        (static_cast<unsigned long long>(slot_count) * quorum_percent + 99ULL)
        / 100ULL);
    return std::max<size_t>(1, required);
}

bool should_use_spot_slot_barrier(size_t slot_count)
{
    return slot_count > 0 && slot_count <= 16;
}

spot_recv_mode_t resolve_spot_run_recv_mode()
{
    return multi_perf_callback_mode()
             ? spot_recv_callback
             : spot_recv_recv;
}

const char *spot_recv_mode_name(spot_recv_mode_t mode)
{
    switch (mode) {
        case spot_recv_callback:
            return "callback";
        case spot_recv_recv:
        default:
            return "recv";
    }
}

bool apply_spot_sub_options(void *sub, const multi_bench_settings_t &settings)
{
    const int linger_ms = 0;
    const int rcvhwm = bench_hwm_from_env("PERF_MULTI_RCVHWM", settings.hwm);
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_RCVTIMEO_MS", 200);
    const int sndbuf = bench_socket_buffer_bytes_from_env("PERF_SNDBUF", -1);
    const int rcvbuf = bench_socket_buffer_bytes_from_env("PERF_RCVBUF", -1);

    if (zlink_set_option(sub, ZLINK_OPT_LINGER, &linger_ms,
                             sizeof(linger_ms))
          != 0
        || zlink_set_option(sub, ZLINK_OPT_RCVHWM, &rcvhwm,
                                sizeof(rcvhwm))
             != 0
        || zlink_set_option(sub, ZLINK_OPT_RCVTIMEO, &rcvtimeo_ms,
                                sizeof(rcvtimeo_ms))
             != 0) {
        return false;
    }

    if (sndbuf > 0
        && zlink_set_option(sub, ZLINK_OPT_SNDBUF, &sndbuf,
                                sizeof(sndbuf))
             != 0) {
        return false;
    }

    if (rcvbuf > 0
        && zlink_set_option(sub, ZLINK_OPT_RCVBUF, &rcvbuf,
                                sizeof(rcvbuf))
             != 0) {
        return false;
    }

    return true;
}

bool open_spot_ready_monitor(spot_client_slot_t *slot)
{
    if (!slot || !slot->node)
        return false;

    if (!open_configured_service_monitor(
          slot->node,
          ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR,
          &slot->monitor)) {
        return false;
    }
    return true;
}

void close_spot_ready_monitor(spot_client_slot_t *slot)
{
    if (!slot)
        return;

    close_ready_monitor(slot->monitor);
}

void destroy_spot_slots(spot_client_state_t *state,
                        std::vector<spot_client_slot_t *> *slots)
{
    if (!slots)
        return;

    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] destroy start ts_us="
                  << perf_multi_metric::now_us()
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
                      << " ts_us=" << perf_multi_metric::now_us()
                      << " node=" << (slot->node ? 1 : 0) << std::endl;
        }
        close_spot_ready_monitor(slot);
        if (slot->node) {
            zlink_spot_node_destroy(&slot->node);
            if (bench_transition_debug_enabled() && i < 4) {
                std::cerr << "[multi-spot-client] destroy slot after node index="
                          << i << " ts_us=" << perf_multi_metric::now_us()
                          << std::endl;
            }
        }
        delete slot;
    }

    slots->clear();
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] destroy done ts_us="
                  << perf_multi_metric::now_us() << std::endl;
    }
}

void handle_spot_client_parts(const char *topic,
                              size_t topic_len,
                              zlink_msg_t *parts,
                              size_t part_count,
                              spot_client_slot_t *slot)
{
    spot_client_state_t *state = g_client_state;
    if (!state || !topic || topic_len != k_topic_len
        || std::memcmp(topic, k_topic, k_topic_len) != 0 || part_count == 0) {
        perf_close_multipart(parts, part_count);
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
    bool slot_changed = false;
    size_t previous_slot_msg_size = 0;
    int previous_slot_phase = static_cast<int>(perf_multi_metric::phase_unknown);
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
        if (slot && slot->index < state->slot_seen_msg_sizes.size()) {
            previous_slot_msg_size = state->slot_seen_msg_sizes[slot->index];
            previous_slot_phase = state->slot_seen_phases[slot->index];
            const bool slot_phase_regressed =
              previous_slot_msg_size == header.msg_size
              && previous_slot_phase > header_phase;
            slot_changed =
              !slot_phase_regressed
              && (previous_slot_msg_size != header.msg_size
                  || previous_slot_phase != header_phase);
            if (!slot_phase_regressed) {
                state->slot_seen_msg_sizes[slot->index] = header.msg_size;
                state->slot_seen_phases[slot->index] = header_phase;
            }
        }
    }
    if (phase_changed && bench_transition_debug_enabled()
        && previous_msg_size != header.msg_size) {
        std::cerr << "[multi-spot-client] transition recv ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << header.msg_size
                  << " phase=" << header.phase
                  << " prev_size=" << previous_msg_size
                  << " prev_phase=" << previous_phase << std::endl;
    }
    const bool collect_active =
      state->collect_active.load(std::memory_order_acquire);

    if (bench_debug_enabled() && previous_msg_size == 0) {
        std::cerr << "[multi-spot-client] first recv size="
                  << header.msg_size << " phase=" << header.phase
                  << " run=" << header.run_id << std::endl;
    }

    if (collect_active
        && header.magic == perf_multi_metric::k_magic
        && header.run_id == k_metric_run_id
        && header.phase
             == static_cast<uint32_t> (perf_multi_metric::phase_active)
        && header.msg_size
             == state->expected_msg_size.load(std::memory_order_acquire)) {
        spot_thread_metrics_t *metrics = bind_spot_thread_metrics(state);
        if (metrics)
            ++metrics->active_received;
        if (metrics && should_sample_spot_latency(++metrics->sample_index)) {
            const uint64_t now_us = perf_multi_metric::now_us();
            const double latency_us =
              header.sent_ts_us > 0 && now_us >= header.sent_ts_us
                ? static_cast<double>(now_us - header.sent_ts_us)
                : 0.0;
            metrics->latency.add(latency_us);
        }
    }

    if (phase_changed || slot_changed)
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
    if (!slot || !slot->node)
        return false;

    if (received)
        *received = false;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof(topic) - 1;
    const int rc = zlink_subscribe(
      slot->node, &parts, &part_count, flags, topic, &topic_len);
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

size_t count_slots_observed_size(const spot_client_state_t *state,
                                 size_t msg_size)
{
    if (!state || state->slot_seen_msg_sizes.empty())
        return 0;

    size_t count = 0;
    for (size_t i = 0; i < state->slot_seen_msg_sizes.size(); ++i) {
        if (state->slot_seen_msg_sizes[i] == msg_size
            && state->slot_seen_phases[i]
                 != static_cast<int>(perf_multi_metric::phase_unknown)) {
            ++count;
        }
    }
    return count;
}

size_t count_slots_observed_phase(const spot_client_state_t *state,
                                  size_t msg_size,
                                  perf_multi_metric::phase_t phase)
{
    if (!state || state->slot_seen_msg_sizes.empty())
        return 0;

    const int expected_phase = static_cast<int>(phase);
    size_t count = 0;
    for (size_t i = 0; i < state->slot_seen_msg_sizes.size(); ++i) {
        if (state->slot_seen_msg_sizes[i] == msg_size
            && state->slot_seen_phases[i] == expected_phase) {
            ++count;
        }
    }
    return count;
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
            return true;
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
        if (!slot || !worker || !worker->poller
            || zlink_poller_add(worker->poller, slot->node, slot, ZLINK_POLLIN)
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

bool create_spot_slots(ctx_guard_t &ctx,
                       const std::string &transport,
                       const std::string &endpoint,
                       const multi_bench_settings_t &settings,
                       spot_recv_mode_t recv_mode,
                       spot_client_state_t *state,
                       std::vector<spot_client_slot_t *> *slots_out)
{
    if (!state || !slots_out)
        return false;

    const size_t service_clients =
      resolve_multi_service_clients(settings.clients);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] create slots begin ts_us="
                  << perf_multi_metric::now_us()
                  << " slots=" << service_clients
                  << " mode=" << spot_recv_mode_name(recv_mode) << std::endl;
    }
    for (size_t i = 0; i < service_clients; ++i) {
        spot_client_slot_t *slot = new (std::nothrow) spot_client_slot_t();
        if (!slot)
            return false;
        slot->index = i;

        char service_name[64];
        std::snprintf(service_name, sizeof(service_name), "perf-spot-c%zu", i);
        slot->node = zlink_spot_node_new(ctx.get(), service_name);
        if (!slot->node || !setup_tls_client(slot->node, transport)) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-client] node create/tls failed slot="
                          << i << " err=" << zlink_errno() << std::endl;
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        if ((recv_mode == spot_recv_callback
             && zlink_subscribe_handler(slot->node, &spot_client_sub_handler,
                                        slot)
                  != 0)
            || !apply_spot_sub_options(slot->node, settings)
            || !open_spot_ready_monitor(slot)
            || zlink_spot_node_connect_peer(slot->node, endpoint.c_str()) != 0
            || zlink_set_subscription (slot->node, k_topic)
                 != 0) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-client] slot create failed slot=" << i
                          << " node=" << (slot->node != NULL)
                          << " err=" << zlink_errno() << std::endl;
            close_spot_ready_monitor(slot);
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        slots_out->push_back(slot);
        if (bench_transition_debug_enabled()
            && (((i + 1) % 25) == 0 || (i + 1) == service_clients)) {
            std::cerr << "[multi-spot-client] create slots progress ts_us="
                      << perf_multi_metric::now_us()
                      << " ready_slots=" << (i + 1)
                      << "/" << service_clients << std::endl;
        }
    }

    if (slots_out->empty())
        return false;
    state->slot_seen_msg_sizes.assign(slots_out->size(), 0);
    state->slot_seen_phases.assign(
      slots_out->size(),
      static_cast<int>(perf_multi_metric::phase_unknown));
    state->use_slot_barrier =
      recv_mode == spot_recv_recv && should_use_spot_slot_barrier(slots_out->size());
    state->required_slot_count =
      state->use_slot_barrier
        ? resolve_required_slot_count(slots_out->size(), transport)
        : 0;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(resolve_spot_connect_ready_timeout_ms(
        transport, settings.connect_ready_timeout_ms));
    for (size_t i = 0; i < slots_out->size(); ++i) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-client] ready wait deadline expired"
                          << " slot=" << i << std::endl;
            }
            return false;
        }

        const int remaining_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now)
            .count());
        if (!wait_for_service_monitor_event(
              (*slots_out)[i]->monitor,
              ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED,
              ZLINK_MONITOR_EVENT_ERROR,
              remaining_ms)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-client] ready wait failed slot=" << i
                          << " err=" << zlink_errno() << std::endl;
            }
            return false;
        }
        close_spot_ready_monitor((*slots_out)[i]);
    }

    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] create slots done ts_us="
                  << perf_multi_metric::now_us()
                  << " slots=" << slots_out->size() << std::endl;
    }
    if (recv_mode == spot_recv_recv && !start_spot_recv_workers(state))
        return false;
    return true;
}

void reset_metrics(spot_client_state_t *state, size_t msg_size)
{
    state->expected_msg_size.store(msg_size, std::memory_order_release);
    state->collect_active.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->seen_msg_size.store(0, std::memory_order_relaxed);
        state->seen_phase.store(
          static_cast<int>(perf_multi_metric::phase_unknown),
          std::memory_order_relaxed);
        std::fill(state->slot_seen_msg_sizes.begin(),
                  state->slot_seen_msg_sizes.end(),
                  0);
        std::fill(state->slot_seen_phases.begin(),
                  state->slot_seen_phases.end(),
                  static_cast<int>(perf_multi_metric::phase_unknown));
    }
    state->metrics_epoch.fetch_add(1, std::memory_order_acq_rel);
}

bool wait_msg_size_start(spot_client_state_t *state,
                         size_t msg_size,
                         int timeout_ms)
{
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->cv.wait_for(
      lock,
      std::chrono::milliseconds(std::max(1, timeout_ms)),
      [state, msg_size]() {
          if (!state->use_slot_barrier) {
              return state->fatal.load(std::memory_order_acquire)
                     || (state->seen_msg_size.load(std::memory_order_acquire)
                           == msg_size
                         && state->seen_phase.load(std::memory_order_acquire)
                              != static_cast<int>(
                                perf_multi_metric::phase_unknown));
          }
          const bool active_seen =
            state->seen_msg_size.load(std::memory_order_acquire) == msg_size
            && state->seen_phase.load(std::memory_order_acquire)
                 == static_cast<int>(perf_multi_metric::phase_active);
          return state->fatal.load(std::memory_order_acquire)
                 || active_seen
                 || count_slots_observed_size(state, msg_size)
                      >= state->required_slot_count;
      });
}

bool wait_phase_start(spot_client_state_t *state,
                      size_t msg_size,
                      perf_multi_metric::phase_t expected_phase,
                      int timeout_ms)
{
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->cv.wait_for(
      lock,
      std::chrono::milliseconds(std::max(1, timeout_ms)),
      [state, msg_size, expected_phase]() {
          return state->fatal.load(std::memory_order_acquire)
                 || (state->seen_msg_size.load(std::memory_order_acquire)
                       == msg_size
                     && state->seen_phase.load(std::memory_order_acquire)
                          == static_cast<int>(expected_phase));
      });
}

int resolve_spot_phase_timeout_ms(const multi_bench_settings_t &settings,
                                  size_t msg_size)
{
    int timeout_ms =
      std::max(settings.connect_ready_timeout_ms,
               std::max(1, settings.duration_seconds) * 5000);
    if (msg_size >= 131072) {
        timeout_ms =
          std::max(timeout_ms,
                   std::max(30000, settings.connect_ready_timeout_ms * 6));
    }

    return resolve_multi_int_env("PERF_MULTI_SPOT_PHASE_TIMEOUT_MS",
                                 timeout_ms,
                                 1);
}

bool wait_phase_duration(spot_client_state_t *state, double seconds)
{
    if (seconds <= 0.0)
        return true;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(seconds));

    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->fatal.load(std::memory_order_acquire)) {
        if (state->cv.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }

    return !state->fatal.load(std::memory_order_acquire);
}

bool run_single_size_case(spot_client_state_t *state,
                          const multi_bench_settings_t &settings,
                          const std::string &lib_name,
                          const std::string &transport,
                          size_t msg_size)
{
    const int phase_timeout_ms =
      resolve_spot_phase_timeout_ms(settings, msg_size);

    std::cout << "CLIENT_READY," << msg_size << std::endl;

    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] size wait start ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " timeout_ms=" << phase_timeout_ms << std::endl;
    }

    reset_metrics(state, msg_size);
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
        std::cerr << "[multi-spot-client] collect start ts_us="
                  << perf_multi_metric::now_us()
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

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();
    if (!wait_phase_duration(
          state, static_cast<double>(std::max(1, settings.duration_seconds)))) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-client] active wait failed size="
                      << msg_size << std::endl;
        return false;
    }
    state->collect_active.store(false, std::memory_order_release);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] collect done ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size << std::endl;
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    bench_latency_stats_t latency;
    double throughput = 0.0;

    unsigned long long active_received = 0;
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] metrics merge start ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size << std::endl;
    }
    collect_spot_thread_metrics(state, &active_received, &latency);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] metrics merge done ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " received=" << active_received << std::endl;
    }
    if (state->fatal.load(std::memory_order_acquire)
        || active_received == 0 || latency.mean_us <= 0.0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] metrics invalid fatal="
                      << state->fatal.load(std::memory_order_acquire)
                      << " received=" << active_received
                      << " latency_mean=" << latency.mean_us << std::endl;
        }
        return false;
    }
    throughput =
      static_cast<double>(active_received)
      / static_cast<double>(std::max(1, settings.duration_seconds));

    print_client_result_lines(k_pattern, lib_name, transport, msg_size,
                              throughput, latency, metrics);
    return true;
}

int run_client_benchmark(const std::string &lib_name,
                         const std::string &transport,
                         const std::string &endpoint,
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
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes(fallback_size);
    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    spot_client_state_t state;
    g_client_state = &state;
    const spot_recv_mode_t recv_mode = resolve_spot_run_recv_mode();
    if (!create_spot_slots(ctx, transport, endpoint, settings, recv_mode,
                           &state,
                           &state.slots)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] create_spot_slots failed"
                      << " mode=" << spot_recv_mode_name(recv_mode)
                      << std::endl;
        }
        fast_exit_process(1);
    }

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (!run_single_size_case(&state, settings, lib_name, transport,
                                  msg_sizes[i])) {
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
    if (!parse_endpoint_arg(argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    return run_client_benchmark(lib_name, transport, endpoint, fallback_size);
}
