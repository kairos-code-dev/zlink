#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"
#include "../../../src/core/monitor_dispatch_internal.hpp"
#include "../../../src/services/spot/spot_dispatch_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
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
        sub(NULL),
        monitor(NULL),
        monitor_state(NULL),
        stop(false)
    {
    }

    void *node;
    void *sub;
    void *monitor;
    struct spot_ready_monitor_state_t *monitor_state;
    std::atomic<bool> stop;
    std::thread recv_thread;
};

struct spot_client_state_t
{
    spot_client_state_t() :
        expected_msg_size(0),
        collect_active(false),
        active_received(0),
        fatal(false),
        fatal_errno(0),
        seen_msg_size(0),
        seen_phase(static_cast<int>(perf_multi_metric::phase_unknown)),
        last_recv_us(0),
        recv_workers_stop(false)
    {
    }

    std::vector<spot_client_slot_t *> slots;
    std::mutex mutex;
    std::condition_variable cv;
    std::mutex latency_mutex;
    bench_latency_sampler_t latency;
    std::atomic<size_t> expected_msg_size;
    std::atomic<bool> collect_active;
    std::atomic<unsigned long long> active_received;
    std::atomic<bool> fatal;
    std::atomic<int> fatal_errno;
    std::atomic<size_t> seen_msg_size;
    std::atomic<int> seen_phase;
    std::atomic<uint64_t> last_recv_us;
    std::atomic<bool> recv_workers_stop;
    std::vector<std::thread> recv_workers;
};

spot_client_state_t *g_client_state = NULL;

struct spot_ready_monitor_state_t
{
    spot_ready_monitor_state_t() :
        filter_applied(false),
        delivery_ready(false),
        error_code(0)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::string expected_endpoint;
    bool filter_applied;
    bool delivery_ready;
    int error_code;
};

struct spot_ready_monitor_registry_t
{
    std::mutex mutex;
    std::map<void *, spot_ready_monitor_state_t *> states;
};

spot_ready_monitor_registry_t &spot_ready_monitor_registry()
{
    static spot_ready_monitor_registry_t registry;
    return registry;
}

void register_spot_ready_monitor(void *monitor,
                                 spot_ready_monitor_state_t *state)
{
    if (!monitor || !state)
        return;

    spot_ready_monitor_registry_t &registry = spot_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.states[monitor] = state;
}

void unregister_spot_ready_monitor(void *monitor)
{
    if (!monitor)
        return;

    spot_ready_monitor_registry_t &registry = spot_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.states.erase(monitor);
}

spot_ready_monitor_state_t *find_spot_ready_monitor_state()
{
    void *monitor = zlink::current_monitor_dispatch_handle();
    if (!monitor)
        return NULL;

    spot_ready_monitor_registry_t &registry = spot_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    std::map<void *, spot_ready_monitor_state_t *>::iterator it =
      registry.states.find(monitor);
    return it != registry.states.end() ? it->second : NULL;
}

