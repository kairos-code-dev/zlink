#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"
#include "../../../src/services/gateway/gateway.hpp"
#include "../../../src/sockets/socket_base.hpp"

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

static const char *k_pattern = "GATEWAY";
static const char *k_service_name = "perf-gateway";
static const char *k_server_routing_id = "perf-gateway-server";
static std::atomic<int> g_debug_recv_logs(0);
static std::atomic<int> g_debug_send_logs(0);

using perf_multi_client::next_metric_run_id;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::print_client_result_lines;
using perf_multi_client::resolve_case_msg_sizes;

struct gateway_client_slot_t
{
    gateway_client_slot_t() :
        gateway(NULL),
        gateway_impl(NULL),
        monitor(NULL),
        monitor_state(NULL),
        slot_index(0),
        send_pending(false),
        inflight(false),
        send_enabled(false),
        auto_send_on_recv(false),
        completed_replies(0),
        run_id(0),
        msg_size(0),
        next_seq(1),
        phase(perf_multi_metric::phase_unknown)
    {
    }

    void *gateway;
    zlink::gateway_t *gateway_impl;
    void *monitor;
    struct gateway_ready_monitor_state_t *monitor_state;
    size_t slot_index;
    std::mutex mutex;
    std::vector<char> payload;
    bool send_pending;
    bool inflight;
    bool send_enabled;
    bool auto_send_on_recv;
    unsigned long long completed_replies;
    uint32_t run_id;
    size_t msg_size;
    uint64_t next_seq;
    perf_multi_metric::phase_t phase;
};

struct gateway_client_state_t
{
    gateway_client_state_t() :
        collect_active(false),
        active_run_id(0),
        active_msg_size(0),
        active_received(0),
        fatal(false),
        fatal_errno(0)
    {
    }

    std::vector<gateway_client_slot_t *> slots;
    std::mutex metrics_mutex;
    std::condition_variable metrics_cv;
    bench_latency_sampler_t latency;
    bool collect_active;
    uint32_t active_run_id;
    size_t active_msg_size;
    unsigned long long active_received;
    bool fatal;
    int fatal_errno;
};

gateway_client_state_t *g_client_state = NULL;

struct gateway_ready_monitor_state_t
{
    gateway_ready_monitor_state_t() :
        ready_peer_count(0),
        send_ready(false),
        error_code(0)
    {
    }

    std::mutex sync;
    std::condition_variable cv;
    size_t ready_peer_count;
    bool send_ready;
    int error_code;
};

struct gateway_ready_monitor_registry_t
{
    std::mutex sync;
    std::map<void *, gateway_ready_monitor_state_t *> states;
};

gateway_ready_monitor_registry_t &gateway_ready_monitor_registry()
{
    static gateway_ready_monitor_registry_t registry;
    return registry;
}

void register_gateway_ready_monitor(
  void *monitor,
  gateway_ready_monitor_state_t *state)
{
    if (!monitor || !state)
        return;

    gateway_ready_monitor_registry_t &registry =
      gateway_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.sync);
    registry.states[monitor] = state;
}

void unregister_gateway_ready_monitor(void *monitor)
{
    if (!monitor)
        return;

    gateway_ready_monitor_registry_t &registry =
      gateway_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.sync);
    registry.states.erase(monitor);
}

gateway_ready_monitor_state_t *find_gateway_ready_monitor_state()
{
    void *monitor = zlink::current_monitor_dispatch_handle();
    if (!monitor)
        return NULL;

    gateway_ready_monitor_registry_t &registry =
      gateway_ready_monitor_registry();
    std::lock_guard<std::mutex> lock(registry.sync);
    std::map<void *, gateway_ready_monitor_state_t *>::iterator it =
      registry.states.find(monitor);
    return it != registry.states.end() ? it->second : NULL;
}

size_t gateway_ready_count(const gateway_ready_monitor_state_t *state)
{
    if (!state)
        return 0;
    return std::max(state->ready_peer_count, state->send_ready ? size_t(1) : 0);
}

