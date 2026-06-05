// MULTI_SPOT server benchmark: one-way SPOT publisher source.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"
#include "../common/perf_spot_phase.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{

static const char *k_pattern = "MULTI_SPOT";
static const char *k_topic = "bench";
static const char *k_control_topic = "bench_ctl";
static const uint32_t k_run_id = 1U;

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

bool wait_for_spot_send_progress (zlink::poller_t *poller_, bool send_enabled_)
{
    if (send_enabled_ && poller_) {
        try {
            zlink::poll_event_t event;
            (void) poller_->wait (&event, 1, std::chrono::milliseconds (-1));
            return true;
        }
        catch (const zlink::binding_error_t &) {
            return false;
        }
    }
    return !send_enabled_;
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "spot server: " << message_ << std::endl;
}

int resolve_multi_int_env (const char *env_name_, int default_value_, int min_value_)
{
    const char *value = std::getenv (env_name_);
    if (value == NULL || *value == '\0')
        return default_value_;
    char *end = NULL;
    const long parsed = std::strtol (value, &end, 10);
    if (end == value)
        return default_value_;
    int result = static_cast<int> (parsed);
    if (result < min_value_)
        result = min_value_;
    return result;
}

// MULTI_SPOT clean-latency second pass: when PERF_MULTI_SPOT_LATENCY_ONLY is
// set the active phase publishes at a fixed pacing interval (unsaturated) so
// the client measures clean latency. Mirrors the C reference
// resolve_spot_latency_only_mode / resolve_spot_latency_only_interval_us
// (bindings/c/perf/multi/src/perf_multi_spot_server.cpp:197-208) and the
// run_phase pacing (lines 334-373).
bool resolve_spot_latency_only_mode ()
{
    const char *value = std::getenv ("PERF_MULTI_SPOT_LATENCY_ONLY");
    return value != NULL && *value != '\0' && std::strcmp (value, "0") != 0;
}

int resolve_spot_latency_only_interval_us ()
{
    return resolve_multi_int_env ("PERF_MULTI_SPOT_LATENCY_ONLY_INTERVAL_US", 1000, 1);
}

perf::multi::start_signal_state_t g_start_gate;

perf::multi::control_connect_gate_t g_control_connect_gate;

void fast_exit_process (int exit_code_)
{
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (exit_code_);
}

int bench_pid ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
#endif
}

int resolve_spot_start_timeout_ms (const perf::multi::multi_bench_settings_t &settings_)
{
    return std::max (settings_.connect_ready_timeout_ms, std::max (1000, settings_.connect_ready_timeout_ms * 6));
}

int resolve_spot_barrier_timeout_ms (const perf::multi::multi_bench_settings_t &settings_,
                                     const std::string &transport_)
{
    int timeout_ms = resolve_spot_start_timeout_ms (settings_);
    if (transport_ == "wss")
        timeout_ms = std::max (timeout_ms, 20000);
    else if (transport_ == "ws" || transport_ == "tls")
        timeout_ms = std::max (timeout_ms, 10000);
    return timeout_ms;
}

bool wait_for_start_signal (size_t msg_size_, int timeout_ms_)
{
    return perf::multi::wait_for_start (&g_start_gate, msg_size_, timeout_ms_);
}

bool wait_for_control_connect (zlink::service::spot_node_t &control_node_, int timeout_ms_)
{
    std::string endpoint;
    if (!perf::multi::wait_for_control_connect (&g_control_connect_gate, control_node_, timeout_ms_, &endpoint)) {
        return false;
    }
    std::cout << "CONTROL_CONNECTED," << endpoint << std::endl;
    return true;
}

