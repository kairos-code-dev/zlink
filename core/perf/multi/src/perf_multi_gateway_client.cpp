#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_GATEWAY";
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
        std::memset(&target_routing_id, 0, sizeof(target_routing_id));
    }

    void *gateway;
    ready_monitor_t monitor;
    size_t slot_index;
    std::vector<char> payload;
    bench_latency_sampler_t latency;
    bool send_pending;
    bool inflight;
    bool send_enabled;
    bool auto_send_on_recv;
    unsigned long long completed_replies;
    zlink_routing_id_t target_routing_id;
    uint32_t run_id;
    size_t msg_size;
    uint64_t next_seq;
    perf_multi_metric::phase_t phase;
};

struct gateway_client_state_t
{
    gateway_client_state_t() :
        poller(NULL),
        collect_active(false),
        active_run_id(0),
        active_msg_size(0),
        active_received(0),
        fatal(false),
        fatal_errno(0)
    {
    }

    std::vector<gateway_client_slot_t *> slots;
    void *poller;
    std::atomic<bool> collect_active;
    std::atomic<uint32_t> active_run_id;
    std::atomic<size_t> active_msg_size;
    std::atomic<unsigned long long> active_received;
    std::atomic<bool> fatal;
    std::atomic<int> fatal_errno;
};

gateway_client_state_t *g_client_state = NULL;

bool open_gateway_ready_monitor(gateway_client_slot_t *slot)
{
    if (!slot || !slot->gateway)
        return false;
    return open_configured_service_monitor(
      slot->gateway,
      ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &slot->monitor);
}

void close_gateway_ready_monitor(gateway_client_slot_t *slot)
{
    if (!slot)
        return;
    close_ready_monitor(slot->monitor);
}

double gateway_percentile_from_sorted(const std::vector<double> &sorted_samples,
                                      double quantile)
{
    if (sorted_samples.empty())
        return 0.0;
    if (quantile <= 0.0)
        return sorted_samples.front();
    if (quantile >= 1.0)
        return sorted_samples.back();

    const double pos =
      (static_cast<double>(sorted_samples.size()) - 1.0) * quantile;
    const size_t lo = static_cast<size_t>(pos);
    const size_t hi =
      lo + 1 < sorted_samples.size() ? lo + 1 : lo;
    const double frac = pos - static_cast<double>(lo);
    return sorted_samples[lo]
           + (sorted_samples[hi] - sorted_samples[lo]) * frac;
}

void mark_fatal(int err)
{
    gateway_client_state_t *state = g_client_state;
    if (!state)
        return;

    state->fatal.store(true, std::memory_order_release);
    state->fatal_errno.store(err != 0 ? err : EIO, std::memory_order_release);
}

bool apply_gateway_options(void *gateway,
                           const multi_bench_settings_t &settings)
{
    const int linger_ms = 0;
    const int sndhwm = bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm);
    const int rcvhwm = bench_hwm_from_env("PERF_MULTI_RCVHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200);

    return zlink_set_option(gateway, ZLINK_OPT_LINGER,
                                    &linger_ms, sizeof(linger_ms))
             == 0
           && zlink_set_option(gateway, ZLINK_OPT_SNDHWM,
                                       &sndhwm, sizeof(sndhwm))
                == 0
           && zlink_set_option(gateway, ZLINK_OPT_RCVHWM,
                                       &rcvhwm, sizeof(rcvhwm))
                == 0
           && zlink_set_option(gateway, ZLINK_OPT_SNDTIMEO,
                                       &sndtimeo_ms, sizeof(sndtimeo_ms))
                == 0;
}