void gateway_ready_monitor_handler(const zlink_service_event_t *event)
{
    gateway_ready_monitor_state_t *state = find_gateway_ready_monitor_state();
    if (!state || !event)
        return;

    {
        std::lock_guard<std::mutex> lock(state->sync);
        switch (event->event_type) {
            case ZLINK_GATEWAY_ROUTE_UP:
            case ZLINK_GATEWAY_ROUTE_DOWN:
                state->ready_peer_count = static_cast<size_t>(event->value);
                break;

            case ZLINK_GATEWAY_SEND_READY_CHANGED:
                state->send_ready = event->value > 0;
                break;

            case ZLINK_GATEWAY_MONITOR_EVENT_ERROR:
                if (state->error_code == 0) {
                    state->error_code =
                      event->error_code != 0 ? event->error_code : EIO;
                }
                break;

            default:
                break;
        }
    }

    state->cv.notify_all();
}

bool open_gateway_ready_monitor(gateway_client_slot_t *slot)
{
    if (!slot || !slot->gateway)
        return false;

    gateway_ready_monitor_state_t *state =
      new (std::nothrow) gateway_ready_monitor_state_t();
    if (!state)
        return false;

    void *monitor = zlink_gateway_monitor_open(
      slot->gateway,
      ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
        | ZLINK_GATEWAY_ROUTE_DOWN
        | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &gateway_ready_monitor_handler);
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

    zlink_monitor_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (zlink_monitor_snapshot(monitor, &snapshot) == 0) {
        state->ready_peer_count = snapshot.ready_peer_count;
        state->send_ready =
          (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0;
    }

    register_gateway_ready_monitor(monitor, state);
    slot->monitor = monitor;
    slot->monitor_state = state;
    return true;
}

bool wait_gateway_ready(gateway_client_slot_t *slot,
                        size_t expected_count,
                        int timeout_ms)
{
    if (!slot || !slot->monitor_state)
        return false;
    if (expected_count == 0)
        return true;

    std::unique_lock<std::mutex> lock(slot->monitor_state->sync);
    if (slot->monitor_state->error_code != 0)
        return false;
    if (gateway_ready_count(slot->monitor_state) >= expected_count)
        return true;

    const int bounded_timeout = timeout_ms > 0 ? timeout_ms : 0;
    if (bounded_timeout == 0)
        return false;

    const bool signaled = slot->monitor_state->cv.wait_for(
      lock,
      std::chrono::milliseconds(bounded_timeout),
      [slot, expected_count]() {
          return slot->monitor_state->error_code != 0
                 || gateway_ready_count(slot->monitor_state) >= expected_count;
      });
    return signaled && slot->monitor_state->error_code == 0
           && gateway_ready_count(slot->monitor_state) >= expected_count;
}

void close_gateway_ready_monitor(gateway_client_slot_t *slot)
{
    if (!slot)
        return;

    gateway_ready_monitor_state_t *state = slot->monitor_state;
    void *monitor = slot->monitor;
    slot->monitor_state = NULL;
    slot->monitor = NULL;

    if (!monitor && !state)
        return;

    if (monitor && zlink_service_monitor_close(&monitor) == 0) {
        unregister_gateway_ready_monitor(monitor);
        delete state;
        return;
    }

    if (bench_debug_enabled()) {
        std::cerr << "[multi-gateway-client] monitor close failed";
        if (monitor)
            std::cerr << ": " << zlink_strerror(zlink_errno());
        std::cerr << std::endl;
    }
}

void gateway_client_send_ready(void *subject);
void gateway_client_recv_handler(const zlink_routing_id_t *,
                                 zlink_msg_t *parts,
                                 size_t part_count);
bool wait_all_gateway_ready(const std::vector<gateway_client_slot_t *> &slots,
                            int timeout_ms);
bool prime_gateway_slot(gateway_client_state_t *state,
                        gateway_client_slot_t *slot,
                        size_t msg_size,
                        int timeout_ms);

bool is_supported_transport(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

void close_parts(zlink_msg_t *parts, size_t part_count)
{
    if (!parts)
        return;
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close(&parts[i]);
}

void mark_fatal(int err)
{
    gateway_client_state_t *state = g_client_state;
    if (!state)
        return;

    std::lock_guard<std::mutex> lock(state->metrics_mutex);
    state->fatal = true;
    state->fatal_errno = err != 0 ? err : EIO;
    state->metrics_cv.notify_all();
}

bool configure_gateway_tls_client(void *gateway,
                                  const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    static const std::string ca_path =
      write_temp_cert(test_certs::ca_cert_pem, "multi_gateway_ca");
    return zlink_gateway_set_tls_client(gateway, ca_path.c_str(), "localhost", 0)
           == 0;
}

bool apply_gateway_options(void *gateway,
                           const multi_bench_settings_t &settings)
{
    const int linger_ms = 0;
    const int sndhwm = bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm);
    const int rcvhwm = bench_hwm_from_env("PERF_MULTI_RCVHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200);

    return zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_LINGER,
                                    &linger_ms, sizeof(linger_ms))
             == 0
           && zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_SNDHWM,
                                       &sndhwm, sizeof(sndhwm))
                == 0
           && zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_RCVHWM,
                                       &rcvhwm, sizeof(rcvhwm))
                == 0
           && zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_SNDTIMEO,
                                       &sndtimeo_ms, sizeof(sndtimeo_ms))
                == 0;
}

