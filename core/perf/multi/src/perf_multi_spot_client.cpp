#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../common/perf_multi_spot_handle.hpp"
#include "../../common/perf_tls_setup.hpp"
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
        handle(NULL),
        control_pub(NULL),
        index(0),
        stop(false),
        phase_trace_msg_size(0),
        first_warmup_us(0),
        first_active_us(0)
    {
    }

    void *node;
    void *handle;
    void *control_pub;
    std::string endpoint;
    size_t index;
    std::atomic<bool> stop;
    std::atomic<size_t> phase_trace_msg_size;
    std::atomic<uint64_t> first_warmup_us;
    std::atomic<uint64_t> first_active_us;
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
        collect_active(false),
        fatal(false),
        ready_barrier_settled(false),
        control_connected(false),
        control_link_ready(false),
        control_started_msg_size(0),
        control_start_recv_us(0),
        seen_msg_size(0),
        seen_phase(static_cast<int>(perf_multi_metric::phase_unknown)),
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
    std::mutex start_wait_mutex;
    std::condition_variable start_wait_cv;
    std::mutex metrics_mutex;
    std::set<size_t> pending_start_sizes;
    std::vector<spot_thread_metrics_t *> thread_metrics;
    std::atomic<size_t> expected_msg_size;
    std::atomic<bool> collect_active;
    std::atomic<bool> fatal;
    std::atomic<bool> ready_barrier_settled;
    std::atomic<bool> control_connected;
    std::atomic<bool> control_link_ready;
    std::atomic<size_t> control_started_msg_size;
    std::atomic<uint64_t> control_start_recv_us;
    std::atomic<size_t> seen_msg_size;
    std::atomic<int> seen_phase;
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

