#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_ROUTER_ROUTER";
static const int k_client_socket_type = ZLINK_SOCKET_ROUTER;
static const char *k_server_routing_id = "SERVER";

using perf_multi_client::close_client_monitors;
using perf_multi_client::create_client_sockets;
using perf_multi_client::is_supported_transport;
using perf_multi_client::next_metric_run_id;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::print_client_result_lines;
using perf_multi_client::resolve_case_max_msg_size;
using perf_multi_client::resolve_case_msg_sizes;
using perf_multi_client::wait_all_client_connect_ready;

struct router_client_slot_t
{
    router_client_slot_t() :
        socket(NULL),
        slot_index(0),
        send_pending(false),
        inflight(false),
        send_enabled(false),
        auto_send_on_recv(false),
        poller_events(0),
        run_id(0),
        msg_size(0),
        next_seq(1),
        phase(perf_multi_metric::phase_unknown)
    {
        std::memset(&target_routing_id, 0, sizeof(target_routing_id));
    }

    void *socket;
    size_t slot_index;
    std::vector<char> payload;
    bench_latency_sampler_t latency;
    bool send_pending;
    bool inflight;
    bool send_enabled;
    bool auto_send_on_recv;
    short poller_events;
    zlink_routing_id_t target_routing_id;
    uint32_t run_id;
    size_t msg_size;
    uint64_t next_seq;
    perf_multi_metric::phase_t phase;
};

struct router_client_state_t
{
    router_client_state_t() :
        poller(NULL),
        collect_active(false),
        active_run_id(0),
        active_msg_size(0),
        active_received(0),
        fatal(false),
        fatal_errno(0)
    {
    }

    std::vector<void *> sockets;
    std::vector<router_client_slot_t> slots;
    void *poller;
    std::atomic<bool> collect_active;
    std::atomic<uint32_t> active_run_id;
    std::atomic<size_t> active_msg_size;
    std::atomic<unsigned long long> active_received;
    std::atomic<bool> fatal;
    std::atomic<int> fatal_errno;
};

enum send_status_t
{
    send_status_ok = 0,
    send_status_blocked = 1,
    send_status_fatal = 2
};

enum recv_status_t
{
    recv_status_processed = 0,
    recv_status_none = 1,
    recv_status_fatal = 2
};

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

