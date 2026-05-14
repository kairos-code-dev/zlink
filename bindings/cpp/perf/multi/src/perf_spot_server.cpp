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
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <process.h>
#else
#include <climits>
#include <poll.h>
#include <unistd.h>
#endif

namespace {

static const char *k_pattern = "MULTI_SPOT";
static const char *k_topic = "bench";
static const char *k_control_topic = "bench_ctl";
static const uint32_t k_run_id = 1U;

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

int perf_idle_wait_ms (long timeout_ms_)
{
#if defined(_WIN32)
    const DWORD wait_ms = timeout_ms_ <= 0
                            ? 0
                            : static_cast<DWORD> (timeout_ms_);
    ::Sleep (wait_ms);
    return 0;
#else
    const int wait_ms =
      timeout_ms_ > static_cast<long> (INT_MAX) ? INT_MAX
                                                : static_cast<int> (timeout_ms_);
    int rc = 0;
    do {
        rc = ::poll (NULL, 0, wait_ms);
    } while (rc < 0 && errno == EINTR);
    return rc < 0 ? -1 : 0;
#endif
}

bool wait_for_spot_send_progress (zlink::poller_t *poller_, bool send_enabled_)
{
    if (send_enabled_ && poller_) {
        try {
            (void) poller_->wait (std::chrono::milliseconds (-1));
            return true;
        }
        catch (const zlink::zlink_error_t &) {
            return false;
        }
    }
    return !send_enabled_;
}

bool wait_for_spot_control_progress ()
{
    return perf_idle_wait_ms (1) >= 0;
}

bool recv_raw_control_payload (zlink::service::spot_t &spot_,
                               const char *channel_name_,
                               std::string *payload_out_,
                               bool *received_out_)
{
    if (received_out_)
        *received_out_ = false;
    if (payload_out_)
        payload_out_->clear ();

    try {
        const std::optional<zlink::topic_message_t> received =
          spot_.subscribe (ZLINK_DONTWAIT);
        if (!received.has_value ())
            return true;

        if (received_out_)
            *received_out_ = true;
        (void) channel_name_;
        if (payload_out_ && received->topic () == k_control_topic
            && !received->parts ().empty ()) {
            const zlink::message_t &part = received->parts ()[0];
            payload_out_->assign (
              static_cast<const char *> (part.data ()), part.size ());
        }
        return true;
    }
    catch (const zlink::recv_error_t &ex) {
        const int err = ex.internal_errno () != 0 ? ex.internal_errno () : errno;
        if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
            || err == ETIMEDOUT) {
            return true;
        }
        errno = err;
        return false;
    }
}

bool publish_raw_control_payload (zlink::service::spot_t &spot_,
                                  const char *channel_name_,
                                  const std::string &payload_,
                                  int timeout_ms_)
{
    (void) channel_name_;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t part (payload_.size ());
        if (!part.valid ())
            return false;
        if (!payload_.empty ())
            std::memcpy (part.data (), payload_.data (), payload_.size ());

        try {
            if (spot_.publish (k_control_topic)
                  .message (part)
                  .submit ())
                return true;
        }
        catch (const zlink::submit_error_t &err) {
            errno = err.internal_errno ();
        }
        const int saved_errno = errno;
        if (!part.valid ())
            part.close ();
        if (saved_errno == 0)
            return true;
        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK
            && saved_errno != ETIMEDOUT) {
            errno = saved_errno;
            return false;
        }
        if (!wait_for_spot_control_progress ())
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "spot server: " << message_ << std::endl;
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
    return std::max (settings_.connect_ready_timeout_ms,
                     std::max (1000, settings_.connect_ready_timeout_ms * 6));
}