void destroy_gateway_slots(gateway_client_state_t *state,
                           std::vector<gateway_client_slot_t *> *slots)
{
    if (!slots)
        return;

    for (size_t i = 0; i < slots->size(); ++i) {
        gateway_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        if (state && state->poller && slot->gateway)
            (void) zlink_poller_remove(state->poller, slot->gateway);
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

enum send_status_t
{
    send_ok = 0,
    send_blocked = 1,
    send_fatal = 2
};

enum recv_status_t
{
    recv_processed = 0,
    recv_none = 1,
    recv_fatal = 2
};

send_status_t send_gateway_request(gateway_client_slot_t *slot)
{
    if (!slot || !slot->gateway || slot->msg_size == 0 || !slot->send_enabled)
        return send_fatal;

    const size_t payload_size =
      std::max(slot->msg_size, perf_multi_metric::header_size());

    zlink_msg_t part;
    if (zlink_msg_init_size(&part, payload_size) != 0)
        return send_fatal;
    if (!perf_multi_metric::stamp_payload(
          zlink_msg_data(&part),
          payload_size,
          slot->run_id,
          slot->phase,
          slot->msg_size,
          (static_cast<uint64_t>(slot->slot_index) << 48) | slot->next_seq,
          perf_multi_metric::now_us())) {
        zlink_msg_close(&part);
        return send_fatal;
    }

    const int rc =
      zlink_gateway_send_rid(slot->gateway, &slot->target_routing_id, &part, 1,
                             ZLINK_DONTWAIT);
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

recv_status_t receive_gateway_reply(gateway_client_state_t *state,
                                    gateway_client_slot_t *slot)
{
    if (!state || !slot || !slot->gateway)
        return recv_fatal;

    zlink_routing_id_t source_rid;
    std::memset(&source_rid, 0, sizeof(source_rid));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = zlink_recv(slot->gateway, &source_rid, &parts, &part_count,
                              ZLINK_DONTWAIT);
    if (rc != 0) {
        const int err = zlink_errno();
        if (err == EAGAIN || err == EINTR)
            return recv_none;
        return recv_fatal;
    }

    if (!parts || part_count == 0) {
        if (parts) {
            zlink_multipart_close(parts, part_count);
            free(parts);
        }
        return recv_fatal;
    }

    perf_multi_metric::header_t header;
    const bool header_ok =
      perf_multi_metric::decode_payload_header(zlink_msg_data(&parts[0]),
                                               zlink_msg_size(&parts[0]),
                                               &header);
    zlink_multipart_close(parts, part_count);
    free(parts);

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

    slot->inflight = false;
    ++slot->completed_replies;
    if (header_ok
        && state->collect_active.load(std::memory_order_acquire)
        && perf_multi_metric::is_expected(
          header,
          state->active_run_id.load(std::memory_order_acquire),
          perf_multi_metric::phase_active,
          state->active_msg_size.load(std::memory_order_acquire))) {
        state->active_received.fetch_add(1, std::memory_order_acq_rel);
        const uint64_t now_us = perf_multi_metric::now_us();
        const double latency_us =
          header.sent_ts_us > 0 && now_us >= header.sent_ts_us
            ? static_cast<double>(now_us - header.sent_ts_us) * 0.5
            : 0.0;
        slot->latency.add(latency_us);
    }

    if (slot->send_enabled && slot->auto_send_on_recv) {
        const send_status_t send_rc = send_gateway_request(slot);
        if (send_rc == send_fatal) {
            mark_fatal(errno);
            return recv_fatal;
        }
    }

    return recv_processed;
}

bool drain_gateway_replies(gateway_client_state_t *state,
                           gateway_client_slot_t *slot,
                           bool *progressed_out)
{
    bool progressed = false;
    while (true) {
        const recv_status_t recv_rc = receive_gateway_reply(state, slot);
        if (recv_rc == recv_none)
            break;
        if (recv_rc == recv_fatal)
            return false;
        progressed = true;
    }

    if (progressed_out)
        *progressed_out = progressed;
    return true;
}

bool service_gateway_slots(gateway_client_state_t *state,
                           int timeout_ms,
                           bool *progressed_out)
{
    if (progressed_out)
        *progressed_out = false;
    if (!state || !state->poller || state->slots.empty())
        return true;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        const gateway_client_slot_t *slot = state->slots[i];
        if (!slot || !slot->gateway)
            continue;
        short events = ZLINK_POLLIN;
        if (slot->send_pending && slot->send_enabled)
            events = static_cast<short>(events | ZLINK_POLLOUT);
        if (zlink_poller_modify(state->poller, slot->gateway, events) != 0) {
            mark_fatal(zlink_errno());
            return false;
        }
    }

    bool progressed = false;
    std::vector<zlink_poller_event_t> events(state->slots.size());
    const int poll_rc =
      zlink_poller_wait_all(state->poller,
                            events.empty() ? NULL : &events[0],
                            static_cast<int>(events.size()),
                            timeout_ms);
    if (poll_rc < 0 && zlink_errno() != EINTR && zlink_errno() != EAGAIN) {
        mark_fatal(zlink_errno());
        return false;
    }

    for (int i = 0; i < poll_rc; ++i) {
        gateway_client_slot_t *slot =
          static_cast<gateway_client_slot_t *>(events[i].user_data);
        if (!slot)
            continue;

        if ((events[i].events & ZLINK_POLLIN) != 0) {
            bool recv_progressed = false;
            if (!drain_gateway_replies(state, slot, &recv_progressed))
                return false;
            progressed = progressed || recv_progressed;
        }

        if ((events[i].events & ZLINK_POLLOUT) != 0
            && slot->send_pending
            && slot->send_enabled) {
            const send_status_t send_rc = send_gateway_request(slot);
            if (send_rc == send_fatal) {
                mark_fatal(errno);
                return false;
            }
            progressed = progressed || send_rc == send_ok;
        }
    }

    if (progressed_out)
        *progressed_out = progressed;
    return !state->fatal.load(std::memory_order_acquire);
}

bool create_gateway_slots(gateway_client_state_t *state,
                          ctx_guard_t &ctx,
                          const std::string &transport,
                          const std::string &endpoint,
                          const multi_bench_settings_t &settings,
                          size_t max_payload_size,
                          std::vector<gateway_client_slot_t *> *slots_out)
{
    if (!state || !slots_out)
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

        slot->gateway = zlink_gateway_new(ctx.get(), k_service_name);
        if (!slot->gateway || !apply_gateway_options(slot->gateway, settings)
            || zlink_set_routing_id(slot->gateway, routing_id,
                                    std::strlen(routing_id))
                 != 0
            || !setup_tls_client(slot->gateway, transport)
            || !open_gateway_ready_monitor(slot)
            || zlink_gateway_connect(slot->gateway, endpoint.c_str(),
                                     &server_routing_id)
                 != 0) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-gateway-client] slot create failed slot="
                          << i << " gateway=" << (slot->gateway != NULL)
                          << " err=" << zlink_errno() << std::endl;
            }
            close_gateway_ready_monitor(slot);
            if (slot->gateway)
                zlink_gateway_destroy(&slot->gateway);
            delete slot;
            return false;
        }

        slot->slot_index = i;
        slot->target_routing_id = server_routing_id;
        slot->payload.assign(std::max<size_t>(
                               max_payload_size,
                               perf_multi_metric::header_size()),
                             'g');
        if (zlink_poller_add(state->poller, slot->gateway, slot, ZLINK_POLLIN)
            != 0) {
            close_gateway_ready_monitor(slot);
            zlink_gateway_destroy(&slot->gateway);
            delete slot;
            return false;
        }
        slots_out->push_back(slot);

        if (!wait_for_service_monitor_event(
              slot->monitor,
              ZLINK_GATEWAY_SEND_READY_CHANGED,
              ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
              settings.connect_ready_timeout_ms)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-gateway-client] ready wait failed slot="
                          << i << " err=" << zlink_errno() << std::endl;
            }
            return false;
        }
        close_gateway_ready_monitor(slot);
    }

    return !slots_out->empty();
}