bool run_phase (zlink::service::spot_t &spot_,
                zlink::poller_t *send_poller_,
                const std::string &channel_name_,
                size_t msg_size_,
                uint64_t &seq_,
                perf_metric::phase_t phase_,
                std::chrono::steady_clock::duration duration_)
{
    if (duration_ <= std::chrono::steady_clock::duration::zero ())
        return true;

    const auto deadline = std::chrono::steady_clock::now () + duration_;
    const size_t payload_size = std::max<size_t> (msg_size_, perf_metric::header_size ());
    zlink::message_t outbound (payload_size);
    if (!outbound.valid ()) {
        errno = EINVAL;
        return false;
    }
    unsigned long long publish_ok_count = 0;
    unsigned long long publish_blocked_count = 0;
    unsigned long long publish_wait_count = 0;

    // Clean-latency pacing: in latency-only mode the active phase publishes
    // one message per fixed interval (unsaturated) so the client measures
    // clean latency. Active aggregation on the client is still the configured
    // duration. Mirrors C reference run_phase
    // (bindings/c/perf/multi/src/perf_multi_spot_server.cpp:334-373).
    const bool latency_only = resolve_spot_latency_only_mode () && phase_ == perf_metric::phase_active;
    const auto probe_interval = std::chrono::microseconds (resolve_spot_latency_only_interval_us ());
    auto next_probe_at = std::chrono::steady_clock::now ();

    while (std::chrono::steady_clock::now () < deadline) {
        (void) channel_name_;
        if (latency_only) {
            const auto now = std::chrono::steady_clock::now ();
            if (now < next_probe_at) {
                const auto wait_for =
                  std::min<std::chrono::steady_clock::duration> (next_probe_at - now, std::chrono::milliseconds (10));
                std::this_thread::sleep_for (wait_for);
                continue;
            }
        }
        const auto publish_once = [&] (zlink::send_flags_t flags_, int *saved_errno_out_) -> int {
            if (!saved_errno_out_) {
                errno = EFAULT;
                return -1;
            }

            zlink::message_t part (payload_size);
            if (!part.valid ()) {
                *saved_errno_out_ = errno;
                return -1;
            }
            if (!perf_metric::stamp_payload (part.data (), part.size (), k_run_id, phase_, msg_size_, seq_,
                                             perf_metric::now_ns ())) {
                const int stamp_errno = errno != 0 ? errno : EFAULT;
                part.close ();
                *saved_errno_out_ = stamp_errno;
                errno = stamp_errno;
                return -1;
            }

            try {
                const bool ok = spot_.publish (k_topic).message (part).flags (static_cast<int> (flags_)).submit ();
                *saved_errno_out_ = ok ? 0 : EAGAIN;
                return ok ? 0 : -1;
            }
            catch (const zlink::submit_error_t &) {
                *saved_errno_out_ = errno;
                return -1;
            }
        };

        int saved_errno = 0;
        const int rc = publish_once (static_cast<int> (zlink::send_flags_t::dontwait), &saved_errno);

        if (rc == 0) {
            ++publish_ok_count;
            ++seq_;
            if (latency_only) {
                next_probe_at = std::chrono::steady_clock::now () + probe_interval;
            }
            continue;
        }

        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK && saved_errno != ETIMEDOUT) {
            errno = saved_errno != 0 ? saved_errno : EFAULT;
            return false;
        }
        ++publish_blocked_count;
        ++publish_wait_count;
        if (!wait_for_spot_send_progress (send_poller_, true))
            return false;
    }

    if (std::getenv ("PERF_DEBUG_TRANSITIONS") != NULL) {
        std::cerr << "[multi-spot-server] phase done size=" << msg_size_ << " phase=" << static_cast<int> (phase_)
                  << " ok=" << publish_ok_count << " blocked=" << publish_blocked_count
                  << " wait=" << publish_wait_count << std::endl;
    }

    return true;
}

} // namespace

