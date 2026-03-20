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

static const char *k_pattern = "SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const size_t k_topic_len = sizeof("bench") - 1;
static const uint32_t k_metric_run_id = 1U;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::print_client_result_lines;
using perf_multi_client::resolve_case_msg_sizes;

enum spot_recv_mode_t
{
    spot_recv_callback = 0,
    spot_recv_slot_threads = 1,
    spot_recv_worker_threads = 2
};

struct spot_client_slot_t
{
    spot_client_slot_t() :
        node(NULL),
        monitor(NULL),
        ready(false),
        error_code(0),
        stop(false)
    {
    }

    void *node;
    void *monitor;
    std::mutex ready_mutex;
    std::condition_variable ready_cv;
    bool ready;
    int error_code;
    std::atomic<bool> stop;
    std::thread recv_thread;
};

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
        metrics_epoch(1)
    {
    }

    std::vector<spot_client_slot_t *> slots;
    std::mutex mutex;
    std::condition_variable cv;
    std::mutex metrics_mutex;
    std::vector<spot_thread_metrics_t *> thread_metrics;
    std::atomic<size_t> expected_msg_size;
    std::atomic<bool> collect_active;
    std::atomic<bool> fatal;
    std::atomic<size_t> seen_msg_size;
    std::atomic<int> seen_phase;
    std::atomic<bool> recv_workers_stop;
    std::atomic<uint64_t> metrics_epoch;
    std::vector<std::thread> recv_workers;
};

spot_client_state_t *g_client_state = NULL;

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

void close_parts(zlink_msg_t *parts, size_t part_count)
{
    if (!parts)
        return;
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close(&parts[i]);
}

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

bool should_sample_spot_latency(unsigned long long sample_index)
{
    static const unsigned int stride = resolve_spot_latency_sample_stride();
    return stride <= 1 || sample_index == 1
           || (sample_index % static_cast<unsigned long long>(stride)) == 0;
}

size_t resolve_spot_recv_thread_max_msg_size()
{
    return static_cast<size_t>(
      resolve_multi_int_env("PERF_MULTI_SPOT_RECV_THREAD_MAX_MSG_SIZE", 256, 0));
}

size_t resolve_spot_recv_worker_min_msg_size()
{
    return static_cast<size_t>(resolve_multi_int_env(
      "PERF_MULTI_SPOT_RECV_WORKER_MIN_MSG_SIZE", 0, 0));
}

size_t resolve_spot_recv_worker_count(size_t slot_count)
{
    const size_t configured = static_cast<size_t>(
      resolve_multi_int_env("PERF_MULTI_SPOT_RECV_WORKERS", 0, 0));
    if (configured > 0)
        return std::min(slot_count, configured);

    const unsigned int hw_threads = std::thread::hardware_concurrency();
    const size_t auto_workers =
      hw_threads > 0
        ? std::max<size_t>(
            4, std::min<size_t>(8, static_cast<size_t>(hw_threads)))
        : size_t(4);
    return std::min(slot_count, auto_workers);
}

spot_recv_mode_t resolve_spot_recv_mode(size_t msg_size)
{
    static const size_t slot_thread_max_msg_size =
      resolve_spot_recv_thread_max_msg_size();
    static const size_t worker_min_msg_size =
      resolve_spot_recv_worker_min_msg_size();

    if (slot_thread_max_msg_size > 0 && msg_size <= slot_thread_max_msg_size)
        return spot_recv_slot_threads;
    if (worker_min_msg_size > 0 && msg_size >= worker_min_msg_size)
        return spot_recv_worker_threads;
    return spot_recv_callback;
}

spot_recv_mode_t resolve_spot_run_recv_mode(const std::vector<size_t> &msg_sizes)
{
    size_t max_msg_size = 0;
    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];
    }

    return resolve_spot_recv_mode(max_msg_size);
}

const char *spot_recv_mode_name(spot_recv_mode_t mode)
{
    switch (mode) {
        case spot_recv_slot_threads:
            return "recv-thread";
        case spot_recv_worker_threads:
            return "recv-worker";
        case spot_recv_callback:
        default:
            return "callback";
    }
}