double percentile_from_sorted(const std::vector<double> &sorted_samples,
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

void close_router_slots(router_client_state_t *state)
{
    if (!state)
        return;

    if (state->poller) {
        for (size_t i = 0; i < state->slots.size(); ++i) {
            if (state->slots[i].socket) {
                (void) zlink_poller_remove(state->poller, state->slots[i].socket);
            }
        }
        zlink_poller_destroy(&state->poller);
    }

    for (size_t i = 0; i < state->sockets.size(); ++i) {
        if (state->sockets[i]) {
            zlink_close(state->sockets[i]);
            state->sockets[i] = NULL;
        }
    }

    state->slots.clear();
    state->sockets.clear();
}

void mark_fatal(router_client_state_t *state, int err)
{
    if (!state)
        return;

    state->fatal.store(true, std::memory_order_release);
    state->fatal_errno.store(err != 0 ? err : EIO, std::memory_order_release);
}

send_status_t send_router_request(router_client_slot_t *slot)
{
    if (!slot || !slot->socket || slot->msg_size == 0 || !slot->send_enabled)
        return send_status_fatal;

    const size_t payload_size =
      std::max(slot->msg_size, perf_multi_metric::header_size());
    if (slot->payload.size() < payload_size)
        slot->payload.resize(payload_size, 'r');

    zlink_msg_t part;
    if (zlink_msg_init_data(
          &part,
          payload_size > 0
            ? static_cast<void *>(slot->payload.data())
            : static_cast<void *>(NULL),
          payload_size,
          NULL,
          NULL)
        != 0) {
        return send_status_fatal;
    }

    if (!perf_multi_metric::stamp_payload(
          slot->payload.data(),
          payload_size,
          slot->run_id,
          slot->phase,
          slot->msg_size,
          (static_cast<uint64_t>(slot->slot_index) << 48) | slot->next_seq,
          perf_multi_metric::now_us())) {
        zlink_msg_close(&part);
        return send_status_fatal;
    }

    const int rc =
      zlink_send_rid(slot->socket, &slot->target_routing_id, &part, 1,
                     ZLINK_DONTWAIT);
    if (rc == 0) {
        slot->send_pending = false;
        slot->inflight = true;
        ++slot->next_seq;
        return send_status_ok;
    }

    const int saved_errno = errno;
    (void) zlink_msg_close(&part);
    if (saved_errno == EAGAIN || saved_errno == EINTR
        || saved_errno == ENOTCONN || saved_errno == EHOSTUNREACH) {
        slot->send_pending = true;
        slot->inflight = false;
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
}

recv_status_t receive_router_reply(router_client_state_t *state,
                                   router_client_slot_t *slot)
{
    if (!state || !slot || !slot->socket)
        return recv_status_fatal;

    zlink_routing_id_t source_rid;
    std::memset(&source_rid, 0, sizeof(source_rid));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc =
      zlink_recv(slot->socket, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
    if (rc != 0) {
        const int err = zlink_errno();
        if (err == EAGAIN || err == EINTR)
            return recv_status_none;
        return recv_status_fatal;
    }

    if (!parts || part_count == 0) {
        if (parts) {
            zlink_multipart_close(parts, part_count);
            free(parts);
        }
        return recv_status_fatal;
    }

    perf_multi_metric::header_t header;
    const bool header_ok =
      perf_multi_metric::decode_payload_header(zlink_msg_data(&parts[0]),
                                               zlink_msg_size(&parts[0]),
                                               &header);
    zlink_multipart_close(parts, part_count);
    free(parts);

    slot->inflight = false;
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
        const send_status_t send_rc = send_router_request(slot);
        if (send_rc == send_status_fatal)
            return recv_status_fatal;
    }

    return recv_status_processed;
}

bool drain_router_replies(router_client_state_t *state,
                          router_client_slot_t *slot,
                          bool *progressed_out)
{
    bool progressed = false;
    while (true) {
        const recv_status_t recv_rc = receive_router_reply(state, slot);
        if (recv_rc == recv_status_none)
            break;
        if (recv_rc == recv_status_fatal)
            return false;
        progressed = true;
    }

    if (progressed_out)
        *progressed_out = progressed;
    return true;
}

bool service_router_slots(router_client_state_t *state,
                          int timeout_ms,
                          bool *progressed_out)
{
    if (progressed_out)
        *progressed_out = false;
    if (!state || !state->poller || state->slots.empty())
        return true;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        router_client_slot_t &slot = state->slots[i];
        if (!slot.socket)
            continue;
        short events = ZLINK_POLLIN;
        if (slot.send_pending && slot.send_enabled)
            events = static_cast<short>(events | ZLINK_POLLOUT);
        if (slot.poller_events != events) {
            if (zlink_poller_modify(state->poller, slot.socket, events) != 0) {
                mark_fatal(state, zlink_errno());
                return false;
            }
            slot.poller_events = events;
        }
    }

    bool progressed = false;
    std::vector<zlink_poller_event_t> events(state->slots.size());
    const int poll_rc =
      zlink_poller_wait_all(state->poller,
                            events.empty() ? NULL : &events[0],
                            static_cast<int>(events.size()),
                            timeout_ms);
    if (poll_rc < 0) {
        const int err = zlink_errno();
        if (err != EINTR && err != EAGAIN) {
            mark_fatal(state, err);
            return false;
        }
    }

    for (int i = 0; i < poll_rc; ++i) {
        router_client_slot_t *slot =
          static_cast<router_client_slot_t *>(events[i].user_data);
        if (!slot)
            continue;

        if ((events[i].events & ZLINK_POLLIN) != 0) {
            bool recv_progressed = false;
            if (!drain_router_replies(state, slot, &recv_progressed)) {
                mark_fatal(state, errno);
                return false;
            }
            progressed = progressed || recv_progressed;
        }

        if ((events[i].events & ZLINK_POLLOUT) != 0
            && slot->send_pending
            && slot->send_enabled) {
            const send_status_t send_rc = send_router_request(slot);
            if (send_rc == send_status_fatal) {
                mark_fatal(state, errno);
                return false;
            }
            progressed = progressed || send_rc == send_status_ok;
        }
    }

    if (progressed_out)
        *progressed_out = progressed;
    return !state->fatal.load(std::memory_order_acquire);
}

bool create_router_slots(router_client_state_t *state,
                         ctx_guard_t &ctx,
                         const std::string &transport,
                         const std::string &endpoint,
                         const multi_bench_settings_t &settings,
                         size_t max_payload_size)
{
    if (!state)
        return false;

    std::vector<ready_monitor_t> monitors;
    if (!create_client_sockets(ctx, transport, endpoint, settings,
                               k_client_socket_type, &state->sockets,
                               &monitors)) {
        close_client_monitors(&monitors);
        return false;
    }

    if (!wait_all_client_connect_ready(monitors,
                                       settings.connect_ready_timeout_ms)) {
        close_client_monitors(&monitors);
        return false;
    }
    close_client_monitors(&monitors);

    state->poller = zlink_poller_new();
    if (!state->poller)
        return false;

    state->slots.resize(state->sockets.size());
    zlink_routing_id_t target_routing_id;
    if (!make_routing_id(k_server_routing_id, &target_routing_id))
        return false;

    const size_t payload_capacity =
      std::max(max_payload_size, perf_multi_metric::header_size());
    for (size_t i = 0; i < state->sockets.size(); ++i) {
        router_client_slot_t &slot = state->slots[i];
        slot.socket = state->sockets[i];
        slot.slot_index = i;
        slot.target_routing_id = target_routing_id;
        slot.payload.assign(payload_capacity, 'r');
        if (zlink_poller_add(state->poller, slot.socket, &slot, ZLINK_POLLIN)
            != 0) {
            return false;
        }
        slot.poller_events = ZLINK_POLLIN;
    }

    return !state->slots.empty();
}

void reset_active_metrics(router_client_state_t *state,
                          uint32_t run_id,
                          size_t msg_size)
{
    if (!state)
        return;

    state->collect_active.store(false, std::memory_order_release);
    state->active_run_id.store(run_id, std::memory_order_release);
    state->active_msg_size.store(msg_size, std::memory_order_release);
    state->active_received.store(0, std::memory_order_release);
    for (size_t i = 0; i < state->slots.size(); ++i)
        state->slots[i].latency = bench_latency_sampler_t();
}

void configure_phase_slots(router_client_state_t *state,
                           uint32_t run_id,
                           size_t msg_size,
                           perf_multi_metric::phase_t phase,
                           bool send_enabled)
{
    if (!state)
        return;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        router_client_slot_t &slot = state->slots[i];
        slot.run_id = run_id;
        slot.msg_size = msg_size;
        slot.phase = phase;
        slot.next_seq = 1;
        slot.send_pending = false;
        slot.inflight = false;
        slot.send_enabled = send_enabled;
        slot.auto_send_on_recv = send_enabled;
        slot.poller_events = 0;
    }
}