int resolve_spot_barrier_timeout_ms (
  const perf::multi::multi_bench_settings_t &settings_,
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

bool wait_for_control_connect (zlink::service::spot_node_t &control_node_,
                               int timeout_ms_)
{
    std::string endpoint;
    if (!perf::multi::wait_for_control_connect (
          &g_control_connect_gate, control_node_, timeout_ms_, &endpoint)) {
        return false;
    }
    std::cout << "CONTROL_CONNECTED," << endpoint << std::endl;
    return true;
}

bool parse_ready_count_command (const std::string &line_,
                                size_t *msg_size_out_,
                                size_t *ready_count_out_)
{
    return perf::multi::parse_size_count_command_line (
      line_, "READY_COUNT,", msg_size_out_, ready_count_out_);
}

bool publish_control_message (zlink::service::spot_t &spot_,
                              const std::string &channel_name_,
                              const std::string &payload_,
                              int timeout_ms_)
{
    (void) channel_name_;
    return publish_raw_control_payload (
      spot_, channel_name_.c_str (), payload_, timeout_ms_);
}

bool publish_control_start (zlink::service::spot_t &spot_,
                            const std::string &channel_name_,
                            size_t msg_size_,
                            int timeout_ms_)
{
    return publish_control_message (
      spot_,
      channel_name_,
      perf::multi::make_start_command (msg_size_),
      timeout_ms_);
}

bool wait_for_ready_counts (zlink::service::spot_t &spot_,
                            const std::string &channel_name_,
                            size_t msg_size_,
                            size_t expected_ready_count_,
                            int timeout_ms_)
{
    (void) channel_name_;
    if (msg_size_ == 0 || expected_ready_count_ == 0) {
        errno = EINVAL;
        return false;
    }

    size_t ready_count = 0;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        bool received = false;
        std::string payload;
        if (!recv_raw_control_payload (
              spot_, channel_name_.c_str (), &payload, &received))
            return false;
        if (received && !payload.empty ()) {
            debug_log ("control recv payload=" + payload);
            size_t ready_size = 0;
            size_t increment = 0;
            if (parse_ready_count_command (payload, &ready_size, &increment)
                && ready_size == msg_size_) {
                ready_count += increment;
                if (ready_count >= expected_ready_count_)
                    return true;
            }
        }
        if (!wait_for_spot_control_progress ())
            return false;
    }

    errno = ETIMEDOUT;
    return false;
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
    const size_t payload_size =
      std::max<size_t> (msg_size_, perf_metric::header_size ());
    zlink::message_t outbound (payload_size);
    if (!outbound.valid ()) {
        errno = EINVAL;
        return false;
    }
    unsigned long long publish_ok_count = 0;
    unsigned long long publish_blocked_count = 0;
    unsigned long long publish_wait_count = 0;

    while (std::chrono::steady_clock::now () < deadline) {
        (void) channel_name_;
        const auto publish_once =
          [&](zlink::send_flags_t flags_, int *saved_errno_out_) -> int {
              if (!saved_errno_out_) {
                  errno = EFAULT;
                  return -1;
              }

              zlink::message_t part (payload_size);
              if (!part.valid ()) {
                  *saved_errno_out_ = errno;
                  return -1;
              }
              if (!perf_metric::stamp_payload (part.data (),
                                               part.size (),
                                               k_run_id,
                                               phase_,
                                               msg_size_,
                                               seq_,
                                               perf_metric::now_ns ())) {
                  const int stamp_errno = errno != 0 ? errno : EFAULT;
                  part.close ();
                  *saved_errno_out_ = stamp_errno;
                  errno = stamp_errno;
                  return -1;
              }

              try {
                  const bool ok = spot_.publish (k_topic)
                    .message (part)
                    .flags (static_cast<int> (flags_))
                    .submit ();
                  *saved_errno_out_ = ok ? 0 : EAGAIN;
                  return ok ? 0 : -1;
              }
              catch (const zlink::submit_error_t &) {
                  *saved_errno_out_ = errno;
                  return -1;
              }
          };

        int saved_errno = 0;
        const int rc = publish_once (ZLINK_DONTWAIT, &saved_errno);

        if (rc == 0) {
            ++publish_ok_count;
            ++seq_;
            continue;
        }

        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK
            && saved_errno != ETIMEDOUT) {
            errno = saved_errno != 0 ? saved_errno : EFAULT;
            return false;
        }
        ++publish_blocked_count;
        ++publish_wait_count;
        if (!wait_for_spot_send_progress (send_poller_, true))
            return false;
    }

    if (std::getenv ("PERF_DEBUG_TRANSITIONS") != NULL) {
        std::cerr << "[multi-spot-server] phase done size=" << msg_size_
                  << " phase=" << static_cast<int> (phase_)
                  << " ok=" << publish_ok_count
                  << " blocked=" << publish_blocked_count
                  << " wait=" << publish_wait_count << std::endl;
    }

    return true;
}

} // namespace

