#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_handshake.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_DEALER_DEALER";
static const char *k_token = "dealer_dealer";
static const zlink_socket_type_t k_server_socket_type = ZLINK_SOCKET_DEALER;

using perf_multi_client::normalize_latency_stats;

enum recv_result_t
{
    recv_ok = 0,
    recv_none = 1,
    recv_fatal = 2
};

inline bool decode_and_match_header (const zlink_msg_t *msg,
                                     size_t expected_msg_size,
                                     uint32_t expected_run_id,
                                     perf_multi_metric::phase_t expected_phase,
                                     perf_multi_metric::header_t *header_out)
{
    if (!msg || !header_out)
        return false;

    if (!perf_multi_metric::decode_payload_header (
          zlink_msg_data (const_cast<zlink_msg_t *> (msg)),
          zlink_msg_size (const_cast<zlink_msg_t *> (msg)),
          header_out)) {
        return false;
    }

    return header_out->magic == perf_multi_metric::k_magic
           && header_out->run_id == expected_run_id
           && header_out->phase == static_cast<uint32_t> (expected_phase)
           && header_out->msg_size == static_cast<uint32_t> (expected_msg_size);
}

inline recv_result_t receive_one_message (
  void *server,
  int flags,
  size_t expected_msg_size,
  uint32_t expected_run_id,
  perf_multi_metric::phase_t expected_phase,
  bool *sender_window_started,
  uint64_t *sender_window_start_us,
  uint64_t *sender_window_end_us,
  uint64_t active_duration_us,
  bool count_message,
  bool collect_latency,
  long *message_count,
  double *lat_sum,
  long *lat_count,
  bench_latency_sampler_t *lat_samples)
{
    if (!server)
        return recv_fatal;

    zlink_routing_id_t source_rid;
    source_rid.size = 0;
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = ::zlink_recv (
      server, &source_rid, &parts, &part_count,
      static_cast<zlink_send_flags_t> (flags));
    if (rc < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR || err == ETIMEDOUT)
            return recv_none;
        if (bench_transition_debug_enabled ()) {
            std::cerr << "[multi-dealer-dealer-server] recv fatal err=" << err
                      << " size=" << expected_msg_size << " run="
                      << expected_run_id << " phase="
                      << static_cast<unsigned int> (expected_phase)
                      << std::endl;
        }
        return recv_fatal;
    }

    if (part_count < 1) {
        if (bench_transition_debug_enabled ()) {
            std::cerr << "[multi-dealer-dealer-server] recv part_count="
                      << part_count << " size=" << expected_msg_size
                      << " run=" << expected_run_id << " phase="
                      << static_cast<unsigned int> (expected_phase)
                      << std::endl;
        }
        if (parts) {
            zlink_multipart_close (parts, part_count);
        }
        return recv_fatal;
    }

    perf_multi_metric::header_t header;
    const bool matched = decode_and_match_header (
      &parts[0],
      expected_msg_size,
      expected_run_id,
      expected_phase,
      &header);

    if (!matched && bench_debug_enabled ()) {
        std::cerr << "[multi-dealer-dealer-server] header mismatch expected_size="
                  << expected_msg_size << " expected_run=" << expected_run_id
                  << " expected_phase="
                  << static_cast<unsigned int> (expected_phase)
                  << " got_magic=" << header.magic << " got_run="
                  << header.run_id << " got_phase=" << header.phase
                  << " got_size=" << header.msg_size << std::endl;
    }
    bool count_matched = matched;
    if (matched && sender_window_started && sender_window_start_us
        && sender_window_end_us) {
        if (!(*sender_window_started)) {
            const uint64_t window_start_us =
              header.sent_ts_us > 0
                ? header.sent_ts_us
                : perf_multi_metric::now_us ();
            *sender_window_started = true;
            *sender_window_start_us = window_start_us;
            *sender_window_end_us = window_start_us + active_duration_us;
        }
        if (count_matched && header.sent_ts_us > 0
            && header.sent_ts_us > *sender_window_end_us)
            count_matched = false;
    }

    if (count_matched && count_message && message_count)
        (*message_count)++;

    if (count_matched && collect_latency && lat_sum && lat_count) {
        const uint64_t now_us = perf_multi_metric::now_us ();
        if (header.sent_ts_us > 0 && now_us >= header.sent_ts_us) {
            const double sample_us = static_cast<double> (now_us - header.sent_ts_us);
            *lat_sum += sample_us;
            (*lat_count)++;
            if (lat_samples)
                lat_samples->add (sample_us);
        }
    }

    zlink_multipart_close (parts, part_count);
    return recv_ok;
}