bool perf_spot_server (const std::string &lib_name, const std::string &transport_, size_t msg_size_)
{
    perf::multi::set_perf_pattern_env ("SPOT");

    if (!perf::multi::validate_multi_perf_pattern (k_pattern))
        return false;

    if (!perf::multi::is_supported_transport (transport_)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << "," << transport_ << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings = perf::multi::resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = perf::multi::resolve_case_msg_sizes (msg_size_);

    perf::multi::ctx_guard_t ctx;
    zlink::service::spot_node_t node (ctx.ctx ());
    if (!node.valid ())
        return false;

    zlink::service::spot_node_t control_node (ctx.ctx ());
    if (!control_node.valid ())
        return false;

    const std::string spot_channel_name = "spot-bench";
    const std::string control_channel_name = "spot-control";
    if (!perf::multi::configure_spot_server_tls (node, transport_))
        return false;
    if (!perf::multi::configure_spot_control_tls (control_node, transport_))
        return false;

    const int control_hwm = std::max (1024, static_cast<int> (settings.clients * 8));
    if (!perf::multi::apply_spot_node_admission_hwm (node, settings.sndhwm, settings.rcvhwm)
        || !perf::multi::apply_spot_node_admission_hwm (control_node, control_hwm, control_hwm))
        return false;

    zlink::service::spot_t spot = node.create_spot ();
    if (!spot.valid ())
        return false;
    zlink::poller_t send_poller;
    try {
        send_poller.add (spot, zlink::poll_event_flag_t::pollout, 0);
    }
    catch (const zlink::binding_error_t &) {
        return false;
    }
    zlink::service::spot_t control_pub = control_node.create_spot ();
    zlink::service::spot_t control_sub = control_node.create_spot ();
    if (!control_pub.valid () || !control_sub.valid ())
        return false;
    const size_t snapshot_msg_size = msg_sizes.empty () ? msg_size_ : msg_sizes[0];
    if (!perf::multi::apply_spot_auto_hwm_msg_unit (ctx.ctx (), snapshot_msg_size))
        return false;
    if (!perf::multi::recalculate_auto_hwm (ctx))
        return false;
    perf::multi::emit_spot_node_auto_hwm_snapshot (node, transport_, snapshot_msg_size);
    perf::multi::emit_spot_node_auto_hwm_snapshot (control_node, transport_, snapshot_msg_size);

    const int base_port = settings.server_bind_port > 0 ? settings.server_bind_port : 39500 + (bench_pid () % 1000) * 8;
    const std::string endpoint = perf::multi::bind_spot_endpoint (node, transport_, base_port);
    if (endpoint.empty ())
        return false;
    const std::string control_endpoint = perf::multi::bind_spot_endpoint (control_node, transport_, base_port + 256);
    if (control_endpoint.empty ())
        return false;

    const int control_timeout_ms = std::max (1000, settings.connect_ready_timeout_ms);
    control_pub.request_timeout (std::chrono::milliseconds (control_timeout_ms));
    control_sub.request_timeout (std::chrono::milliseconds (control_timeout_ms));
    control_sub.set_subscription (k_control_topic);

    perf::multi::print_ready (endpoint);
    std::cout << "CONTROL_READY," << control_endpoint << std::endl;

    perf::multi::reset_start_signal_state (&g_start_gate);
    perf::multi::reset_control_connect_gate (&g_control_connect_gate);

    const int start_timeout_ms = resolve_spot_start_timeout_ms (settings);
    debug_log ("waiting control reverse connect");
    if (!wait_for_control_connect (control_node, start_timeout_ms))
        return false;

    uint64_t seq = 1;
    if (!perf::multi::run_spot_server_cases (
          settings, msg_sizes,
          [&] (size_t current_size, int timeout_ms) {
              debug_log ("waiting stdin START size=" + std::to_string (current_size));
              const bool ok = wait_for_start_signal (current_size, timeout_ms);
              if (ok)
                  debug_log ("stdin START received size=" + std::to_string (current_size));
              return ok;
          },
          [&] (size_t current_size) {
              const int barrier_timeout_ms = resolve_spot_barrier_timeout_ms (settings, transport_);
              debug_log ("waiting ready count barrier size=" + std::to_string (current_size)
                         + " expected=" + std::to_string (settings.clients));
              return perf::multi::wait_for_ready_counts (
                control_sub, control_channel_name, k_control_topic, current_size,
                std::max<size_t> (1, settings.clients), barrier_timeout_ms,
                [] (const std::string &payload, size_t *ready_size, size_t *increment) {
                    return perf::multi::parse_size_count_command_line (payload, "READY_COUNT,", ready_size, increment);
                });
          },
          [&] (size_t current_size) {
              const int barrier_timeout_ms = resolve_spot_barrier_timeout_ms (settings, transport_);
              return perf::multi::publish_control_payload (
                control_pub, k_control_topic, perf::multi::make_start_command (current_size), barrier_timeout_ms);
          },
          [&] (size_t current_size, perf_metric::phase_t phase, std::chrono::steady_clock::duration duration) {
              return run_phase (spot, &send_poller, spot_channel_name, current_size, seq, phase, duration);
          })) {
        return false;
    }
    fast_exit_process (0);
    return false;
}

int main (int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "usage: <lib_name> <transport> <size>" << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    if (size == 0)
        return 1;

    perf::multi::reset_start_signal_state (&g_start_gate);
    perf::multi::reset_control_connect_gate (&g_control_connect_gate);
    perf::multi::start_server_control_watcher (&g_control_connect_gate, &g_start_gate);

    return perf_spot_server (lib_name, transport, size) ? 0 : 1;
}