bool start_phase_requests(router_client_state_t *state,
                          int timeout_ms)
{
    if (!state)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        bool all_started = true;
        for (size_t i = 0; i < state->slots.size(); ++i) {
            router_client_slot_t &slot = state->slots[i];
            if (!slot.socket || slot.inflight || !slot.send_enabled)
                continue;

            const send_status_t send_rc = send_router_request(&slot);
            if (send_rc == send_status_fatal) {
                mark_fatal(state, errno);
                return false;
            }
            if (send_rc != send_status_ok)
                all_started = false;
        }

        if (all_started)
            return true;

        const int remaining_ms = static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now())
            .count());
        if (remaining_ms <= 0)
            break;
        if (!service_router_slots(state, std::min(remaining_ms, 10), NULL))
            return false;
    }

    return false;
}

void stop_phase(router_client_state_t *state)
{
    if (!state)
        return;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        state->slots[i].send_enabled = false;
        state->slots[i].send_pending = false;
        state->slots[i].auto_send_on_recv = false;
    }
}

bool wait_phase_duration(router_client_state_t *state, double seconds)
{
    if (!state)
        return false;
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
        if (!service_router_slots(state, std::min(remaining_ms, 10), NULL))
            return false;
    }

    return !state->fatal.load(std::memory_order_acquire);
}