void reset_active_metrics(gateway_client_state_t *state,
                          uint32_t run_id,
                          size_t msg_size)
{
    state->collect_active.store(false, std::memory_order_release);
    state->active_run_id.store(run_id, std::memory_order_release);
    state->active_msg_size.store(msg_size, std::memory_order_release);
    state->active_received.store(0, std::memory_order_release);
    for (size_t i = 0; i < state->slots.size(); ++i) {
        gateway_client_slot_t *slot = state->slots[i];
        if (!slot)
            continue;
        slot->latency = bench_latency_sampler_t();
    }
}

void configure_phase_slots(const std::vector<gateway_client_slot_t *> &slots,
                           uint32_t run_id,
                           size_t msg_size,
                           perf_multi_metric::phase_t phase,
                           bool send_enabled)
{
    for (size_t i = 0; i < slots.size(); ++i) {
        gateway_client_slot_t *slot = slots[i];
        if (!slot)
            continue;
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

bool seed_phase_requests(gateway_client_state_t *state,
                         const std::vector<gateway_client_slot_t *> &slots,
                         int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        bool all_seeded = true;
        for (size_t i = 0; i < slots.size(); ++i) {
            gateway_client_slot_t *slot = slots[i];
            if (!slot || slot->inflight || !slot->send_enabled)
                continue;

            const send_status_t send_rc = send_gateway_request(slot);
            if (send_rc == send_fatal) {
                mark_fatal(errno);
                return false;
            }
            if (send_rc != send_ok)
                all_seeded = false;
        }

        if (all_seeded)
            return true;

        const int remaining_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
            .count());
        if (remaining_ms <= 0)
            break;
        if (!service_gateway_slots(state, std::min(remaining_ms, 10), NULL)) {
            return false;
        }
    }

    return false;
}