int resolve_spot_ready_settle_ms()
{
    return resolve_multi_int_env("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000, 0);
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
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200);
    const int rcvhwm = bench_hwm_from_env("PERF_MULTI_RCVHWM", settings.hwm);
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_RCVTIMEO_MS", 200);
    const int sndbuf = bench_socket_buffer_bytes_from_env("PERF_SNDBUF", -1);
    const int rcvbuf = bench_socket_buffer_bytes_from_env("PERF_RCVBUF", -1);

    if (zlink_set_option(sub, ZLINK_OPT_LINGER, &linger_ms,
                             sizeof(linger_ms))
          != 0
        || zlink_set_option(sub, ZLINK_OPT_SNDTIMEO, &sndtimeo_ms,
                                sizeof(sndtimeo_ms))
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

bool apply_spot_control_options(void *pub,
                                void *sub,
                                const multi_bench_settings_t &settings)
{
    if (!pub || !sub)
        return false;

    const int linger_ms = 0;
    const int timeout_ms = std::max(1000, settings.connect_ready_timeout_ms);
    const int hwm = std::max<int>(1024, static_cast<int>(settings.clients * 8));
    const int nodrop = 1;

    if (zlink_set_option(pub, ZLINK_OPT_LINGER, &linger_ms,
                         sizeof(linger_ms))
          != 0
        || zlink_set_option(pub, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm)) != 0
        || zlink_set_option(pub, ZLINK_OPT_SNDTIMEO, &timeout_ms,
                            sizeof(timeout_ms))
             != 0
        || zlink_set_pub_option(pub, ZLINK_PUB_OPT_NODROP, &nodrop,
                                sizeof(nodrop))
             != 0
        || zlink_set_option(sub, ZLINK_OPT_LINGER, &linger_ms,
                            sizeof(linger_ms))
             != 0
        || zlink_set_option(sub, ZLINK_OPT_RCVHWM, &hwm, sizeof(hwm)) != 0
        || zlink_set_option(sub, ZLINK_OPT_RCVTIMEO, &timeout_ms,
                            sizeof(timeout_ms))
             != 0) {
        return false;
    }

    return true;
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

bool parse_control_start(const void *data, size_t size, size_t *msg_size_out)
{
    static const char prefix[] = "START,";
    if (!data || !msg_size_out
        || size < (sizeof(prefix) - 1)
        || std::memcmp(data, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }

    std::string line(static_cast<const char *>(data), size);
    char *end = NULL;
    const unsigned long long parsed =
      std::strtoull(line.c_str() + (sizeof(prefix) - 1), &end, 10);
    if (!end || *end != '\0' || parsed == 0)
        return false;

    *msg_size_out = static_cast<size_t>(parsed);
    return true;
}

bool parse_control_connected(const void *data, size_t size)
{
    static const char payload[] = "CONNECTED";
    return data && size == (sizeof(payload) - 1)
           && std::memcmp(data, payload, sizeof(payload) - 1) == 0;
}

bool parse_control_connected_line(const std::string &line)
{
    static const char prefix[] = "CONTROL_CONNECTED,";
    return line.compare(0, sizeof(prefix) - 1, prefix) == 0;
}

void notify_size_start(spot_client_state_t *state, size_t msg_size)
{
    if (!state || msg_size == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(state->start_wait_mutex);
        state->pending_start_sizes.insert(msg_size);
    }
    state->start_wait_cv.notify_all();
}

bool wait_for_size_start_signal(spot_client_state_t *state,
                                size_t msg_size,
                                int timeout_ms)
{
    if (!state || msg_size == 0)
        return false;

    std::unique_lock<std::mutex> lock(state->start_wait_mutex);
    if (state->pending_start_sizes.erase(msg_size) != 0)
        return true;

    const bool signaled = state->start_wait_cv.wait_for(
      lock,
      std::chrono::milliseconds(std::max(1, timeout_ms)),
      [state, msg_size]() {
          return state->fatal.load(std::memory_order_acquire)
                 || state->pending_start_sizes.count(msg_size) != 0;
      });
    return signaled && state->pending_start_sizes.erase(msg_size) != 0;
}

bool publish_control_ready(spot_client_slot_t *slot,
                           size_t msg_size,
                           int /*send_ready_timeout_ms*/)
{
    (void) slot;
    (void) msg_size;
    errno = EINVAL;
    return false;
}

bool publish_control_ready(spot_client_state_t *state,
                           size_t slot_index,
                           size_t msg_size)
{
    if (!state || !state->control_pub || msg_size == 0) {
        errno = EINVAL;
        return false;
    }

    char payload[128];
    const int payload_len = std::snprintf(payload,
                                          sizeof(payload),
                                          "READY,%lu,%lu",
                                          static_cast<unsigned long>(msg_size),
                                          static_cast<unsigned long>(slot_index));
    if (payload_len <= 0
        || static_cast<size_t>(payload_len) >= sizeof(payload)) {
        errno = EMSGSIZE;
        return false;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size(&part, static_cast<size_t>(payload_len)) != 0)
        return false;

    std::memcpy(zlink_msg_data(&part), payload, static_cast<size_t>(payload_len));
    const int rc = zlink_publish(state->control_pub, k_topic, &part, 1, 0);
    const int saved_errno = rc == 0 ? 0 : errno;
    (void) zlink_msg_close(&part);
    if (rc == 0 && bench_transition_debug_enabled()) {
        zlink_spot_node_status_t status;
        std::memset(&status, 0, sizeof(status));
        std::cerr << "[multi-spot-client] ready publish ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " slot=" << slot_index;
        if (state->control_node
            && zlink_spot_node_status_snapshot(state->control_node, &status)
                 == 0) {
            std::cerr << " active=" << status.active_peer_count
                      << " connected=" << status.connected_peer_count
                      << " ready_subjects=" << status.ready_subject_count;
        }
        std::cerr << std::endl;
    }
    if (rc == 0)
        return true;

    errno = saved_errno;
    return false;
}

bool publish_control_ready_count(spot_client_state_t *state,
                                 size_t msg_size,
                                 size_t ready_count)
{
    if (!state || !state->control_pub || msg_size == 0 || ready_count == 0) {
        errno = EINVAL;
        return false;
    }

    char payload[128];
    const int payload_len = std::snprintf(payload,
                                          sizeof(payload),
                                          "READY_COUNT,%lu,%lu",
                                          static_cast<unsigned long>(msg_size),
                                          static_cast<unsigned long>(ready_count));
    if (payload_len <= 0
        || static_cast<size_t>(payload_len) >= sizeof(payload)) {
        errno = EMSGSIZE;
        return false;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size(&part, static_cast<size_t>(payload_len)) != 0)
        return false;
    std::memcpy(zlink_msg_data(&part), payload, static_cast<size_t>(payload_len));
    const int rc = zlink_publish(state->control_pub, k_topic, &part, 1, 0);
    const int saved_errno = rc == 0 ? 0 : errno;
    (void) zlink_msg_close(&part);
    if (rc == 0)
        return true;
    errno = saved_errno;
    return false;
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
        if (slot->control_pub)
            perf_destroy_default_spot_handle(&slot->control_pub);
        if (slot->handle)
            perf_destroy_default_spot_handle(&slot->handle);
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
    if (state->control_pub)
        perf_destroy_default_spot_handle(&state->control_pub);
    if (state->control_sub)
        perf_destroy_default_spot_handle(&state->control_sub);
    if (state->control_node)
        zlink_spot_node_destroy(&state->control_node);
    state->control_connected.store(false, std::memory_order_release);
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
    if (parse_control_start(zlink_msg_data(&parts[0]),
                            zlink_msg_size(&parts[0]),
                            &started_size)) {
        const uint64_t start_recv_us =
          bench_transition_debug_enabled() ? perf_multi_metric::now_us() : 0;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->control_started_msg_size.store(started_size,
                                                  std::memory_order_relaxed);
            if (start_recv_us != 0)
                state->control_start_recv_us.store(start_recv_us,
                                                   std::memory_order_relaxed);
            state->seen_msg_size.store(started_size,
                                       std::memory_order_relaxed);
            state->seen_phase.store(
              static_cast<int>(perf_multi_metric::phase_warmup),
              std::memory_order_relaxed);
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-client] start recv ts_us="
                      << perf_multi_metric::now_us()
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
        std::cerr << "[multi-spot-client] transition recv ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << header.msg_size
                  << " phase=" << header.phase
                  << " prev_size=" << previous_msg_size
                  << " prev_phase=" << previous_phase << std::endl;
    }
    const bool collect_active =
      state->collect_active.load(std::memory_order_acquire);
    const bool trace_phases = bench_transition_debug_enabled();
    const uint64_t recv_ts_us =
      trace_phases ? perf_multi_metric::now_us() : 0;

    if (trace_phases && slot
        && header.msg_size
             == state->expected_msg_size.load(std::memory_order_acquire)) {
        if (header.phase
              == static_cast<uint32_t>(perf_multi_metric::phase_warmup)
            && slot->first_warmup_us.load(std::memory_order_acquire) == 0) {
            uint64_t expected = 0;
            (void) slot->first_warmup_us.compare_exchange_strong(
              expected, recv_ts_us, std::memory_order_acq_rel);
        }
        if (header.phase
              == static_cast<uint32_t>(perf_multi_metric::phase_active)
            && slot->first_active_us.load(std::memory_order_acquire) == 0) {
            uint64_t expected = 0;
            (void) slot->first_active_us.compare_exchange_strong(
              expected, recv_ts_us, std::memory_order_acq_rel);
        }
    }

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
            const uint64_t sample_ts_us =
              trace_phases ? recv_ts_us : perf_multi_metric::now_us();
            const double latency_us =
              header.sent_ts_us > 0 && sample_ts_us >= header.sent_ts_us
                ? static_cast<double>(sample_ts_us - header.sent_ts_us)
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
    if (!state || !state->control_sub)
        return false;

    if (received)
        *received = false;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof(topic) - 1;
    const int rc =
      zlink_subscribe(state->control_sub,
                      &parts,
                      &part_count,
                      ZLINK_DONTWAIT,
                      topic,
                      &topic_len);
    if (rc != 0) {
        const int err = zlink_errno() != 0 ? zlink_errno() : errno;
        if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
            || err == ETIMEDOUT) {
            return true;
        }
        return false;
    }

    if (received)
        *received = true;

    size_t started_size = 0;
    if (topic_len > 0 && topic[topic_len - 1] == '\0')
        --topic_len;
    if (topic_len == k_topic_len
        && std::memcmp(topic, k_topic, k_topic_len) == 0
        && part_count > 0
        && parse_control_connected(zlink_msg_data(&parts[0]),
                                   zlink_msg_size(&parts[0]))) {
        state->control_link_ready.store(true, std::memory_order_release);
        perf_close_multipart(parts, part_count);
        state->cv.notify_all();
        return true;
    }
    if (topic_len == k_topic_len
        && std::memcmp(topic, k_topic, k_topic_len) == 0
        && part_count > 0
        && parse_control_start(zlink_msg_data(&parts[0]),
                               zlink_msg_size(&parts[0]),
                               &started_size)) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->control_started_msg_size.store(started_size,
                                                  std::memory_order_relaxed);
            state->seen_msg_size.store(started_size, std::memory_order_relaxed);
            state->seen_phase.store(
              static_cast<int>(perf_multi_metric::phase_warmup),
              std::memory_order_relaxed);
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-client] control start recv ts_us="
                      << perf_multi_metric::now_us()
                      << " size=" << started_size << std::endl;
        }
        state->cv.notify_all();
    }

    perf_close_multipart(parts, part_count);
    return true;
}

