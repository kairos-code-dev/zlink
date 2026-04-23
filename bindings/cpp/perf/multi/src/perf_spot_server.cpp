// MULTI_SPOT server benchmark: one-way SPOT publisher source.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"
#include "../common/perf_spot_phase.hpp"

#include <zlink_c.h>

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

bool wait_for_spot_send_progress (bool send_enabled_)
{
    return perf_idle_wait_ms (send_enabled_ ? 2 : 1) >= 0;
}

bool wait_for_spot_control_progress ()
{
    return perf_idle_wait_ms (1) >= 0;
}

void close_raw_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        (void) zlink_msg_close (&parts_[i]);
}

bool control_topic_matches (const char *actual_topic_,
                            size_t actual_topic_len_,
                            const char *expected_topic_)
{
    if (!actual_topic_ || !expected_topic_ || !*expected_topic_)
        return false;

    const size_t expected_len = std::strlen (expected_topic_);
    if (actual_topic_len_ > 0 && actual_topic_[actual_topic_len_ - 1] == '\0')
        --actual_topic_len_;
    return actual_topic_len_ == expected_len
           && std::memcmp (actual_topic_, expected_topic_, expected_len) == 0;
}

bool recv_raw_control_payload (zlink::service::spot_t &spot_,
                               const char *service_name_,
                               std::string *payload_out_,
                               bool *received_out_)
{
    if (received_out_)
        *received_out_ = false;
    if (payload_out_)
        payload_out_->clear ();

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof (topic) - 1;
    const zlink_recv_result_t rc = zlink_subscribe (
      spot_.handle (),
      NULL,
      &parts,
      &part_count,
      topic,
      &topic_len,
      ZLINK_RECV_FLAGS_DONTWAIT);
    if (rc != ZLINK_RECV_OK) {
        const int err = zlink_errno () != 0 ? zlink_errno () : errno;
        if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
            || err == ETIMEDOUT) {
            return true;
        }
        errno = err;
        return false;
    }

    if (received_out_)
        *received_out_ = true;
    (void) service_name_;
    if (payload_out_ && control_topic_matches (topic, topic_len, k_control_topic)
        && part_count > 0) {
        payload_out_->assign (
          static_cast<const char *> (zlink_msg_data (&parts[0])),
          zlink_msg_size (&parts[0]));
    }
    close_raw_parts (parts, part_count);
    return true;
}

bool publish_raw_control_payload (zlink::service::spot_t &spot_,
                                  const char *service_name_,
                                  const std::string &payload_,
                                  int timeout_ms_)
{
    (void) service_name_;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_msg_t part;
        if (zlink_msg_init_size (&part, payload_.size ()) != 0)
            return false;
        if (!payload_.empty ())
            std::memcpy (zlink_msg_data (&part), payload_.data (), payload_.size ());

        const zlink_submit_result_t rc = zlink_publish (
          spot_.handle (),
          k_control_topic,
          &part,
          1,
          ZLINK_SEND_FLAGS_NONE);
        const int saved_errno = rc == ZLINK_SUBMIT_OK ? 0 : errno;
        if (rc == ZLINK_SUBMIT_OK)
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

void ensure_multi_spot_mesh_pub_budget_default ()
{
    const char *existing = std::getenv ("ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM");
    if (existing && *existing)
        return;

#if defined(_WIN32)
    _putenv_s ("ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM", "100");
#else
    setenv ("ZLINK_SPOT_INTERNAL_MESH_PUB_SNDHWM", "100", 1);
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
                              const std::string &service_name_,
                              const std::string &payload_,
                              int timeout_ms_)
{
    (void) service_name_;
    return publish_raw_control_payload (
      spot_, service_name_.c_str (), payload_, timeout_ms_);
}

bool publish_control_start (zlink::service::spot_t &spot_,
                            const std::string &service_name_,
                            size_t msg_size_,
                            int timeout_ms_)
{
    return publish_control_message (
      spot_,
      service_name_,
      perf::multi::make_start_command (msg_size_),
      timeout_ms_);
}

bool wait_for_ready_counts (zlink::service::spot_t &spot_,
                            const std::string &service_name_,
                            size_t msg_size_,
                            size_t expected_ready_count_,
                            int timeout_ms_)
{
    (void) service_name_;
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
              spot_, service_name_.c_str (), &payload, &received))
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
                const std::string &service_name_,
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

    while (std::chrono::steady_clock::now () < deadline) {
        (void) service_name_;
        const auto publish_once =
          [&](zlink_send_flags_t flags_, int *saved_errno_out_) -> int {
              if (!saved_errno_out_) {
                  errno = EFAULT;
                  return -1;
              }

              zlink_msg_t part;
              if (zlink_msg_init_size (&part, payload_size) != 0) {
                  *saved_errno_out_ = errno;
                  return -1;
              }
              if (!perf_metric::stamp_payload (zlink_msg_data (&part),
                                               zlink_msg_size (&part),
                                               k_run_id,
                                               phase_,
                                               msg_size_,
                                               seq_,
                                               perf_metric::now_ns ())) {
                  const int stamp_errno = errno != 0 ? errno : EFAULT;
                  (void) zlink_msg_close (&part);
                  *saved_errno_out_ = stamp_errno;
                  errno = stamp_errno;
                  return -1;
              }

              const zlink_submit_result_t rc = zlink_publish (
                spot_.handle (), k_topic, &part, 1, flags_);
              *saved_errno_out_ = rc == ZLINK_SUBMIT_OK ? 0 : errno;
              return rc == ZLINK_SUBMIT_OK ? 0 : -1;
          };

        int saved_errno = 0;
        int rc = publish_once (ZLINK_SEND_FLAGS_DONTWAIT, &saved_errno);
        if (rc != 0 && saved_errno == EAGAIN)
            rc = publish_once (ZLINK_SEND_FLAGS_NONE, &saved_errno);

        if (rc == 0) {
            ++seq_;
            continue;
        }

        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK
            && saved_errno != ETIMEDOUT) {
            errno = saved_errno != 0 ? saved_errno : EFAULT;
            return false;
        }
        if (!wait_for_spot_send_progress (true))
            return false;
    }

    return true;
}

} // namespace