void destroy_gateway_slots(std::vector<gateway_client_slot_t *> *slots)
{
    if (!slots)
        return;

    for (size_t i = 0; i < slots->size(); ++i) {
        gateway_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        close_gateway_ready_monitor(slot);
        if (slot->gateway)
            zlink_gateway_destroy(&slot->gateway);
        delete slot;
    }

    slots->clear();
}

bool make_routing_id(const char *text, zlink_routing_id_t *routing_id)
{
    if (!text || !routing_id)
        return false;

    const size_t size = std::strlen(text);
    if (size == 0 || size > sizeof(routing_id->data))
        return false;

    std::memset(routing_id, 0, sizeof(*routing_id));
    std::memcpy(routing_id->data, text, size);
    routing_id->size = static_cast<uint8_t>(size);
    return true;
}

gateway_client_slot_t *find_slot_for_send_ready(void *gateway)
{
    gateway_client_state_t *state = g_client_state;
    if (!state)
        return NULL;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        if (state->slots[i] && state->slots[i]->gateway == gateway)
            return state->slots[i];
    }

    return NULL;
}

gateway_client_slot_t *find_slot_for_recv_dispatch()
{
    gateway_client_state_t *state = g_client_state;
    if (!state)
        return NULL;

    zlink::socket_base_t *current_socket =
      zlink::socket_base_t::current_socket_msg_dispatch_socket();
    if (!current_socket)
        return NULL;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        gateway_client_slot_t *slot = state->slots[i];
        if (slot && slot->gateway_impl
            && slot->gateway_impl->router() == current_socket) {
            return slot;
        }
    }

    return NULL;
}

gateway_client_slot_t *find_slot_for_seq(uint64_t seq)
{
    gateway_client_state_t *state = g_client_state;
    if (!state)
        return NULL;

    const size_t slot_index = static_cast<size_t>(seq >> 48);
    if (slot_index >= state->slots.size())
        return NULL;
    return state->slots[slot_index];
}

enum send_status_t
{
    send_ok = 0,
    send_blocked = 1,
    send_fatal = 2
};

