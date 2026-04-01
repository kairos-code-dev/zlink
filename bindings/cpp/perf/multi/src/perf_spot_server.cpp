// MULTI_SPOT server benchmark: one-way SPOT publisher source.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

int bench_pid ()
{
#if defined(_WIN32)
    return _getpid ();
#else
    return getpid ();
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
        const std::string endpoint =
          make_transport_endpoint (transport_, base_port_ + i);
        if (node_.bind (endpoint) == 0)
            return endpoint;
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

bool wait_for_service_ready_count (zlink::service_monitor_handle_t &monitor_,
                                   uint32_t event_type_,
                                   uint64_t min_value_,
                                   int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (timeout_ms_, 1000));

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
            if (errno == EINTR)
                continue;
            return false;
        }
        if (poll_rc == 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        const zlink::maybe_t<zlink_service_monitor_event_t> event =
          monitor_.try_receive ();
        if (!event)
            continue;
        if (event->event_type
            == static_cast<uint32_t> (zlink::service_monitor_event::error)) {
            errno = event->error_code != 0 ? event->error_code : EIO;
            return false;
        }
        if (event->event_type != event_type_)
            continue;
        if (event->value >= min_value_)
            return true;
    }

    errno = ETIMEDOUT;
    return false;
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

        const int rc = spot_.publish (
          k_topic, payload_.data (), payload_.size (), zlink::send_flag::dontwait);
        if (rc == 0) {
            continue;
        }

        if (errno == EINTR)
            continue;
        if (errno != EAGAIN)
            return false;
        std::this_thread::yield ();
    }

    return true;
}

} // namespace

bool perf_spot_server (const std::string &transport_, size_t msg_size_)
{
    perf::multi::set_perf_pattern_env ("SPOT");

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

    zlink::service_monitor_handle_t monitor (
      spot.handle (),
      zlink::service_monitor_event::spot_first_delivery_ready_changed
        | zlink::service_monitor_event::error);
    if (!monitor.valid ())
        return false;

    perf::multi::print_ready (endpoint);

    if (!wait_for_service_ready_count (
          monitor,
          static_cast<uint32_t> (
            zlink::service_monitor_event::spot_first_delivery_ready_changed),
          static_cast<uint64_t> (std::max<size_t> (1, settings.clients)),
          settings.connect_ready_timeout_ms)) {
        if (std::getenv ("PERF_DEBUG") != NULL)
            std::cerr << "spot server: pub ready gate timed out errno=" << errno
                      << std::endl;
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
                    perf_metric::phase_drain,
                    std::chrono::milliseconds (std::max (0, settings.settle_ms)))) {
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
    return true;
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

    return perf_spot_server (transport, size) ? 0 : 1;
}