bool wait_for_control_link_ready(spot_client_state_t *state, int timeout_ms)
{
    if (!state)
        return false;
    if (state->control_link_ready.load(std::memory_order_acquire))
        return true;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline) {
        if (state->control_link_ready.load(std::memory_order_acquire))
            return true;
        bool received = false;
        if (!recv_one_control_message(state, &received))
            return false;
        if (state->control_link_ready.load(std::memory_order_acquire))
            return true;
        zlink_pollitem_t item = {NULL, 0, 0, 0};
        if (zlink_poll(&item, 0, 5) < 0 && zlink_errno() != EINTR)
            return false;
    }

    errno = ETIMEDOUT;
    return false;
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
    if (!apply_spot_control_options(state->control_pub, state->control_sub,
                                    settings)) {
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
    if (zlink_publish(state->control_pub, "__warmup__", NULL, 0, 0) != 0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control warmup publish failed err="
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
    if (zlink_set_subscription(state->control_sub, k_topic) != 0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] control subscribe failed err="
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
    if (!state || !state->control_node || state->server_control_endpoint.empty())
        return false;

    if (state->control_connected.load(std::memory_order_acquire))
        return true;

    if (zlink_spot_node_connect_peer(state->control_node,
                                     state->server_control_endpoint.c_str())
        != 0) {
        return false;
    }

    state->control_connected.store(true, std::memory_order_release);
    return true;
}

void disconnect_control_peer(spot_client_state_t *state)
{
    if (!state || !state->control_node || state->server_control_endpoint.empty())
        return;

    (void) zlink_spot_node_disconnect_peer(state->control_node,
                                           state->server_control_endpoint.c_str());
    state->control_connected.store(false, std::memory_order_release);
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

        if ((recv_mode == spot_recv_callback
             && zlink_subscribe_handler(slot->handle, &spot_client_sub_handler,
                                        slot)
                  != 0)
            || !apply_spot_sub_options(slot->handle, settings)
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
            std::cerr << "[multi-spot-client] create slots progress ts_us="
                      << perf_multi_metric::now_us()
                      << " ready_slots=" << (i + 1)
                      << "/" << service_clients << std::endl;
        }
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
    state->control_started_msg_size.store(0, std::memory_order_release);
    state->control_start_recv_us.store(0, std::memory_order_release);
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
        slot->first_warmup_us.store(0, std::memory_order_release);
        slot->first_active_us.store(0, std::memory_order_release);
    }
    state->metrics_epoch.fetch_add(1, std::memory_order_acq_rel);
}

