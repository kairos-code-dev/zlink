#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"
#include "../../../src/services/spot/spot_dispatch_internal.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
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

struct spot_client_slot_t
{
    spot_client_slot_t() : node(NULL), sub(NULL)
    {
    }

    void *node;
    void *sub;
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
        seen_phase(perf_multi_metric::phase_unknown),
        last_recv_us(0)
    {
    }

    std::vector<spot_client_slot_t *> slots;
    std::mutex mutex;
    std::condition_variable cv;
    bench_latency_sampler_t latency;
    size_t expected_msg_size;
    bool collect_active;
    unsigned long long active_received;
    bool fatal;
    int fatal_errno;
    size_t seen_msg_size;
    perf_multi_metric::phase_t seen_phase;
    uint64_t last_recv_us;
};

spot_client_state_t *g_client_state = NULL;

void spot_client_sub_handler(const zlink_routing_id_t *,
                             const char *topic,
                             size_t topic_len,
                             zlink_msg_t *parts,
                             size_t part_count);

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

void mark_fatal(int err)
{
    spot_client_state_t *state = g_client_state;
    if (!state)
        return;

    std::lock_guard<std::mutex> lock(state->mutex);
    state->fatal = true;
    state->fatal_errno = err != 0 ? err : EIO;
    state->cv.notify_all();
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

    return zlink_spot_set_sub_option(sub, ZLINK_SPOT_SUB_OPT_LINGER,
                                     &linger_ms, sizeof(linger_ms))
             == 0
           && zlink_spot_set_sub_option(sub, ZLINK_SPOT_SUB_OPT_RCVHWM,
                                        &rcvhwm, sizeof(rcvhwm))
                == 0;
}

void destroy_spot_slots(std::vector<spot_client_slot_t *> *slots)
{
    if (!slots)
        return;

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t *slot = (*slots)[i];
        if (!slot)
            continue;
        if (slot->sub)
            zlink_spot_destroy(&slot->sub);
        if (slot->node)
            zlink_spot_node_destroy(&slot->node);
        delete slot;
    }

    slots->clear();
}

bool create_spot_slots(ctx_guard_t &ctx,
                       const std::string &transport,
                       const std::string &endpoint,
                       const multi_bench_settings_t &settings,
                       std::vector<spot_client_slot_t *> *slots_out)
{
    if (!slots_out)
        return false;

    const size_t service_clients =
      resolve_multi_service_clients(settings.clients);
    for (size_t i = 0; i < service_clients; ++i) {
        spot_client_slot_t *slot = new spot_client_slot_t();
        if (!slot)
            return false;

        char service_name[64];
        std::snprintf(service_name, sizeof(service_name), "perf-spot-c%zu", i);
        slot->node =
          zlink_spot_node_new(ctx.get(), service_name, &discard_spot_parts);
        if (!slot->node || !configure_spot_tls_client(slot->node, transport)) {
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        slot->sub = zlink_spot_new(slot->node, &spot_client_sub_handler);
        if (!slot->sub || apply_spot_sub_options(slot->sub, settings) == false
            || zlink_spot_subscribe(slot->sub, k_topic) != 0
            || zlink_spot_node_connect_peer_pub(slot->node, endpoint.c_str()) != 0) {
            if (slot->sub)
                zlink_spot_destroy(&slot->sub);
            if (slot->node)
                zlink_spot_node_destroy(&slot->node);
            delete slot;
            return false;
        }

        slots_out->push_back(slot);
    }

    return !slots_out->empty();
}

bool wait_all_sub_peers(const std::vector<spot_client_slot_t *> &slots,
                        int timeout_ms)
{
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        size_t ready = 0;
        for (size_t i = 0; i < slots.size(); ++i) {
            size_t count = 0;
            if (slots[i] && zlink_spot_peers_sub(slots[i]->sub, NULL, &count) == 0
                && count > 0) {
                ++ready;
            }
        }
        if (ready == slots.size())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    for (size_t i = 0; i < slots.size(); ++i) {
        size_t count = 0;
        if (!slots[i] || zlink_spot_peers_sub(slots[i]->sub, NULL, &count) != 0
            || count == 0) {
            return false;
        }
    }
    return true;
}

void reset_metrics(spot_client_state_t *state, size_t msg_size)
{
    std::lock_guard<std::mutex> lock(state->mutex);
    state->latency = bench_latency_sampler_t();
    state->expected_msg_size = msg_size;
    state->collect_active = false;
    state->active_received = 0;
    state->seen_msg_size = 0;
    state->seen_phase = perf_multi_metric::phase_unknown;
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
          return state->fatal
                 || (state->seen_msg_size == msg_size
                     && state->seen_phase == phase);
      });
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
    while (!state->fatal) {
        if (state->cv.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }
    return !state->fatal;
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
    while (!state->fatal) {
        const uint64_t now_us = perf_multi_metric::now_us();
        if (state->last_recv_us != 0 && now_us >= state->last_recv_us
            && (now_us - state->last_recv_us) >= quiet_us) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline)
            break;
        state->cv.wait_for(lock, std::chrono::milliseconds(5));
    }

    return !state->fatal;
}