send_status_t send_gateway_request_locked(gateway_client_slot_t *slot)
{
    if (!slot || !slot->gateway || slot->msg_size == 0 || !slot->send_enabled)
        return send_fatal;

    const size_t payload_size =
      std::max(slot->msg_size, perf_multi_metric::header_size());
    if (slot->payload.size() < payload_size)
        return send_fatal;

    if (!perf_multi_metric::stamp_payload(
          slot->payload.data(),
          payload_size,
          slot->run_id,
          slot->phase,
          slot->msg_size,
          (static_cast<uint64_t>(slot->slot_index) << 48) | slot->next_seq,
          perf_multi_metric::now_us())) {
        return send_fatal;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size(&part, payload_size) != 0) {
        return send_fatal;
    }
    if (payload_size > 0) {
        std::memcpy(
          zlink_msg_data(&part),
          static_cast<const void *>(slot->payload.data()),
          payload_size);
    }

    const int rc = zlink_gateway_send(slot->gateway, &part, 1, ZLINK_DONTWAIT);
    if (rc == 0) {
        if (bench_debug_enabled()
            && g_debug_send_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
            std::cerr << "[multi-gateway-client] send ok slot="
                      << slot->slot_index << " size=" << payload_size
                      << " phase=" << static_cast<int>(slot->phase)
                      << " run=" << slot->run_id << std::endl;
        }
        slot->send_pending = false;
        slot->inflight = true;
        ++slot->next_seq;
        return send_ok;
    }
    const int saved_errno = errno;
    (void) zlink_msg_close(&part);

    if (saved_errno == EAGAIN || saved_errno == EHOSTUNREACH
        || saved_errno == ENOTCONN) {
        if (bench_debug_enabled()
            && g_debug_send_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
            std::cerr << "[multi-gateway-client] send blocked slot="
                      << slot->slot_index << " errno=" << saved_errno
                      << " phase=" << static_cast<int>(slot->phase)
                      << " run=" << slot->run_id << std::endl;
        }
        slot->send_pending = true;
        slot->inflight = false;
        errno = saved_errno;
        return send_blocked;
    }

    errno = saved_errno;
    return send_fatal;
}

bool create_gateway_slots(ctx_guard_t &ctx,
                          const std::string &transport,
                          const std::string &endpoint,
                          const multi_bench_settings_t &settings,
                          size_t max_payload_size,
                          std::vector<gateway_client_slot_t *> *slots_out)
{
    if (!slots_out)
        return false;

    const size_t service_clients =
      resolve_multi_service_clients(settings.clients);
    zlink_routing_id_t server_routing_id;
    if (!make_routing_id(k_server_routing_id, &server_routing_id))
        return false;

    for (size_t i = 0; i < service_clients; ++i) {
        char routing_id[64];
        std::snprintf(routing_id, sizeof(routing_id), "gwc-%zu", i);

        gateway_client_slot_t *slot = new gateway_client_slot_t();
        if (!slot)
            return false;

        slot->gateway = zlink_gateway_new(ctx.get(), k_service_name,
                                          routing_id,
                                          &gateway_client_recv_handler);
        if (!slot->gateway || !apply_gateway_options(slot->gateway, settings)
            || !configure_gateway_tls_client(slot->gateway, transport)
            || zlink_gateway_set_send_ready_handler(
                 slot->gateway, &gateway_client_send_ready)
                 != 0
            || !open_gateway_ready_monitor(slot)
            || zlink_gateway_connect(slot->gateway, endpoint.c_str(),
                                     &server_routing_id)
                 != 0) {
            close_gateway_ready_monitor(slot);
            if (slot->gateway)
                zlink_gateway_destroy(&slot->gateway);
            delete slot;
            return false;
        }

        slot->gateway_impl = static_cast<zlink::gateway_t *>(slot->gateway);
        slot->slot_index = i;
        slot->payload.assign(std::max<size_t>(
                               max_payload_size,
                               perf_multi_metric::header_size()),
                             'g');
        slots_out->push_back(slot);

        std::vector<gateway_client_slot_t *> ready_slots(1, slot);
        if (!wait_all_gateway_ready(
              ready_slots,
              settings.connect_ready_timeout_ms)) {
            return false;
        }
        if (!prime_gateway_slot(
              g_client_state,
              slot,
              max_payload_size,
              settings.connect_ready_timeout_ms)) {
            return false;
        }
    }

    return !slots_out->empty();
}