void stop_phase(const std::vector<gateway_client_slot_t *> &slots)
{
    for (size_t i = 0; i < slots.size(); ++i) {
        gateway_client_slot_t *slot = slots[i];
        if (!slot)
            continue;
        slot->send_enabled = false;
        slot->send_pending = false;
        slot->auto_send_on_recv = false;
    }
}

bool wait_phase_duration(gateway_client_state_t *state, double seconds)
{
    if (seconds <= 0.0)
        return true;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(seconds));

    while (!state->fatal.load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        const int remaining_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
            .count());
        if (remaining_ms <= 0)
            break;
        if (!service_gateway_slots(state, std::min(remaining_ms, 10), NULL)) {
            return false;
        }
    }
    return !state->fatal.load(std::memory_order_acquire);
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
    if (!seed_phase_requests(state, state->slots, settings.connect_ready_timeout_ms))
        return false;
    if (!wait_phase_duration(
          state, static_cast<double>(std::max(0, settings.warmup_seconds)))) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-gateway-client] warmup wait failed"
                      << std::endl;
        }
        return false;
    }

    stop_phase(state->slots);
    reset_active_metrics(state, run_id, msg_size);
    state->collect_active.store(true, std::memory_order_release);
    configure_phase_slots(state->slots, run_id, msg_size,
                          perf_multi_metric::phase_active, true);
    if (!seed_phase_requests(state, state->slots, settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-gateway-client] active seed failed"
                      << std::endl;
        }
        return false;
    }
    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();
    if (!wait_phase_duration(
          state, static_cast<double>(std::max(1, settings.duration_seconds)))) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-gateway-client] active phase failed"
                      << std::endl;
        }
        return false;
    }

    state->collect_active.store(false, std::memory_order_release);
    stop_phase(state->slots);

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    bench_latency_stats_t latency;
    double throughput = 0.0;

    const unsigned long long active_received =
      state->active_received.load(std::memory_order_acquire);
    unsigned long long latency_count = 0;
    double latency_sum_us = 0.0;
    std::vector<double> latency_samples;
    for (size_t i = 0; i < state->slots.size(); ++i) {
        gateway_client_slot_t *slot = state->slots[i];
        if (!slot)
            continue;
        latency_count += slot->latency.count();
        latency_sum_us += slot->latency.sum_us();
        slot->latency.append_samples(&latency_samples);
    }
    if (state->fatal.load(std::memory_order_acquire) || active_received == 0
        || latency_count == 0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-gateway-client] metrics invalid fatal="
                      << state->fatal.load(std::memory_order_acquire)
                      << " received=" << active_received
                      << " latency_count=" << latency_count << std::endl;
        }
        return false;
    }
    throughput =
      static_cast<double>(active_received)
      / static_cast<double>(std::max(1, settings.duration_seconds));
    latency.mean_us = latency_sum_us / static_cast<double>(latency_count);
    if (latency_samples.empty()) {
        latency.p95_us = latency.mean_us;
        latency.p99_us = latency.mean_us;
    } else {
        std::sort(latency_samples.begin(), latency_samples.end());
        latency.p95_us =
          gateway_percentile_from_sorted(latency_samples, 0.95);
        latency.p99_us =
          gateway_percentile_from_sorted(latency_samples, 0.99);
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

    if (!perf_multi_client::is_supported_transport(transport)) {
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
    state.poller = zlink_poller_new();
    if (!state.poller) {
        g_client_state = NULL;
        return 1;
    }
    if (!create_gateway_slots(&state, ctx, transport, endpoint, settings,
                              max_msg_size,
                              &state.slots)) {
        destroy_gateway_slots(&state, &state.slots);
        zlink_poller_destroy(&state.poller);
        g_client_state = NULL;
        return 1;
    }

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (!run_single_size_case(&state, settings, lib_name, transport,
                                  msg_sizes[i])) {
            destroy_gateway_slots(&state, &state.slots);
            zlink_poller_destroy(&state.poller);
            g_client_state = NULL;
            return 1;
        }
    }

    destroy_gateway_slots(&state, &state.slots);
    zlink_poller_destroy(&state.poller);
    g_client_state = NULL;
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