bool spot_event_matches_endpoint(const zlink_service_event_t *event,
                                 const std::string &expected_endpoint)
{
    if (!event)
        return false;
    if (expected_endpoint.empty())
        return true;
    if ((event->detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
        return false;

    return std::strncmp(event->endpoint,
                        expected_endpoint.c_str(),
                        expected_endpoint.size())
           == 0;
}

bool spot_event_matches_subject(const zlink_service_event_t *event,
                                const char *expected_subject)
{
    if (!event || !expected_subject || expected_subject[0] == '\0')
        return false;
    if ((event->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
        return false;
    return std::strcmp(event->subject, expected_subject) == 0;
}

bool is_spot_ready(const spot_ready_monitor_state_t *state)
{
    return state && state->filter_applied && state->delivery_ready;
}

void spot_ready_monitor_handler(const zlink_service_event_t *event)
{
    spot_ready_monitor_state_t *state = find_spot_ready_monitor_state();
    if (!state || !event)
        return;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        switch (event->event_type) {
            case ZLINK_SPOT_SUB_FILTER_APPLIED:
                if (spot_event_matches_subject(event, k_topic))
                    state->filter_applied = true;
                break;

            case ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED:
                if (spot_event_matches_endpoint(event, state->expected_endpoint))
                    state->delivery_ready =
                      spot_event_matches_subject(event, k_topic)
                      && event->value > 0;
                break;

            case ZLINK_MONITOR_EVENT_ERROR:
                if (state->error_code == 0)
                    state->error_code =
                      event->error_code != 0 ? event->error_code : EIO;
                break;

            default:
                break;
        }
    }

    state->cv.notify_all();
}

void close_parts(zlink_msg_t *parts, size_t part_count)
{
    if (!parts)
        return;
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close(&parts[i]);
}

void discard_spot_parts(const zlink_routing_id_t *,
                        const char *,
                        size_t,
                        zlink_msg_t *parts,
                        size_t part_count)
{
    close_parts(parts, part_count);
}

bool is_supported_transport(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

void fast_exit_process(int exit_code)
{
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
}

void mark_fatal(int err)
{
    spot_client_state_t *state = g_client_state;
    if (!state)
        return;

    state->fatal.store(true, std::memory_order_release);
    state->fatal_errno.store(err != 0 ? err : EIO, std::memory_order_release);
    state->cv.notify_all();
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
      "PERF_MULTI_SPOT_RECV_WORKER_MIN_MSG_SIZE", 131072, 0));
}

size_t resolve_spot_recv_worker_count()
{
    return static_cast<size_t>(
      resolve_multi_int_env("PERF_MULTI_SPOT_RECV_WORKERS", 1, 1));
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
    return zlink_spot_node_set_tls_client(node, ca_path.c_str(), "localhost", 0)
           == 0;
}

bool apply_spot_sub_options(void *sub, const multi_bench_settings_t &settings)
{
    const int linger_ms = 0;
    const int rcvhwm = bench_hwm_from_env("PERF_MULTI_RCVHWM", settings.hwm);
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_RCVTIMEO_MS", 200);

    return zlink_spot_set_sub_option(sub, ZLINK_SPOT_SUB_OPT_LINGER,
                                     &linger_ms, sizeof(linger_ms))
             == 0
           && zlink_spot_set_sub_option(sub, ZLINK_SPOT_SUB_OPT_RCVHWM,
                                        &rcvhwm, sizeof(rcvhwm))
                == 0
           && zlink_spot_set_sub_option(sub, ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                                        &rcvtimeo_ms, sizeof(rcvtimeo_ms))
                == 0;
}

bool open_spot_ready_monitor(spot_client_slot_t *slot,
                             const std::string &endpoint)
{
    if (!slot || !slot->sub)
        return false;

    spot_ready_monitor_state_t *state =
      new (std::nothrow) spot_ready_monitor_state_t();
    if (!state)
        return false;
    state->expected_endpoint = endpoint;

    void *monitor = zlink_spot_monitor_open(
      slot->sub,
      ZLINK_SPOT_ROLE_SUB,
      ZLINK_SPOT_SUB_FILTER_APPLIED | ZLINK_SPOT_SUB_DELIVERY_READY_CHANGED
        | ZLINK_MONITOR_EVENT_ERROR,
      &spot_ready_monitor_handler);
    if (!monitor) {
        delete state;
        return false;
    }

    const int monitor_hwm = bench_hwm_from_env("PERF_MONITOR_HWM", 1000);
    set_sockopt_int(monitor, ZLINK_LINGER, 0, "ZLINK_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int(monitor, ZLINK_SNDHWM, monitor_hwm, "ZLINK_SNDHWM");
        set_sockopt_int(monitor, ZLINK_RCVHWM, monitor_hwm, "ZLINK_RCVHWM");
    }

    register_spot_ready_monitor(monitor, state);
    slot->monitor = monitor;
    slot->monitor_state = state;
    return true;
}

bool wait_spot_ready(spot_client_slot_t *slot, int timeout_ms)
{
    if (!slot || !slot->monitor_state)
        return false;

    std::unique_lock<std::mutex> lock(slot->monitor_state->mutex);
    if (slot->monitor_state->error_code != 0)
        return false;
    if (is_spot_ready(slot->monitor_state))
        return true;

    return slot->monitor_state->cv.wait_for(
             lock,
             std::chrono::milliseconds(std::max(1, timeout_ms)),
             [slot]() {
                 return slot->monitor_state->error_code != 0
                        || is_spot_ready(slot->monitor_state);
             })
           && slot->monitor_state->error_code == 0
           && is_spot_ready(slot->monitor_state);
}

void close_spot_ready_monitor(spot_client_slot_t *slot)
{
    if (!slot)
        return;

    spot_ready_monitor_state_t *state = slot->monitor_state;
    void *monitor = slot->monitor;
    void *monitor_handle = monitor;
    slot->monitor_state = NULL;
    slot->monitor = NULL;

    if (!monitor && !state)
        return;

    if (monitor && zlink_service_monitor_close(&monitor) == 0) {
        unregister_spot_ready_monitor(monitor_handle);
        delete state;
        return;
    }
}

void close_all_spot_ready_monitors(std::vector<spot_client_slot_t *> *slots)
{
    if (!slots)
        return;

    for (size_t i = 0; i < slots->size(); ++i) {
        if ((*slots)[i])
            close_spot_ready_monitor((*slots)[i]);
    }
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

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        if (slot->recv_thread.joinable())
            slot->recv_thread.join();
    }

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        close_spot_ready_monitor(slot);
        if (slot->sub)
            zlink_spot_destroy(&slot->sub);
        if (slot->node)
            zlink_spot_node_destroy(&slot->node);
        delete slot;
    }

    slots->clear();
}

