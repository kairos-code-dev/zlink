// MULTI_SPOT_REQREP server benchmark: router-based echo responder for
// spot channel requests.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_SPOT_REQREP";

bool is_supported_transport (const std::string &transport_)
{
    return transport_ == "tcp" || transport_ == "tls" || transport_ == "ws"
           || transport_ == "wss";
}

template<typename SocketLike>
void apply_socket_options (SocketLike &socket_,
                           const perf::multi::multi_bench_settings_t &settings_,
                           size_t msg_size_)
{
    zlink::common_socket_options_t options = socket_.options ();
    if (perf::multi::manual_socket_overrides_enabled ()) {
        options.send_hwm (
          zlink::message_count_t::value (settings_.sndhwm > 0 ? settings_.sndhwm : 1));
        options.recv_hwm (
          zlink::message_count_t::value (settings_.rcvhwm > 0 ? settings_.rcvhwm : 1));
    }
    options.send_timeout (std::chrono::milliseconds (settings_.sndtimeo_ms));
    options.recv_timeout (std::chrono::milliseconds (settings_.rcvtimeo_ms));
    options.linger (std::chrono::milliseconds (0));
    options.auto_hwm_msg_unit_bytes (
      zlink::byte_size_t::bytes (static_cast<int64_t> (msg_size_)));
}

template<typename SocketLike>
std::string bind_and_resolve_endpoint (SocketLike &socket_,
                                       const std::string &transport_,
                                       int fixed_port_)
{
    const std::string endpoint =
      perf::multi::make_endpoint (transport_, "cpp_multi_spot_reqrep", fixed_port_);
    try {
        socket_.bind (endpoint);
    }
    catch (const zlink::zlink_error_t &) {
        return std::string ();
    }

    return perf::multi::normalize_endpoint_host (
      socket_.options ().last_endpoint ());
}

zlink::message_t clone_message (const zlink::message_t &source_)
{
    return zlink::message_t::from_bytes (source_.data (), source_.size ());
}

bool reply_with_payload (const zlink::received_t &received_)
{
    if (received_.parts ().empty ()) {
        zlink::message_t reply (0);
        if (!reply.valid ())
            return false;
        received_.reply (reply);
        return true;
    }

    std::vector<zlink::message_t> reply_parts;
    reply_parts.reserve (received_.parts ().size ());
    for (size_t i = 0; i < received_.parts ().size (); ++i) {
        zlink::message_t reply = clone_message (received_.parts ()[i]);
        if (!reply.valid ())
            return false;
        reply_parts.push_back (std::move (reply));
    }
    received_.reply (reply_parts);
    return true;
}

bool stdin_stop_thread (std::atomic<bool> &stop_requested_)
{
    std::string line;
    while (std::getline (std::cin, line)) {
        if (line == "STOP") {
            stop_requested_.store (true, std::memory_order_release);
            return true;
        }
    }
    return true;
}

} // namespace

bool perf_spot_reqrep_server (const std::string &lib_name,
                              const std::string &transport,
                              size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("SPOT_REQREP");

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes =
      perf::multi::resolve_case_msg_sizes (msg_size);
    const size_t max_msg_size =
      perf::multi::max_case_msg_size (msg_sizes, msg_size);

    perf::multi::ctx_guard_t ctx;
    zlink::router_socket_t responder (ctx);
    if (!responder.valid ())
        return false;

    apply_socket_options (responder, settings, max_msg_size);
    (void) perf::multi::recalculate_auto_hwm (ctx);
    if (!perf::setup_tls_server (responder, transport))
        return false;

    const std::string endpoint =
      bind_and_resolve_endpoint (responder, transport, settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    const bench_multi_cpu_sample_t resource_probe_start =
      perf::multi::start_resource_probe ();
    perf::multi::print_ready (endpoint);

    std::atomic<bool> stop_requested (false);
    std::thread stop_thread (stdin_stop_thread, std::ref (stop_requested));

    zlink::poller_t poller;
    try {
        poller.add (responder, zlink::poll_event_flag_t::pollin);
    }
    catch (const zlink::zlink_error_t &) {
        stop_requested.store (true, std::memory_order_release);
        stop_thread.join ();
        return false;
    }

    bool failed = false;
    while (!stop_requested.load (std::memory_order_acquire)) {
        std::optional<zlink::poll_event_t> event =
          poller.wait (std::chrono::milliseconds (20));
        const int poll_rc = event ? 1 : 0;
        if (poll_rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            failed = true;
            break;
        }
        if (poll_rc == 0
            || (static_cast<short> (event->revents) & static_cast<short> (zlink::poll_event_flag_t::pollin))
                 == 0) {
            continue;
        }

        for (;;) {
            try {
                zlink::received_t received;
                const int rc = responder.recv (
                  received, zlink::recv_flags_t::dontwait);
                if (rc != 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    failed = true;
                    stop_requested.store (true, std::memory_order_release);
                    break;
                }
                if (!reply_with_payload (received)) {
                    failed = true;
                    stop_requested.store (true, std::memory_order_release);
                    break;
                }
            }
            catch (const zlink::recv_error_t &err) {
                if (err.result () == zlink::recv_result_t::no_data
                    || err.result () == zlink::recv_result_t::busy) {
                    break;
                }
                failed = true;
                stop_requested.store (true, std::memory_order_release);
                break;
            }
            catch (const zlink::zlink_error_t &) {
                failed = true;
                stop_requested.store (true, std::memory_order_release);
                break;
            }
        }
    }

    if (!stop_requested.load (std::memory_order_acquire))
        stop_requested.store (true, std::memory_order_release);
    stop_thread.join ();

    const bench_multi_resource_metrics_t resource_metrics =
      perf::multi::finish_resource_probe (resource_probe_start);
    perf::multi::print_server_resource_metrics (
      lib_name, k_pattern, transport, msg_size, resource_metrics);
    perf::multi::print_server_queue_metrics (
      lib_name,
      k_pattern,
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
    return !failed;
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

    const bool ok = perf_spot_reqrep_server (lib_name, transport, size);
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (ok ? 0 : 1);
}