inline bool drain_non_blocking_messages (
  void *server,
  size_t expected_msg_size,
  uint32_t expected_run_id,
  perf_multi_metric::phase_t expected_phase,
  bool *sender_window_started,
  uint64_t *sender_window_start_us,
  uint64_t *sender_window_end_us,
  uint64_t active_duration_us,
  bool count_message,
  bool collect_latency,
  long *message_count,
  double *lat_sum,
  long *lat_count,
  bench_latency_sampler_t *lat_samples)
{
    while (!perf_stop_requested ().load (std::memory_order_acquire)) {
        const recv_result_t status = receive_one_message (
          server,
          ZLINK_DONTWAIT,
          expected_msg_size,
          expected_run_id,
          expected_phase,
          sender_window_started,
          sender_window_start_us,
          sender_window_end_us,
          active_duration_us,
          count_message,
          collect_latency,
          message_count,
          lat_sum,
          lat_count,
          lat_samples);
        if (status == recv_none)
            break;
        if (status == recv_fatal)
            return false;
    }
    return true;
}

inline bool run_receive_window (
  void *server,
  size_t expected_msg_size,
  uint32_t expected_run_id,
  perf_multi_metric::phase_t expected_phase,
  double measure_seconds,
  double local_wait_seconds,
  bool *sender_window_started,
  uint64_t *sender_window_start_us,
  uint64_t *sender_window_end_us,
  bool count_message,
  bool collect_latency,
  long *message_count,
  double *lat_sum,
  long *lat_count,
  bench_latency_sampler_t *lat_samples)
{
    if (!server)
        return false;
    if (measure_seconds <= 0.0)
        return true;

    if (local_wait_seconds <= 0.0)
        local_wait_seconds = measure_seconds;

    std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (local_wait_seconds));
    const uint64_t active_duration_us =
      static_cast<uint64_t> (
        std::max (1.0, measure_seconds) * 1000000.0);
    const double delivery_slack_s =
      std::max (1.0, std::min (5.0, measure_seconds));
    bool active_window_observed =
      sender_window_started ? *sender_window_started : false;

    while (!perf_stop_requested ().load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < deadline) {
        const bool had_sender_window =
          sender_window_started ? *sender_window_started : false;
        const recv_result_t status = receive_one_message (
          server,
          0,
          expected_msg_size,
          expected_run_id,
          expected_phase,
          sender_window_started,
          sender_window_start_us,
          sender_window_end_us,
          active_duration_us,
          count_message,
          collect_latency,
          message_count,
          lat_sum,
          lat_count,
          lat_samples);
        if (status == recv_none)
            continue;
        if (status == recv_fatal)
            return false;

        if (sender_window_started && *sender_window_started
            && (!active_window_observed || !had_sender_window)) {
            active_window_observed = true;
            deadline =
              std::chrono::steady_clock::now ()
              + std::chrono::duration_cast<
                std::chrono::steady_clock::duration> (
                std::chrono::duration<double> (
                  measure_seconds + delivery_slack_s));
        }

        if (!drain_non_blocking_messages (
              server,
              expected_msg_size,
              expected_run_id,
              expected_phase,
              sender_window_started,
              sender_window_start_us,
              sender_window_end_us,
              active_duration_us,
              count_message,
              collect_latency,
              message_count,
              lat_sum,
              lat_count,
              lat_samples)) {
            return false;
        }
    }

    return true;
}

inline bool drain_phase_until_idle (void *server,
                                    size_t expected_msg_size,
                                    uint32_t expected_run_id,
                                    perf_multi_metric::phase_t expected_phase,
                                    double max_wait_seconds,
                                    int idle_wait_ms)
{
    if (!server)
        return false;

    if (max_wait_seconds <= 0.0)
        return true;

    if (idle_wait_ms <= 0)
        idle_wait_ms = 50;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (max_wait_seconds));
    std::chrono::steady_clock::time_point idle_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (idle_wait_ms);

    while (!perf_stop_requested ().load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < deadline) {
        const recv_result_t status = receive_one_message (
          server,
          ZLINK_DONTWAIT,
          expected_msg_size,
          expected_run_id,
          expected_phase,
          NULL,
          NULL,
          NULL,
          0,
          false,
          false,
          NULL,
          NULL,
          NULL,
          NULL);
        if (status == recv_fatal)
            return false;
        if (status == recv_ok) {
            idle_deadline =
              std::chrono::steady_clock::now ()
              + std::chrono::milliseconds (idle_wait_ms);
            continue;
        }

        if (std::chrono::steady_clock::now () >= idle_deadline)
            return true;

        const long wait_ms = std::max<long> (
          1,
          std::min<long> (
            idle_wait_ms,
            std::chrono::duration_cast<std::chrono::milliseconds> (
              idle_deadline - std::chrono::steady_clock::now ())
              .count ()));
        if (perf_socket_poll (NULL, 0, wait_ms) < 0
            && zlink_errno () != EINTR) {
            return false;
        }
    }

    return std::chrono::steady_clock::now () >= idle_deadline;
}