void print_phase_spread_summary(spot_client_state_t *state, size_t msg_size)
{
    if (!state || !bench_transition_debug_enabled())
        return;

    uint64_t warmup_min_us = 0;
    uint64_t warmup_max_us = 0;
    uint64_t active_min_us = 0;
    uint64_t active_max_us = 0;
    size_t warmup_count = 0;
    size_t active_count = 0;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        spot_client_slot_t *slot = state->slots[i];
        if (!slot
            || slot->phase_trace_msg_size.load(std::memory_order_acquire)
                 != msg_size) {
            continue;
        }

        const uint64_t warmup_us =
          slot->first_warmup_us.load(std::memory_order_acquire);
        const uint64_t active_us =
          slot->first_active_us.load(std::memory_order_acquire);

        if (warmup_us != 0) {
            ++warmup_count;
            if (warmup_min_us == 0 || warmup_us < warmup_min_us)
                warmup_min_us = warmup_us;
            if (warmup_us > warmup_max_us)
                warmup_max_us = warmup_us;
        }
        if (active_us != 0) {
            ++active_count;
            if (active_min_us == 0 || active_us < active_min_us)
                active_min_us = active_us;
            if (active_us > active_max_us)
                active_max_us = active_us;
        }
    }

    const uint64_t start_recv_us =
      state->control_start_recv_us.load(std::memory_order_acquire);
    const uint64_t warmup_spread_us =
      warmup_max_us >= warmup_min_us ? warmup_max_us - warmup_min_us : 0;
    const uint64_t active_spread_us =
      active_max_us >= active_min_us ? active_max_us - active_min_us : 0;

    std::cerr << "[multi-spot-client] phase spread size=" << msg_size
              << " start_recv_us=" << start_recv_us
              << " warmup_slots=" << warmup_count
              << " warmup_first_delta_us="
              << ((start_recv_us != 0 && warmup_min_us >= start_recv_us)
                    ? (warmup_min_us - start_recv_us)
                    : 0)
              << " warmup_last_delta_us="
              << ((start_recv_us != 0 && warmup_max_us >= start_recv_us)
                    ? (warmup_max_us - start_recv_us)
                    : 0)
              << " warmup_spread_us=" << warmup_spread_us
              << " active_slots=" << active_count
              << " active_first_delta_us="
              << ((start_recv_us != 0 && active_min_us >= start_recv_us)
                    ? (active_min_us - start_recv_us)
                    : 0)
              << " active_last_delta_us="
              << ((start_recv_us != 0 && active_max_us >= start_recv_us)
                    ? (active_max_us - start_recv_us)
                    : 0)
              << " active_spread_us=" << active_spread_us
              << std::endl;
}