bool wait_all_gateway_ready(const std::vector<gateway_client_slot_t *> &slots,
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
        if (!wait_gateway_ready(slots[i], 1, remaining_ms))
            return false;
    }
    return true;
}

void reset_active_metrics(gateway_client_state_t *state,
                          uint32_t run_id,
                          size_t msg_size)
{
    std::lock_guard<std::mutex> lock(state->metrics_mutex);
    state->latency = bench_latency_sampler_t();
    state->collect_active = false;
    state->active_run_id = run_id;
    state->active_msg_size = msg_size;
    state->active_received = 0;
}

void configure_phase_slots(const std::vector<gateway_client_slot_t *> &slots,
                           uint32_t run_id,
                           size_t msg_size,
                           perf_multi_metric::phase_t phase,
                           bool send_enabled)
{
    for (size_t i = 0; i < slots.size(); ++i) {
        gateway_client_slot_t *slot = slots[i];
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->run_id = run_id;
        slot->msg_size = msg_size;
        slot->phase = phase;
        slot->next_seq = 1;
        slot->send_pending = false;
        slot->inflight = false;
        slot->send_enabled = send_enabled;
        slot->auto_send_on_recv = send_enabled;
    }
}

bool prime_gateway_slot(gateway_client_state_t *state,
                        gateway_client_slot_t *slot,
                        size_t msg_size,
                        int timeout_ms)
{
    if (!state || !slot || msg_size == 0)
        return false;

    const uint32_t run_id = next_metric_run_id();
    unsigned long long baseline_replies = 0;
    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->run_id = run_id;
        slot->msg_size = msg_size;
        slot->phase = perf_multi_metric::phase_warmup;
        slot->next_seq = 1;
        slot->send_pending = false;
        slot->inflight = false;
        slot->send_enabled = true;
        slot->auto_send_on_recv = false;
        baseline_replies = slot->completed_replies;
        const send_status_t rc = send_gateway_request_locked(slot);
        if (rc == send_fatal) {
            mark_fatal(errno);
            return false;
        }
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard<std::mutex> lock(state->metrics_mutex);
            if (state->fatal)
                return false;
        }
        {
            std::lock_guard<std::mutex> lock(slot->mutex);
            if (slot->completed_replies > baseline_replies) {
                slot->send_enabled = false;
                slot->send_pending = false;
                slot->inflight = false;
                return true;
            }
        }

        std::unique_lock<std::mutex> metrics_lock(state->metrics_mutex);
        state->metrics_cv.wait_for(metrics_lock, std::chrono::milliseconds(2));
    }

    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->send_enabled = false;
        slot->send_pending = false;
        slot->auto_send_on_recv = false;
    }
    return false;
}

bool prime_gateway_slots(gateway_client_state_t *state,
                         const std::vector<gateway_client_slot_t *> &slots,
                         size_t msg_size,
                         int timeout_ms)
{
    if (!state)
        return false;

    for (size_t i = 0; i < slots.size(); ++i) {
        if (!prime_gateway_slot(state, slots[i], msg_size, timeout_ms)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-gateway-client] prime failed slot=" << i
                          << std::endl;
            }
            return false;
        }
    }
    return true;
}

bool seed_phase_requests(const std::vector<gateway_client_slot_t *> &slots,
                         int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        bool all_seeded = true;
        for (size_t i = 0; i < slots.size(); ++i) {
            gateway_client_slot_t *slot = slots[i];
            std::lock_guard<std::mutex> lock(slot->mutex);
            if (slot->inflight || !slot->send_enabled)
                continue;

            const send_status_t rc = send_gateway_request_locked(slot);
            if (rc == send_fatal) {
                mark_fatal(errno);
                return false;
            }
            if (rc != send_ok)
                all_seeded = false;
        }

        if (all_seeded)
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return false;
}