bool run_single_size_case(router_client_state_t *state,
                          const multi_bench_settings_t &settings,
                          const std::string &lib_name,
                          const std::string &transport,
                          size_t msg_size)
{
    if (!state)
        return false;

    const uint32_t run_id = next_metric_run_id();
    reset_active_metrics(state, run_id, msg_size);

    configure_phase_slots(state, run_id, msg_size,
                          perf_multi_metric::phase_warmup, true);
    if (!start_phase_requests(state, settings.connect_ready_timeout_ms))
        return false;
    if (!wait_phase_duration(
          state, static_cast<double>(std::max(0, settings.warmup_seconds)))) {
        return false;
    }

    stop_phase(state);
    reset_active_metrics(state, run_id, msg_size);
    state->collect_active.store(true, std::memory_order_release);
    configure_phase_slots(state, run_id, msg_size,
                          perf_multi_metric::phase_active, true);
    if (!start_phase_requests(state, settings.connect_ready_timeout_ms))
        return false;

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();
    if (!wait_phase_duration(
          state, static_cast<double>(std::max(1, settings.duration_seconds)))) {
        return false;
    }

    state->collect_active.store(false, std::memory_order_release);
    stop_phase(state);

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    const unsigned long long active_received =
      state->active_received.load(std::memory_order_acquire);

    unsigned long long latency_count = 0;
    double latency_sum_us = 0.0;
    std::vector<double> latency_samples;
    for (size_t i = 0; i < state->slots.size(); ++i) {
        latency_count += state->slots[i].latency.count();
        latency_sum_us += state->slots[i].latency.sum_us();
        state->slots[i].latency.append_samples(&latency_samples);
    }

    if (state->fatal.load(std::memory_order_acquire) || active_received == 0
        || latency_count == 0) {
        return false;
    }

    bench_latency_stats_t latency;
    latency.mean_us =
      latency_sum_us / static_cast<double>(latency_count);
    if (latency_samples.empty()) {
        latency.p95_us = latency.mean_us;
        latency.p99_us = latency.mean_us;
    } else {
        std::sort(latency_samples.begin(), latency_samples.end());
        latency.p95_us = percentile_from_sorted(latency_samples, 0.95);
        latency.p99_us = percentile_from_sorted(latency_samples, 0.99);
    }

    const double throughput =
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
    const size_t max_msg_size =
      resolve_case_max_msg_size(fallback_size, msg_sizes);

    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    router_client_state_t state;
    if (!create_router_slots(&state, ctx, transport, endpoint, settings,
                             max_msg_size)) {
        close_router_slots(&state);
        return 1;
    }

    for (size_t si = 0; si < msg_sizes.size(); ++si) {
        if (!run_single_size_case(&state, settings, lib_name, transport,
                                  msg_sizes[si])) {
            close_router_slots(&state);
            return 1;
        }
    }

    close_router_slots(&state);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern(k_pattern))
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