bool configure_spot_tls_client(void *node, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    static const std::string ca_path =
      write_temp_cert(test_certs::ca_cert_pem, "multi_spot_ca");
    return zlink_set_tls_client(node, ca_path.c_str(), "localhost", 0)
           == 0;
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

    zlink_service_monitor_open_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.events =
      ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED | ZLINK_MONITOR_EVENT_ERROR;
    void *monitor = zlink_service_monitor_open(slot->node, &opts);
    if (!monitor)
        return false;

    const int monitor_hwm = bench_hwm_from_env("PERF_MONITOR_HWM", 1000);
    set_sockopt_int(monitor, ZLINK_OPT_LINGER, 0, "ZLINK_OPT_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int(monitor, ZLINK_OPT_SNDHWM, monitor_hwm,
                        "ZLINK_OPT_SNDHWM");
        set_sockopt_int(monitor, ZLINK_OPT_RCVHWM, monitor_hwm,
                        "ZLINK_OPT_RCVHWM");
    }

    slot->monitor = monitor;
    if (zlink_service_monitor_handler(
          monitor,
          [](const zlink_service_event_t *event, void *userdata) {
              spot_client_slot_t *slot_state =
                static_cast<spot_client_slot_t *> (userdata);
              if (!slot_state || !event)
                  return;
              {
                  std::lock_guard<std::mutex> lock (slot_state->ready_mutex);
                  switch (event->event_type) {
                      case ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED:
                          slot_state->ready = event->value > 0;
                          break;

                      case ZLINK_MONITOR_EVENT_ERROR:
                          if (slot_state->error_code == 0) {
                              slot_state->error_code =
                                event->error_code != 0 ? event->error_code
                                                       : EIO;
                          }
                          break;

                      default:
                          break;
                  }
              }
              slot_state->ready_cv.notify_all ();
          },
          slot)
        != 0) {
        (void) zlink_monitor_close (&monitor);
        slot->monitor = NULL;
        return false;
    }
    return true;
}

bool spot_slot_has_ready_peer(spot_client_slot_t *slot)
{
    if (!slot || !slot->monitor)
        return false;
    std::lock_guard<std::mutex> lock (slot->ready_mutex);
    return slot->error_code == 0 && slot->ready;
}

void close_spot_ready_monitor(spot_client_slot_t *slot)
{
    if (!slot)
        return;

    void *monitor = slot->monitor;
    slot->monitor = NULL;

    if (!monitor)
        return;

    (void) zlink_monitor_close(&monitor);
}

void join_spot_recv_workers(spot_client_state_t *state)
{
    if (!state)
        return;

    for (size_t i = 0; i < state->recv_workers.size(); ++i) {
        if (state->recv_workers[i].joinable())
            state->recv_workers[i].join();
    }
    state->recv_workers.clear();
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

    if (state)
        state->recv_workers_stop.store(true, std::memory_order_release);

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        slot->stop.store(true, std::memory_order_release);
    }

    if (state)
        join_spot_recv_workers(state);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] destroy after worker join ts_us="
                  << perf_multi_metric::now_us() << std::endl;
    }

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        if (slot->recv_thread.joinable())
            slot->recv_thread.join();
    }
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] destroy after recv join ts_us="
                  << perf_multi_metric::now_us() << std::endl;
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
                              size_t part_count)
{
    spot_client_state_t *state = g_client_state;
    if (!state || !topic || topic_len != k_topic_len
        || std::memcmp(topic, k_topic, k_topic_len) != 0 || part_count == 0) {
        close_parts(parts, part_count);
        return;
    }

    perf_multi_metric::header_t header;
    const bool header_ok =
      perf_multi_metric::decode_payload_header(zlink_msg_data(&parts[0]),
                                               zlink_msg_size(&parts[0]),
                                               &header);
    close_parts(parts, part_count);
    if (!header_ok)
        return;

    const size_t previous_msg_size =
      state->seen_msg_size.load(std::memory_order_acquire);
    const int previous_phase =
      state->seen_phase.load(std::memory_order_acquire);
    const bool phase_changed =
      previous_msg_size != header.msg_size || previous_phase != header.phase;
    if (phase_changed) {
        state->seen_msg_size.store(header.msg_size, std::memory_order_release);
        state->seen_phase.store(header.phase, std::memory_order_release);
        if (bench_transition_debug_enabled()
            && previous_msg_size != header.msg_size) {
            std::cerr << "[multi-spot-client] transition recv ts_us="
                      << perf_multi_metric::now_us()
                      << " size=" << header.msg_size
                      << " phase=" << header.phase
                      << " prev_size=" << previous_msg_size
                      << " prev_phase=" << previous_phase << std::endl;
        }
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

    if (phase_changed)
        state->cv.notify_all();
}

void spot_client_sub_handler(const zlink_routing_id_t *,
                             const char *topic,
                             size_t topic_len,
                             zlink_msg_t *parts,
                             size_t part_count,
                             void *)
{
    handle_spot_client_parts(topic, topic_len, parts, part_count);
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
        const int err = errno;
        if (slot->stop.load(std::memory_order_acquire))
            return false;
        if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
            || err == ETIMEDOUT) {
            return true;
        }
        mark_fatal();
        return false;
    }

    if (received)
        *received = true;
    if (topic_len < sizeof(topic))
        topic[topic_len] = '\0';
    handle_spot_client_parts(topic, topic_len, parts, part_count);
    return true;
}

bool wait_all_sub_ready(const std::vector<spot_client_slot_t *> &slots,
                        int timeout_ms);

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