void stop_phase(const std::vector<gateway_client_slot_t *> &slots)
{
    for (size_t i = 0; i < slots.size(); ++i) {
        gateway_client_slot_t *slot = slots[i];
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->send_enabled = false;
        slot->send_pending = false;
        slot->auto_send_on_recv = false;
    }
}

bool wait_until_quiescent(gateway_client_state_t *state,
                          const std::vector<gateway_client_slot_t *> &slots,
                          int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        bool all_idle = true;
        for (size_t i = 0; i < slots.size(); ++i) {
            gateway_client_slot_t *slot = slots[i];
            std::lock_guard<std::mutex> lock(slot->mutex);
            if (slot->inflight || slot->send_pending) {
                all_idle = false;
                break;
            }
        }
        if (all_idle)
            return true;

        std::unique_lock<std::mutex> metrics_lock(state->metrics_mutex);
        state->metrics_cv.wait_for(metrics_lock, std::chrono::milliseconds(2));
    }

    return false;
}

bool wait_phase_duration(gateway_client_state_t *state, double seconds)
{
    if (seconds <= 0.0)
        return true;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(seconds));

    std::unique_lock<std::mutex> lock(state->metrics_mutex);
    while (!state->fatal) {
        if (state->metrics_cv.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }
    return !state->fatal;
}

void gateway_client_send_ready(void *subject)
{
    gateway_client_slot_t *slot = find_slot_for_send_ready(subject);
    gateway_client_state_t *state = g_client_state;
    if (!slot || !state)
        return;

    std::lock_guard<std::mutex> lock(slot->mutex);
    if (!slot->send_pending) {
        state->metrics_cv.notify_all();
        return;
    }
    if (!slot->send_enabled) {
        slot->send_pending = false;
        state->metrics_cv.notify_all();
        return;
    }

    const send_status_t rc = send_gateway_request_locked(slot);
    if (rc == send_fatal)
        mark_fatal(errno);
    state->metrics_cv.notify_all();
}

void gateway_client_recv_handler(const zlink_routing_id_t *,
                                 zlink_msg_t *parts,
                                 size_t part_count)
{
    gateway_client_state_t *state = g_client_state;
    if (!state || part_count == 0) {
        close_parts(parts, part_count);
        return;
    }

    perf_multi_metric::header_t header;
    const bool header_ok =
      perf_multi_metric::decode_payload_header(zlink_msg_data(&parts[0]),
                                               zlink_msg_size(&parts[0]),
                                               &header);
    gateway_client_slot_t *slot =
      header_ok ? find_slot_for_seq(header.seq) : find_slot_for_recv_dispatch();
    if (!slot) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-gateway-client] recv slot lookup failed"
                      << std::endl;
        close_parts(parts, part_count);
        return;
    }
    close_parts(parts, part_count);
    if (bench_debug_enabled()
        && g_debug_recv_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
        std::cerr << "[multi-gateway-client] recv header_ok=" << header_ok;
        if (header_ok) {
            std::cerr << " run=" << header.run_id
                      << " phase=" << header.phase
                      << " size=" << header.msg_size
                      << " seq=" << header.seq;
        }
        std::cerr << std::endl;
    }

    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        slot->inflight = false;
        if (slot->send_enabled && slot->auto_send_on_recv) {
            const send_status_t rc = send_gateway_request_locked(slot);
            if (rc == send_fatal) {
                mark_fatal(errno);
                return;
            }
        }
    }

    if (header_ok) {
        std::lock_guard<std::mutex> metrics_lock(state->metrics_mutex);
        ++slot->completed_replies;
        if (state->collect_active
            && perf_multi_metric::is_expected(
              header,
              state->active_run_id,
              perf_multi_metric::phase_active,
              state->active_msg_size)) {
            ++state->active_received;
            const uint64_t now_us = perf_multi_metric::now_us();
            const double latency_us =
              header.sent_ts_us > 0 && now_us >= header.sent_ts_us
                ? static_cast<double>(now_us - header.sent_ts_us) * 0.5
                : 0.0;
            state->latency.add(latency_us);
        }
    }

    state->metrics_cv.notify_all();
}