void handle_spot_client_parts(const char *topic,
                              size_t topic_len,
                              zlink_msg_t *parts,
                              size_t part_count)
{
    spot_client_state_t *state = g_client_state;
    if (!state || !topic || topic_len != std::strlen(k_topic)
        || std::memcmp(topic, k_topic, topic_len) != 0 || part_count == 0) {
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
      state->seen_msg_size.exchange(header.msg_size, std::memory_order_acq_rel);
    const int previous_phase =
      state->seen_phase.exchange(header.phase, std::memory_order_acq_rel);
    const uint64_t now_us = perf_multi_metric::now_us();
    state->last_recv_us.store(now_us, std::memory_order_release);

    if (bench_debug_enabled() && previous_msg_size == 0) {
        std::cerr << "[multi-spot-client] first recv size="
                  << header.msg_size << " phase=" << header.phase
                  << " run=" << header.run_id << std::endl;
    }

    if (state->collect_active.load(std::memory_order_acquire)
        && header.magic == perf_multi_metric::k_magic
        && header.run_id == k_metric_run_id
        && header.msg_size
             == state->expected_msg_size.load(std::memory_order_acquire)
        && header.phase
             == static_cast<uint32_t>(perf_multi_metric::phase_active)) {
        const unsigned long long received =
          state->active_received.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (should_sample_spot_latency(received)) {
            const double latency_us =
              header.sent_ts_us > 0 && now_us >= header.sent_ts_us
                ? static_cast<double>(now_us - header.sent_ts_us)
                : 0.0;
            std::lock_guard<std::mutex> latency_lock(state->latency_mutex);
            state->latency.add(latency_us);
        }
    }

    if (previous_msg_size != header.msg_size || previous_phase != header.phase)
        state->cv.notify_all();
}

void spot_client_sub_handler(const zlink_routing_id_t *,
                             const char *topic,
                             size_t topic_len,
                             zlink_msg_t *parts,
                             size_t part_count)
{
    handle_spot_client_parts(topic, topic_len, parts, part_count);
}

bool recv_one_spot_message(spot_client_slot_t *slot, int flags, bool *received)
{
    if (!slot || !slot->sub)
        return false;

    if (received)
        *received = false;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof(topic) - 1;
    const int rc =
      zlink_spot_recv(slot->sub, &parts, &part_count, flags, topic, &topic_len);
    if (rc != 0) {
        const int err = errno;
        if (slot->stop.load(std::memory_order_acquire))
            return false;
        if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
            || err == ETIMEDOUT) {
            return true;
        }
        mark_fatal(err);
        return false;
    }

    if (received)
        *received = true;
    if (topic_len < sizeof(topic))
        topic[topic_len] = '\0';
    handle_spot_client_parts(topic, topic_len, parts, part_count);
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

void spot_client_recv_loop(spot_client_slot_t *slot)
{
    if (!slot || !slot->sub)
        return;

    while (!slot->stop.load(std::memory_order_acquire)) {
        bool received = false;
        if (!recv_one_spot_message(slot, 0, &received))
            break;
        if (!received)
            continue;
        if (!drain_spot_client_slot(slot, NULL))
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
                       std::vector<spot_client_slot_t *> *slots_out)
{
    if (!slots_out)
        return false;

    const size_t service_clients =
      resolve_multi_service_clients(settings.clients);
    for (size_t i = 0; i < service_clients; ++i) {
        spot_client_slot_t *slot = new (std::nothrow) spot_client_slot_t();
        if (!slot)
            return false;

        char service_name[64];
        std::snprintf(service_name, sizeof(service_name), "perf-spot-c%zu", i);
        slot->node = zlink_spot_node_new(ctx.get(), service_name, NULL);
        if (!slot->node || !configure_spot_tls_client(slot->node, transport)) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-client] node create/tls failed slot="
                          << i << " err=" << zlink_errno() << std::endl;
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        slot->sub =
          zlink_spot_new(slot->node,
                         recv_mode == spot_recv_callback
                           ? &spot_client_sub_handler
                           : NULL);
        if (!slot->sub || !apply_spot_sub_options(slot->sub, settings)
            || !open_spot_ready_monitor(slot, endpoint)
            || zlink_spot_subscribe(slot->sub, k_topic) != 0
            || zlink_spot_node_connect_peer_pub(slot->node, endpoint.c_str()) != 0) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-client] slot create failed slot=" << i
                          << " sub=" << (slot->sub != NULL)
                          << " err=" << zlink_errno() << std::endl;
            close_spot_ready_monitor(slot);
            if (slot->sub)
                zlink_spot_destroy(&slot->sub);
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        if (recv_mode == spot_recv_slot_threads)
            slot->recv_thread = std::thread(spot_client_recv_loop, slot);
        slots_out->push_back(slot);
    }

    return !slots_out->empty();
}