bool perf_spot_server (const std::string &lib_name,
                       const std::string &transport_,
                       size_t msg_size_)
{
    perf::multi::set_perf_pattern_env ("SPOT");

    if (!perf::multi::validate_multi_perf_pattern (k_pattern))
        return false;

    if (!perf::multi::is_supported_transport (transport_)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport_
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes =
      perf::multi::resolve_case_msg_sizes (msg_size_);

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

    const int control_hwm =
      std::max (1024, static_cast<int> (settings.clients * 8));
    if (!perf::multi::apply_spot_node_admission_hwm (
          node, settings.sndhwm, settings.rcvhwm)
        || !perf::multi::apply_spot_node_admission_hwm (
          control_node, control_hwm, control_hwm))
        return false;

    zlink::service::spot_t spot = node.create_spot ();
    if (!spot.valid ())
        return false;
    zlink::poller_t send_poller;
    try {
        send_poller.add (spot, zlink::poll_event_flag_t::pollout);
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }
    zlink::service::spot_t control_pub = control_node.create_spot ();
    zlink::service::spot_t control_sub = control_node.create_spot ();
    if (!control_pub.valid () || !control_sub.valid ())
        return false;
    if (!perf::multi::recalculate_auto_hwm (ctx))
        return false;
    const size_t snapshot_msg_size = msg_sizes.empty () ? msg_size_ : msg_sizes[0];
    perf::multi::emit_spot_node_auto_hwm_snapshot (
      node, transport_, snapshot_msg_size);
    perf::multi::emit_spot_node_auto_hwm_snapshot (
      control_node, transport_, snapshot_msg_size);

    const int base_port = settings.server_bind_port > 0
                            ? settings.server_bind_port
                            : 39500 + (bench_pid () % 1000) * 8;
    const std::string endpoint =
      perf::multi::bind_spot_endpoint (node, transport_, base_port);
    if (endpoint.empty ())
        return false;
    const std::string control_endpoint =
      perf::multi::bind_spot_endpoint (control_node, transport_, base_port + 256);
    if (control_endpoint.empty ())
        return false;

    const int control_timeout_ms =
      std::max (1000, settings.connect_ready_timeout_ms);
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
    if (!perf::multi::run_spot_server_cases(
          settings,
          msg_sizes,
          [&](size_t current_size, int timeout_ms) {
              debug_log("waiting stdin START size=" + std::to_string(current_size));
              const bool ok = wait_for_start_signal(current_size, timeout_ms);
              if (ok)
                  debug_log("stdin START received size=" + std::to_string(current_size));
              return ok;
          },
          [&](size_t current_size) {
              const int barrier_timeout_ms =
                resolve_spot_barrier_timeout_ms(settings, transport_);
              debug_log("waiting ready count barrier size="
                        + std::to_string(current_size) + " expected="
                        + std::to_string(settings.clients));
              return wait_for_ready_counts(
                control_sub,
                control_channel_name,
                current_size,
                std::max<size_t>(1, settings.clients),
                barrier_timeout_ms);
          },
              [&](size_t current_size) {
              const int barrier_timeout_ms =
                resolve_spot_barrier_timeout_ms(settings, transport_);
              return publish_control_start(control_pub,
                                           control_channel_name,
                                           current_size,
                                           barrier_timeout_ms);
          },
          [&](size_t current_size,
              perf_metric::phase_t phase,
              std::chrono::steady_clock::duration duration) {
              const bench_multi_cpu_sample_t resource_probe_start =
                perf::multi::start_resource_probe();
              const bool ok = run_phase(spot,
                                        &send_poller,
                                        spot_channel_name,
                                        current_size,
                                        seq,
                                        phase,
                                        duration);
              if (ok) {
                  const bench_multi_resource_metrics_t resource_metrics =
                    perf::multi::finish_resource_probe(resource_probe_start);
                  perf::multi::print_server_resource_metrics(
                    lib_name, k_pattern, transport_, current_size,
                    resource_metrics);
                  perf::multi::print_server_queue_metrics(
                    lib_name,
                    k_pattern,
                    transport_,
                    current_size,
                    perf::multi::server_queue_stats_t());
              }
              return ok;
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
    perf::multi::start_server_control_watcher (&g_control_connect_gate,
                                               &g_start_gate);

    return perf_spot_server (lib_name, transport, size) ? 0 : 1;
}