void spot_client_sub_handler(const zlink_routing_id_t *,
                             const char *topic,
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

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->seen_msg_size = header.msg_size;
        state->seen_phase =
          static_cast<perf_multi_metric::phase_t>(header.phase);
        state->last_recv_us = perf_multi_metric::now_us();
        if (state->collect_active
            && perf_multi_metric::is_expected(
              header,
              k_metric_run_id,
              perf_multi_metric::phase_active,
              state->expected_msg_size)) {
            ++state->active_received;
            const uint64_t now_us = perf_multi_metric::now_us();
            const double latency_us =
              header.sent_ts_us > 0 && now_us >= header.sent_ts_us
                ? static_cast<double>(now_us - header.sent_ts_us)
                : 0.0;
            state->latency.add(latency_us);
        }
        state->cv.notify_all();
    }
}

bool run_single_size_case(spot_client_state_t *state,
                          const multi_bench_settings_t &settings,
                          const std::string &lib_name,
                          const std::string &transport,
                          size_t msg_size)
{
    reset_metrics(state, msg_size);
    if (!wait_phase_start(state, msg_size, perf_multi_metric::phase_warmup,
                          settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-spot-client] warmup start timeout size="
                      << msg_size << std::endl;
        return false;
    }

    if (!wait_phase_duration(
          state, static_cast<double>(std::max(0, settings.warmup_seconds)))) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-spot-client] warmup wait failed size="
                      << msg_size << std::endl;
        return false;
    }

    if (settings.settle_ms > 0
        && !wait_phase_start(state,
                             msg_size,
                             perf_multi_metric::phase_drain,
                             std::max(settings.connect_ready_timeout_ms,
                                      settings.settle_ms))) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-spot-client] drain start timeout size="
                      << msg_size << std::endl;
        return false;
    }

    reset_metrics(state, msg_size);
    if (!wait_phase_start(state, msg_size, perf_multi_metric::phase_active,
                          settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-spot-client] active start timeout size="
                      << msg_size << std::endl;
        return false;
    }

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->collect_active = true;
    }
    if (!wait_phase_duration(
          state, static_cast<double>(std::max(1, settings.duration_seconds)))) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-spot-client] active wait failed size="
                      << msg_size << std::endl;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->collect_active = false;
    }

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    bench_latency_stats_t latency;
    double throughput = 0.0;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->fatal || state->active_received == 0
            || state->latency.count() == 0) {
            if (bench_debug_enabled ()) {
                std::cerr << "[multi-spot-client] metrics invalid fatal="
                          << state->fatal << " received="
                          << state->active_received << " latency_count="
                          << state->latency.count() << std::endl;
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

    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    spot_client_state_t state;
    g_client_state = &state;
    if (!create_spot_slots(ctx, transport, endpoint, settings, &state.slots)) {
        destroy_spot_slots(&state.slots);
        g_client_state = NULL;
        return 1;
    }

    if (!wait_all_sub_peers(state.slots, settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled ())
            std::cerr << "[multi-spot-client] sub peer ready timeout"
                      << std::endl;
        destroy_spot_slots(&state.slots);
        g_client_state = NULL;
        return 1;
    }

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (!run_single_size_case(&state, settings, lib_name, transport,
                                  msg_sizes[i])) {
            destroy_spot_slots(&state.slots);
            g_client_state = NULL;
            return 1;
        }
    }

    (void) wait_for_quiet(
      &state,
      std::max(100, settings.settle_ms),
      settings.connect_ready_timeout_ms);

    destroy_spot_slots(&state.slots);
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
