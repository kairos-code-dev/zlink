// MULTI_SPOT server benchmark: one-way SPOT publisher source.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"

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
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

static const char *k_pattern = "MULTI_SPOT";
static const char *k_topic = "bench";
static const uint32_t k_run_id = 1U;

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "spot server: " << message_ << std::endl;
}

struct start_gate_t
{
    start_gate_t () : requested (false), msg_size (0) {}

    bool requested;
    size_t msg_size;
    std::mutex mutex;
    std::condition_variable cv;
};

start_gate_t g_start_gate;

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

std::string make_transport_endpoint (const std::string &transport_, int port_)
{
    const std::string suffix = std::to_string (port_);
    if (transport_ == "ws")
        return std::string ("ws://127.0.0.1:") + suffix;
    if (transport_ == "wss")
        return std::string ("wss://127.0.0.1:") + suffix;
    if (transport_ == "tls")
        return std::string ("tls://127.0.0.1:") + suffix;
    return std::string ("tcp://127.0.0.1:") + suffix;
}

std::string bind_spot_endpoint (zlink::service::spot_node_t &node_,
                                const std::string &transport_,
                                int base_port_)
{
    for (int i = 0; i < 64; ++i) {
        const std::string requested_endpoint =
          make_transport_endpoint (transport_, base_port_ + i);
        if (node_.bind (requested_endpoint) != 0)
            continue;

        std::string resolved_endpoint = node_.last_endpoint ();
        if (!resolved_endpoint.empty ()) {
            return perf::multi::normalize_endpoint_host (resolved_endpoint);
        }

        return requested_endpoint;
    }
    return std::string ();
}

bool configure_spot_server_tls (zlink::service::spot_node_t &node_,
                                const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return node_.set_tls_server (cert, key) == 0;
}

bool open_spot_pub_ready_probe (zlink::service_monitor_handle_t *monitor_out_,
                                zlink::service::spot_node_t &node_)
{
    if (!monitor_out_)
        return false;

    zlink::service_monitor_handle_t monitor (
      node_.handle (),
      zlink::service_monitor_event::spot_pub_delivery_ready_changed
        | zlink::service_monitor_event::spot_first_delivery_ready_changed
        | zlink::service_monitor_event::error);
    if (!monitor.valid ())
        return false;
    perf::multi::configure_perf_monitor_socket (monitor.handle ());
    *monitor_out_ = std::move (monitor);
    return true;
}

bool wait_for_spot_pub_ready (zlink::service_monitor_handle_t &monitor_,
                              uint64_t min_value_,
                              int timeout_ms_)
{
    if (min_value_ == 0)
        return true;

    uint64_t ready_count = 0;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (timeout_ms_, 1));

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_pollitem_t item;
        item.socket = monitor_.handle ();
        item.fd = 0;
        item.events = ZLINK_POLLIN;
        item.revents = 0;

        const auto remaining =
          deadline - std::chrono::steady_clock::now ();
        int wait_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (wait_ms < 1)
            wait_ms = 1;

        const int poll_rc = zlink_poll (&item, 1, wait_ms);
        if (poll_rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return false;
        }
        if (poll_rc == 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            const zlink::maybe_t<zlink_service_monitor_event_t> event =
              monitor_.try_recv ();
            if (!event)
                break;

            if (event->event_type
                == static_cast<uint32_t> (
                  zlink::service_monitor_event::error)) {
                errno = event->error_code != 0 ? event->error_code : EIO;
                return false;
            }

            if (event->event_type
                    != static_cast<uint32_t> (
                      zlink::service_monitor_event::spot_pub_delivery_ready_changed)
                && event->event_type
                     != static_cast<uint32_t> (
                       zlink::service_monitor_event::spot_first_delivery_ready_changed)) {
                continue;
            }

            if ((event->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
                && std::strcmp (event->subject, k_topic) != 0) {
                continue;
            }

            ready_count = event->value;
            if (perf_debug_enabled ()) {
                const std::string subject =
                  (event->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) != 0
                    ? std::string (event->subject)
                    : std::string ("<none>");
                std::cerr << "spot server: ready event type="
                          << event->event_type << " value=" << event->value
                          << " subject=" << subject << std::endl;
            }
            if (ready_count >= min_value_)
                return true;
        }
    }

    errno = ETIMEDOUT;
    return false;
}