bool run_single_size_case(gateway_client_state_t *state,
                          const multi_bench_settings_t &settings,
                          const std::string &lib_name,
                          const std::string &transport,
                          size_t msg_size)
{
    const uint32_t run_id = next_metric_run_id();
    reset_active_metrics(state, run_id, msg_size);

    configure_phase_slots(state->slots, run_id, msg_size,
                          perf_multi_metric::phase_warmup, true);
    if (!seed_phase_requests(state->slots, settings.connect_ready_timeout_ms))
        return false;
    if (!wait_phase_duration(
          state, static_cast<double>(std::max(0, settings.warmup_seconds)))) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-gateway-client] warmup wait failed" << std::endl;
        return false;
    }

    stop_phase(state->slots);
    if (settings.settle_ms > 0) {
        configure_phase_slots(state->slots, run_id, msg_size,
                              perf_multi_metric::phase_drain, false);
        if (!wait_phase_duration(
              state,
              static_cast<double>(settings.settle_ms) / 1000.0)) {
            if (bench_debug_enabled ())
                std::cerr << "[multi-gateway-client] settle wait failed"
                          << std::endl;
            return false;
        }
    }

    reset_active_metrics(state, run_id, msg_size);
    {
        std::lock_guard<std::mutex> lock(state->metrics_mutex);
        state->collect_active = true;
    }
    configure_phase_slots(state->slots, run_id, msg_size,
                          perf_multi_metric::phase_active, true);
    if (!seed_phase_requests(state->slots, settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-gateway-client] active seed failed"
                      << std::endl;
        return false;
    }
    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();
    if (!wait_phase_duration(
             state, static_cast<double>(std::max(1, settings.duration_seconds)))) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-gateway-client] active phase failed"
                      << std::endl;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state->metrics_mutex);
        state->collect_active = false;
    }
    stop_phase(state->slots);
    if (settings.settle_ms > 0
        && !wait_phase_duration(
          state, static_cast<double>(settings.settle_ms) / 1000.0)) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-gateway-client] active settle failed"
                      << std::endl;
        return false;
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    bench_latency_stats_t latency;
    double throughput = 0.0;

    {
        std::lock_guard<std::mutex> lock(state->metrics_mutex);
        if (state->fatal || state->active_received == 0
            || state->latency.count() == 0) {
            if (bench_debug_enabled ()) {
                std::cerr << "[multi-gateway-client] metrics invalid fatal="
                          << state->fatal << " received="
                          << state->active_received << " latency_count="
                          << state->latency.count () << std::endl;
            }
            return false;
        }
        throughput =
          static_cast<double>(state->active_received)
          / static_cast<double>(std::max(1, settings.duration_seconds));
        latency = state->latency.snapshot();
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
    size_t max_msg_size = fallback_size > 0 ? fallback_size : 64;
    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    gateway_client_state_t state;
    g_client_state = &state;
    if (!create_gateway_slots(ctx, transport, endpoint, settings, max_msg_size,
                              &state.slots)) {
        destroy_gateway_slots(&state.slots);
        g_client_state = NULL;
        return 1;
    }

    if (!wait_all_gateway_ready(state.slots, settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-gateway-client] gateway ready timeout"
                      << std::endl;
        destroy_gateway_slots(&state.slots);
        g_client_state = NULL;
        return 1;
    }

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (!run_single_size_case(&state, settings, lib_name, transport,
                                  msg_sizes[i])) {
            destroy_gateway_slots(&state.slots);
            g_client_state = NULL;
            return 1;
        }
    }

    destroy_gateway_slots(&state.slots);
    g_client_state = NULL;
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