bool start_spot_recv_workers(spot_client_state_t *state, spot_recv_mode_t mode)
{
    if (!state)
        return false;
    if (mode != spot_recv_worker_threads)
        return true;
    if (state->slots.empty())
        return false;

    const size_t worker_count =
      std::min(state->slots.size(), resolve_spot_recv_worker_count());
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

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    for (size_t i = 0; i < slots.size(); ++i) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return false;

        const int remaining_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now)
            .count());
        if (!wait_spot_ready(slots[i], remaining_ms))
            return false;
    }
    return true;
}

void reset_metrics(spot_client_state_t *state, size_t msg_size)
{
    {
        std::lock_guard<std::mutex> latency_lock(state->latency_mutex);
        state->latency = bench_latency_sampler_t();
    }
    state->expected_msg_size.store(msg_size, std::memory_order_release);
    state->collect_active.store(false, std::memory_order_release);
    state->active_received.store(0, std::memory_order_release);
    state->seen_msg_size.store(0, std::memory_order_release);
    state->seen_phase.store(static_cast<int>(perf_multi_metric::phase_unknown),
                            std::memory_order_release);
    state->last_recv_us.store(0, std::memory_order_release);
}

bool wait_phase_start(spot_client_state_t *state,
                      size_t msg_size,
                      perf_multi_metric::phase_t phase,
                      int timeout_ms)
{
    std::unique_lock<std::mutex> lock(state->mutex);
    return state->cv.wait_for(
      lock,
      std::chrono::milliseconds(std::max(1, timeout_ms)),
      [state, msg_size, phase]() {
          return state->fatal.load(std::memory_order_acquire)
                 || (state->seen_msg_size.load(std::memory_order_acquire)
                       == msg_size
                     && state->seen_phase.load(std::memory_order_acquire)
                          == static_cast<int>(phase));
      });
}