inline bool run_one_size_benchmark (
  void *server,
  const multi_bench_settings_t &settings,
  size_t msg_size,
  uint32_t run_id,
  const std::string &lib_name,
  const std::string &transport)
{
    const double active_s =
      static_cast<double> (std::max (1, settings.duration_seconds));

    long recv_count = 0;
    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_sampler_t lat_samples;
    bool sender_window_started = false;
    uint64_t sender_window_start_us = 0;
    uint64_t sender_window_end_us = 0;

    const bool active_ok = run_receive_window (
      server,
      msg_size,
      run_id,
      perf_multi_metric::phase_active,
      active_s,
      std::max (active_s + 2.0, active_s + 5.0),
      &sender_window_started,
      &sender_window_start_us,
      &sender_window_end_us,
      true,
      true,
      &recv_count,
      &lat_sum,
      &lat_count,
      &lat_samples);
    if (!active_ok) {
        if (bench_transition_debug_enabled ()) {
            std::cerr << "[multi-dealer-dealer-server] active failed size="
                      << msg_size << " run=" << run_id << std::endl;
        }
        return false;
    }

    if (recv_count <= 0 || lat_count <= 0) {
        if (bench_transition_debug_enabled ()) {
            std::cerr << "[multi-dealer-dealer-server] active empty size="
                      << msg_size << " run=" << run_id
                      << " recv_count=" << recv_count
                      << " lat_count=" << lat_count
                      << " sender_window_started="
                      << sender_window_started << std::endl;
        }
        return false;
    }

    bench_latency_stats_t latency;
    normalize_latency_stats (lat_sum, lat_count, &lat_samples, &latency);

    const double throughput =
      static_cast<double> (recv_count)
      / static_cast<double> (std::max (1, settings.duration_seconds));

    print_result (
      lib_name,
      k_pattern,
      transport,
      msg_size,
      throughput,
      latency.mean_us,
      latency.p95_us,
      latency.p99_us);

    return true;
}

inline int run_server_benchmark (const std::string &lib_name,
                                 const std::string &transport)
{
    set_perf_multi_pattern_env (k_pattern);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available (transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *server = zlink_socket (ctx.get (), k_server_socket_type);
    if (!server)
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    apply_benchmark_socket_options (server, settings.hwm, transport);

    if (!setup_tls_server (server, transport)) {
        zlink_close (server);
        return 1;
    }

    const std::string endpoint = bind_server_endpoint (
      server,
      transport,
      lib_name + std::string ("_") + k_token + "_server");
    if (endpoint.empty ()) {
        zlink_close (server);
        return 1;
    }

    perf_stop_requested ().store (false, std::memory_order_release);
    install_perf_signal_handlers ();

    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);
    std::cout << "READY," << endpoint << std::endl;

    bool ok = true;
    for (size_t si = 0; si < sizes.size (); ++si) {
        if (perf_stop_requested ().load (std::memory_order_acquire)) {
            ok = false;
            break;
        }

        if (bench_transition_debug_enabled ()) {
            std::cerr << "[multi-dealer-dealer-server] wait start size="
                      << sizes[si] << std::endl;
        }
        if (!perf_multi_handshake::wait_for_start_from_stdin (sizes[si])) {
            if (bench_transition_debug_enabled ()) {
                std::cerr << "[multi-dealer-dealer-server] start gate failed size="
                          << sizes[si] << std::endl;
            }
            ok = false;
            break;
        }
        if (bench_transition_debug_enabled ()) {
            std::cerr << "[multi-dealer-dealer-server] start size="
                      << sizes[si] << std::endl;
        }

        const uint32_t run_id = static_cast<uint32_t> (si + 1);
        if (!run_one_size_benchmark (
              server,
              settings,
              sizes[si],
              run_id,
              lib_name,
              transport)) {
            ok = false;
            break;
        }
    }

    perf_stop_requested ().store (true, std::memory_order_release);
    zlink_close (server);

    return ok ? 0 : 1;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark (lib_name, transport);
}