void debug_dump_spot_node_state (zlink::service::spot_node_t &node_)
{
    if (!perf_debug_enabled ())
        return;

    zlink_spot_node_status_t status;
    std::memset (&status, 0, sizeof (status));
    if (node_.status_snapshot (status) == 0) {
        std::cerr << "spot server: node status"
                  << " state=" << status.state
                  << " configured=" << status.configured_peer_count
                  << " active=" << status.active_peer_count
                  << " connected=" << status.connected_peer_count
                  << " subjects=" << status.subject_count
                  << " ready_subjects=" << status.ready_subject_count
                  << " last_error=" << status.last_error
                  << " endpoint=" << status.local_endpoint
                  << std::endl;
    } else {
        std::cerr << "spot server: node status snapshot failed errno="
                  << errno << std::endl;
    }

    std::vector<zlink_spot_node_peer_entry_t> peers (16);
    size_t peer_count = peers.size ();
    if (node_.peers_snapshot (peers.data (), &peer_count) == 0) {
        peers.resize (peer_count);
        std::cerr << "spot server: peer count=" << peers.size () << std::endl;
        for (size_t i = 0; i < peers.size (); ++i) {
            std::cerr << "spot server: peer[" << i << "] state="
                      << peers[i].state
                      << " source=" << peers[i].source
                      << " endpoint=" << peers[i].peer_endpoint
                      << std::endl;
        }
    } else {
        std::cerr << "spot server: peers snapshot failed errno=" << errno
                  << std::endl;
    }

    std::vector<zlink_spot_node_subject_entry_t> subjects (16);
    size_t subject_count = subjects.size ();
    if (node_.subjects_snapshot (subjects.data (), &subject_count) == 0) {
        subjects.resize (subject_count);
        std::cerr << "spot server: subject count=" << subjects.size ()
                  << std::endl;
        for (size_t i = 0; i < subjects.size (); ++i) {
            std::cerr << "spot server: subject[" << i << "] role="
                      << subjects[i].role
                      << " ready_peers=" << subjects[i].ready_peer_count
                      << " active_peers=" << subjects[i].active_peer_count
                      << " subject=" << subjects[i].subject << std::endl;
        }
    } else {
        std::cerr << "spot server: subjects snapshot failed errno=" << errno
                  << std::endl;
    }
}

bool parse_start_command (const std::string &line_, size_t *msg_size_out_)
{
    static const char prefix[] = "START,";
    if (!msg_size_out_
        || line_.compare (0, sizeof (prefix) - 1, prefix) != 0) {
        return false;
    }

    const char *value = line_.c_str () + (sizeof (prefix) - 1);
    char *end = NULL;
    const unsigned long long parsed = std::strtoull (value, &end, 10);
    if (!end || *end != '\0' || parsed == 0)
        return false;

    *msg_size_out_ = static_cast<size_t> (parsed);
    return true;
}

int resolve_spot_start_timeout_ms (const perf::multi::multi_bench_settings_t &settings_)
{
    return std::max (settings_.connect_ready_timeout_ms,
                     std::max (1000, settings_.connect_ready_timeout_ms * 6));
}

int resolve_spot_pub_ready_timeout_ms (const perf::multi::multi_bench_settings_t &settings_,
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
    std::unique_lock<std::mutex> lock (g_start_gate.mutex);
    if (g_start_gate.requested && g_start_gate.msg_size == msg_size_) {
        g_start_gate.requested = false;
        g_start_gate.msg_size = 0;
        return true;
    }

    const bool signaled = g_start_gate.cv.wait_for (
      lock,
      std::chrono::milliseconds (std::max (1, timeout_ms_)),
      [msg_size_]() {
          return g_start_gate.requested
                 && g_start_gate.msg_size == msg_size_;
      });
    if (!signaled) {
        errno = ETIMEDOUT;
        return false;
    }

    g_start_gate.requested = false;
    g_start_gate.msg_size = 0;
    return true;
}

size_t resolve_spot_ready_quorum (size_t client_count_)
{
    if (client_count_ == 0)
        return 1;

    const int percent = std::max (
      1,
      perf::multi::parse_positive_env (
        "PERF_MULTI_SPOT_PUB_READY_QUORUM_PERCENT", 90));
    const size_t bounded_percent =
      static_cast<size_t> (std::min (percent, 100));
    const size_t quorum = static_cast<size_t> (
      (static_cast<unsigned long long> (client_count_) * bounded_percent + 99ULL)
      / 100ULL);
    return std::max<size_t> (1, quorum);
}