bool wait_msg_size_phase(spot_client_state_t *state,
                         size_t msg_size,
                         int timeout_ms,
                         perf_multi_metric::phase_t *phase_out)
{
    if (!state)
        return false;

    std::unique_lock<std::mutex> lock(state->mutex);
    const bool ready = state->cv.wait_for(
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
    if (!ready || state->fatal.load(std::memory_order_acquire))
        return false;

    if (phase_out)
        *phase_out = static_cast<perf_multi_metric::phase_t>(
          state->seen_phase.load(std::memory_order_acquire));
    return true;
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

bool wait_for_quiet(spot_client_state_t *state, int quiet_ms, int timeout_ms)
{
    if (!state)
        return false;

    const uint64_t quiet_us =
      static_cast<uint64_t>(std::max(1, quiet_ms)) * 1000ULL;
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->fatal.load(std::memory_order_acquire)) {
        const uint64_t now_us = perf_multi_metric::now_us();
        const uint64_t last_recv_us =
          state->last_recv_us.load(std::memory_order_acquire);
        if (last_recv_us != 0 && now_us >= last_recv_us
            && (now_us - last_recv_us) >= quiet_us) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            break;
        state->cv.wait_for(lock, std::chrono::milliseconds(5));
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

    reset_metrics(state, msg_size);
    perf_multi_metric::phase_t start_phase =
      perf_multi_metric::phase_unknown;
    if (!wait_msg_size_phase(state, msg_size, phase_timeout_ms,
                             &start_phase)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-client] warmup start timeout size="
                      << msg_size << std::endl;
        return false;
    }

    if (start_phase == perf_multi_metric::phase_unknown) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] unexpected start phase size="
                      << msg_size << " phase="
                      << static_cast<int>(start_phase) << std::endl;
        }
        return false;
    }

    if (bench_debug_enabled()) {
        std::cerr << "[multi-spot-client] collect start size="
                  << msg_size << " active-phase-only=1" << std::endl;
    }

    state->collect_active.store(true, std::memory_order_release);
    if (start_phase != perf_multi_metric::phase_active
        && !wait_phase_start(state,
                             msg_size,
                             perf_multi_metric::phase_active,
                             phase_timeout_ms)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-client] active start timeout size="
                      << msg_size << std::endl;
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
    {
        std::lock_guard<std::mutex> latency_lock(state->latency_mutex);
        state->collect_active.store(false, std::memory_order_release);
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    bench_latency_stats_t latency;
    double throughput = 0.0;

    {
        std::lock_guard<std::mutex> latency_lock(state->latency_mutex);
        const unsigned long long active_received =
          state->active_received.load(std::memory_order_acquire);
        if (state->fatal.load(std::memory_order_acquire)
            || active_received == 0 || state->latency.count() == 0) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-client] metrics invalid fatal="
                          << state->fatal.load(std::memory_order_acquire)
                          << " received=" << active_received
                          << " latency_count=" << state->latency.count()
                          << std::endl;
            }
            return false;
        }
        throughput =
          static_cast<double>(active_received)
          / static_cast<double>(std::max(1, settings.duration_seconds));
        latency = state->latency.snapshot();
    }

    if (settings.settle_ms > 0) {
        const int quiet_timeout_ms =
          std::max(settings.connect_ready_timeout_ms,
                   std::max(1, settings.duration_seconds) * 5000);
        if (!wait_for_quiet(state,
                            std::max(100, settings.settle_ms),
                            quiet_timeout_ms)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-client] quiet wait failed size="
                          << msg_size << std::endl;
            }
            return false;
        }
    }

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
                           &state.slots)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] create_spot_slots failed"
                      << " mode=" << spot_recv_mode_name(recv_mode)
                      << std::endl;
        }
        destroy_spot_slots(&state, &state.slots);
        g_client_state = NULL;
        return 1;
    }

    if (!wait_all_sub_ready(state.slots, settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] subscription ready timeout"
                      << " mode=" << spot_recv_mode_name(recv_mode)
                      << std::endl;
        }
        destroy_spot_slots(&state, &state.slots);
        g_client_state = NULL;
        return 1;
    }

    close_all_spot_ready_monitors(&state.slots);
    if (!start_spot_recv_workers(&state, recv_mode)) {
        destroy_spot_slots(&state, &state.slots);
        g_client_state = NULL;
        return 1;
    }

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (!run_single_size_case(&state, settings, lib_name, transport,
                                  msg_sizes[i])) {
            destroy_spot_slots(&state, &state.slots);
            g_client_state = NULL;
            fast_exit_process(1);
        }
    }

    destroy_spot_slots(&state, &state.slots);
    g_client_state = NULL;
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