bool perf_spot_server (const std::string &lib_name,
                       const std::string &transport_,
                       size_t msg_size_)
{
    perf::multi::set_perf_pattern_env ("SPOT");
    ensure_multi_spot_mesh_pub_budget_default ();

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

    perf::multi::ctx_guard_t ctx;
    zlink::service::spot_node_t node (ctx.ctx ());
    if (!node.valid ())
        return false;

    zlink::service::spot_node_t control_node (ctx.ctx ());
    if (!control_node.valid ())
        return false;

    const std::string spot_service_name = "spot-bench";
    const std::string control_service_name = "spot-control";
    if (!perf::multi::configure_spot_server_tls (node, transport_))
        return false;
    if (!perf::multi::configure_spot_control_tls (control_node, transport_))
        return false;

    zlink::service::spot_t spot = node.create_spot ();
    if (!spot.valid ())
        return false;
    zlink::service::spot_t control_pub = control_node.create_spot ();
    zlink::service::spot_t control_sub = control_node.create_spot ();
    if (!control_pub.valid () || !control_sub.valid ())
        return false;

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

    spot.options ().send_hwm (settings.sndhwm);
    spot.options ().send_timeout (settings.sndtimeo_ms);
    spot.publisher_options ().no_drop (
      perf::multi::parse_positive_env ("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0);
    const int control_timeout_ms =
      std::max (1000, settings.connect_ready_timeout_ms);
    const int control_hwm =
      std::max (1024, static_cast<int> (settings.clients * 8));
    control_pub.options ().linger (0);
    control_pub.options ().send_hwm (control_hwm);
    control_pub.options ().send_timeout (control_timeout_ms);
    control_pub.publisher_options ().no_drop (true);
    control_sub.options ().linger (0);
    control_sub.options ().recv_hwm (control_hwm);
    control_sub.options ().recv_timeout (control_timeout_ms);
    control_sub.set_subscription (k_control_topic);

    const std::vector<size_t> msg_sizes (1, msg_size_);

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
                control_service_name,
                current_size,
                std::max<size_t>(1, settings.clients),
                barrier_timeout_ms);
          },
              [&](size_t current_size) {
              const int barrier_timeout_ms =
                resolve_spot_barrier_timeout_ms(settings, transport_);
              return publish_control_start(control_pub,
                                           control_service_name,
                                           current_size,
                                           barrier_timeout_ms);
          },
          [&](size_t current_size,
              perf_metric::phase_t phase,
              std::chrono::steady_clock::duration duration) {
              const bench_multi_cpu_sample_t resource_probe_start =
                perf::multi::start_resource_probe();
              const bool ok = run_phase(spot,
                                        spot_service_name,
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