void spot_client_recv_loop(spot_client_slot_t *slot)
{
    if (!slot || !slot->node)
        return;

    while (!slot->stop.load(std::memory_order_acquire)) {
        bool progressed = false;
        if (!drain_spot_client_slot(slot, &progressed))
            return;
        if (!progressed && perf_socket_poll(NULL, 0, 1) < 0
            && zlink_errno() != EINTR)
            return;
    }
}

void spot_client_recv_worker_loop(spot_client_state_t *state,
                                  size_t worker_index,
                                  size_t worker_count)
{
    if (!state || worker_count == 0)
        return;

    while (!state->recv_workers_stop.load(std::memory_order_acquire)) {
        bool progressed = false;

        for (size_t i = worker_index; i < state->slots.size();
             i += worker_count) {
            spot_client_slot_t *slot = state->slots[i];
            if (!slot || slot->stop.load(std::memory_order_acquire))
                continue;
            if (!drain_spot_client_slot(slot, &progressed))
                return;
        }

        if (!progressed)
            std::this_thread::yield();
    }
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

        char service_name[64];
        std::snprintf(service_name, sizeof(service_name), "perf-spot-c%zu", i);
        slot->node = zlink_spot_node_new(ctx.get(), service_name);
        if (!slot->node || !configure_spot_tls_client(slot->node, transport)) {
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
                                        NULL)
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

        if (recv_mode == spot_recv_slot_threads)
            slot->recv_thread = std::thread(spot_client_recv_loop, slot);
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

    if (!wait_all_sub_ready(*slots_out, settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-client] ready wait timeout slots="
                      << slots_out->size() << std::endl;
        return false;
    }

    for (size_t i = 0; i < slots_out->size(); ++i)
        close_spot_ready_monitor((*slots_out)[i]);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] create slots done ts_us="
                  << perf_multi_metric::now_us()
                  << " slots=" << slots_out->size() << std::endl;
    }
    return true;
}

bool start_spot_recv_workers(spot_client_state_t *state, spot_recv_mode_t mode)
{
    if (!state)
        return false;
    if (mode != spot_recv_worker_threads)
        return true;
    if (state->slots.empty())
        return false;

    const size_t worker_count = resolve_spot_recv_worker_count(state->slots.size());
    if (worker_count == 0)
        return false;

    state->recv_workers_stop.store(false, std::memory_order_release);
    for (size_t i = 0; i < worker_count; ++i)
        state->recv_workers.push_back(
          std::thread(spot_client_recv_worker_loop, state, i, worker_count));
    return true;
}

bool wait_all_sub_ready(const std::vector<spot_client_slot_t *> &slots,
                        int timeout_ms)
{
    if (slots.empty())
        return true;

    std::vector<char> ready(slots.size(), 0);
    size_t ready_count = 0;
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] ready wait begin ts_us="
                  << perf_multi_metric::now_us()
                  << " slots=" << slots.size() << std::endl;
    }

    while (std::chrono::steady_clock::now() < deadline
           && ready_count < slots.size()) {
        for (size_t i = 0; i < slots.size(); ++i) {
            if (ready[i])
                continue;
            if (spot_slot_has_ready_peer(slots[i])) {
                ready[i] = 1;
                ++ready_count;
            }
        }

        if (ready_count >= slots.size())
            break;

        for (size_t i = 0; i < slots.size (); ++i) {
            spot_client_slot_t *slot = slots[i];
            if (!slot || ready[i])
                continue;
            std::unique_lock<std::mutex> lock (slot->ready_mutex);
            if (slot->error_code != 0)
                return false;
            (void) slot->ready_cv.wait_for (
              lock, std::chrono::milliseconds (1),
              [slot] () { return slot->error_code != 0 || slot->ready; });
            if (slot->error_code != 0)
                return false;
            if (slot->ready) {
                ready[i] = 1;
                ++ready_count;
            }
        }
    }

    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-client] ready wait done ts_us="
                  << perf_multi_metric::now_us()
                  << " ready=" << ready_count
                  << "/" << slots.size() << std::endl;
    }

    return ready_count == slots.size();
}

void reset_metrics(spot_client_state_t *state, size_t msg_size)
{
    state->expected_msg_size.store(msg_size, std::memory_order_release);
    state->collect_active.store(false, std::memory_order_release);
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
          return state->fatal.load(std::memory_order_acquire)
                 || (state->seen_msg_size.load(std::memory_order_acquire)
                       == msg_size
                     && state->seen_phase.load(std::memory_order_acquire)
                          != static_cast<int>(
                            perf_multi_metric::phase_unknown));
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
    const spot_recv_mode_t recv_mode = resolve_spot_run_recv_mode(msg_sizes);
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

    if (!start_spot_recv_workers(&state, recv_mode)) {
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