bool wait_msg_size_start(spot_client_state_t *state,
                         size_t msg_size,
                         int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        if (state->fatal.load(std::memory_order_acquire)
            || state->control_started_msg_size.load(std::memory_order_acquire)
                 == msg_size) {
            return true;
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

    // Large recv-mode fan-in cases can accumulate a substantial warmup backlog
    // before the active phase becomes visible on the client. Give the phase
    // transition enough time to drain queued warmup traffic instead of failing
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

    reset_metrics(state, msg_size);
    if (!ensure_control_connected(state)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] ensure control connect failed"
                      << " size=" << msg_size
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
        std::cerr << "[multi-spot-client] size wait start ts_us="
                  << perf_multi_metric::now_us()
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
    if (bench_transition_debug_enabled())
        print_phase_spread_summary(state, msg_size);

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
    std::thread stdin_watcher([&state]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-client] stdin line ts_us="
                          << perf_multi_metric::now_us()
                          << " line=" << line << std::endl;
            }
            size_t start_size = 0;
            if (parse_control_connected_line(line)) {
                state.control_link_ready.store(true,
                                               std::memory_order_release);
                state.cv.notify_all();
                continue;
            }
            if (parse_control_start(line.data(), line.size(), &start_size)) {
                notify_size_start(&state, start_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                state.fatal.store(true, std::memory_order_release);
                state.cv.notify_all();
                state.start_wait_cv.notify_all();
                return;
            }
        }
    });
    stdin_watcher.detach();
    if (!create_control_spot(ctx, transport, control_endpoint, settings,
                             &state)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-client] create_control_spot failed"
                      << std::endl;
        fast_exit_process(1);
    }
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