bool run_phase (zlink::service::spot_t &spot_,
                std::vector<char> &payload_,
                size_t msg_size_,
                uint64_t &seq_,
                perf_metric::phase_t phase_,
                std::chrono::steady_clock::duration duration_)
{
    if (duration_ <= std::chrono::steady_clock::duration::zero ())
        return true;

    const auto deadline = std::chrono::steady_clock::now () + duration_;

    while (std::chrono::steady_clock::now () < deadline) {
        if (!perf_metric::stamp_payload (payload_.data (),
                                         payload_.size (),
                                         k_run_id,
                                         phase_,
                                         msg_size_,
                                         seq_++,
                                         perf_metric::now_us ())) {
            errno = EINVAL;
            return false;
        }

        zlink::message_t outbound =
          zlink::message_t::from_bytes (payload_.data (), payload_.size ());
        if (!outbound.valid ()) {
            errno = EINVAL;
            return false;
        }

        const zlink::send_result_t result =
          spot_.try_publish (k_topic, outbound);
        if (result == zlink::send_result_t::sent) {
            continue;
        }

        if (result != zlink::send_result_t::backpressured
            && result != zlink::send_result_t::not_ready) {
            errno = EFAULT;
            return false;
        }
        std::this_thread::yield ();
    }

    return true;
}

} // namespace

bool perf_spot_server (const std::string &transport_, size_t msg_size_)
{
    perf::multi::set_perf_pattern_env ("SPOT");
    ensure_multi_spot_mesh_pub_budget_default ();

    if (!perf::multi::multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return false;

    if (!perf::multi::is_supported_transport (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_ << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();

    perf::multi::ctx_guard_t ctx;
    zlink::service::spot_node_t node (ctx.ctx ());
    if (!node.valid ())
        return false;

    zlink::service::spot_t spot (node);
    if (!spot.valid ())
        return false;

    if (!configure_spot_server_tls (node, transport_))
        return false;

    const int base_port = settings.server_bind_port > 0
                            ? settings.server_bind_port
                            : 39500 + (bench_pid () % 1000) * 8;
    const std::string endpoint = bind_spot_endpoint (node, transport_, base_port);
    if (endpoint.empty ())
        return false;

    (void) spot.set (zlink::socket_options::sndhwm, settings.sndhwm);
    (void) spot.set (zlink::socket_options::sndtimeo, settings.sndtimeo_ms);
    (void) spot.set (zlink::pub_options::nodrop,
                     perf::multi::parse_positive_env (
                       "PERF_MULTI_SPOT_XPUB_NODROP", 1)
                       > 0
                       ? 1
                       : 0);

    zlink::service_monitor_handle_t monitor;
    if (!open_spot_pub_ready_probe (&monitor, node))
        return false;

    perf::multi::print_ready (endpoint);

    {
        std::lock_guard<std::mutex> lock (g_start_gate.mutex);
        g_start_gate.requested = false;
        g_start_gate.msg_size = 0;
    }

    const int start_timeout_ms = resolve_spot_start_timeout_ms (settings);
    if (!wait_for_start_signal (msg_size_, start_timeout_ms))
        return false;

    const size_t ready_quorum = resolve_spot_ready_quorum (settings.clients);
    const int ready_timeout_ms =
      resolve_spot_pub_ready_timeout_ms (settings, transport_);
    if (!wait_for_spot_pub_ready (
          monitor, static_cast<uint64_t> (ready_quorum), ready_timeout_ms)) {
        debug_log ("pub ready gate timed out errno=" + std::to_string (errno));
        debug_dump_spot_node_state (node);
        return false;
    }

    std::vector<char> payload (
      std::max<size_t> (msg_size_, perf_metric::header_size ()), 's');
    uint64_t seq = 1;

    if (!run_phase (spot,
                    payload,
                    msg_size_,
                    seq,
                    perf_metric::phase_warmup,
                    std::chrono::seconds (std::max (0, settings.warmup_seconds)))) {
        return false;
    }
    if (!run_phase (spot,
                    payload,
                    msg_size_,
                    seq,
                    perf_metric::phase_active,
                    std::chrono::seconds (std::max (1, settings.duration_seconds)))) {
        return false;
    }

    perf::multi::print_server_queue_metrics (
      "current", k_pattern, transport_, msg_size_, perf::multi::server_queue_stats_t ());
    fast_exit_process (0);
    return false;
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size>" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    if (size == 0)
        return 1;

    {
        std::lock_guard<std::mutex> lock (g_start_gate.mutex);
        g_start_gate.requested = false;
        g_start_gate.msg_size = 0;
    }

    std::thread stdin_watcher ([]() {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t start_size = 0;
            if (parse_start_command (line, &start_size)) {
                std::lock_guard<std::mutex> lock (g_start_gate.mutex);
                g_start_gate.requested = true;
                g_start_gate.msg_size = start_size;
                g_start_gate.cv.notify_all ();
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                std::lock_guard<std::mutex> lock (g_start_gate.mutex);
                g_start_gate.requested = false;
                g_start_gate.msg_size = 0;
                g_start_gate.cv.notify_all ();
                return;
            }
        }
    });
    stdin_watcher.detach ();

    return perf_spot_server (transport, size) ? 0 : 1;
}
