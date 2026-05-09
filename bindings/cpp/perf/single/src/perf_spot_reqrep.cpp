// SPOT_REQREP benchmark: single requester/replier echo workload.
// Topology: requester spot(channel dealer) <-> responder router(bind)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <thread>
#include <vector>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#endif

#if !defined(ZLINK_HAVE_WINDOWS)
#include <unistd.h>
#endif

namespace {

const char *const k_channel = "bench";
const char *const k_stop_token = "__zlink_perf_stop__";

unsigned current_process_id ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    return static_cast<unsigned> (_getpid ());
#else
    return static_cast<unsigned> (getpid ());
#endif
}

std::string make_reqrep_endpoint (const std::string &transport_)
{
    static unsigned counter = 0;
    const unsigned port = 34100u + ((current_process_id () % 1000u) * 20u)
                          + (++counter);
    return perf::single::make_fixed_endpoint (transport_, static_cast<int> (port));
}

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

bool is_supported_transport (const std::string &transport_)
{
    return transport_ == "tcp" || transport_ == "tls" || transport_ == "ws"
           || transport_ == "wss";
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

bool wait_connected (zlink::monitor_handle_t &bind_monitor_,
                     zlink::monitor_handle_t &connect_monitor_)
{
    return perf::wait_socket_monitor_event (
             bind_monitor_,
             static_cast<uint64_t> (zlink::monitor_event::connection_ready),
             10000)
           && perf::wait_socket_monitor_event (
             connect_monitor_,
             static_cast<uint64_t> (zlink::monitor_event::connection_ready),
             10000);
}

bool warmup_probe (zlink::service::spot_t &requester_,
                   size_t payload_size_,
                   size_t msg_size_)
{
    std::vector<char> payload (payload_size_, 'q');
    if (!perf_single_metric::stamp_payload (payload.data (),
                                            payload.size (),
                                            1U,
                                            perf_single_metric::phase_warmup,
                                            msg_size_,
                                            1,
                                            perf_single_metric::now_ns ())) {
        return false;
    }

    std::vector<zlink::message_t> request_parts;
    request_parts.push_back (
      zlink::message_t::from_bytes (payload.data (), payload.size ()));
    if (!request_parts.front ().valid ())
        return false;

    try {
        zlink::async_result_t<std::vector<zlink::message_t> > future =
          requester_.request_channel (k_channel)
            .message (request_parts.front ())
            .timeout (std::chrono::milliseconds (5000))
            .submit_async ();
        std::vector<zlink::message_t> reply_parts = future.get ();
        return !reply_parts.empty () && reply_parts.front ().valid ()
               && reply_parts.front ().size () == payload_size_;
    }
    catch (const std::exception &) {
        return false;
    }
}

bool run_pattern_spot_reqrep (const std::string &transport,
                              size_t msg_size,
                              const std::string &lib_name)
{
    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << ",SPOT_REQREP," << transport
                  << std::endl;
        return true;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ())
        return false;

    zlink::service::spot_node_t requester_node (ctx);
    zlink::service::spot_t requester = requester_node.create_spot ();
    zlink::router_socket_t responder (ctx);
    zlink::dealer_socket_t requester_dealer (ctx);
    if (!requester_node.valid () || !requester.valid () || !responder.valid ()
        || !requester_dealer.valid ()) {
        return false;
    }

    if (!perf::setup_tls_server (responder, transport)
        || !perf::setup_tls_client (requester_dealer, transport)) {
        return false;
    }

    if (!perf::single::apply_single_auto_hwm_msg_unit (
          requester_node, msg_size)
        || !perf::single::apply_single_auto_hwm_msg_unit (
          requester, msg_size)
        || !perf::single::apply_single_auto_hwm_msg_unit (
          responder, msg_size)
        || !perf::single::apply_single_auto_hwm_msg_unit (
          requester_dealer, msg_size)
        || !perf::single::recalculate_single_auto_hwm (ctx)) {
        return false;
    }

    perf::single::apply_single_hwm (responder);
    perf::single::apply_single_hwm (requester_dealer);

    zlink::monitor_handle_t responder_monitor = zlink::monitor_handle_t::open (
      responder, zlink::monitor_event::connection_ready);
    zlink::monitor_handle_t dealer_monitor = zlink::monitor_handle_t::open (
      requester_dealer, zlink::monitor_event::connection_ready);
    if (!responder_monitor.valid () || !dealer_monitor.valid ())
        return false;

    const std::string endpoint = make_reqrep_endpoint (transport);
    try {
        responder.bind (endpoint);
        requester_dealer.connect (endpoint);
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }

    perf::single::apply_single_benchmark_socket_options (responder, transport);
    perf::single::apply_single_benchmark_socket_options (
      requester_dealer, transport);
    if (!wait_connected (responder_monitor, dealer_monitor))
        return false;

    try {
        requester_node.attach_channel_dealer_manual (k_channel, requester_dealer);
    }
    catch (const zlink::zlink_error_t &) {
        return false;
    }

    std::atomic<bool> stop_requested (false);
    std::atomic<bool> responder_ok (true);
    std::thread responder_thread ([&]() {
        zlink::poller_t poller;
        try {
            poller.add (responder, zlink::poll_event_flag_t::pollin);
        }
        catch (const zlink::zlink_error_t &) {
            responder_ok.store (false, std::memory_order_release);
            return;
        }

        while (!stop_requested.load (std::memory_order_acquire)) {
            std::optional<zlink::poll_event_t> event =
              poller.wait (std::chrono::milliseconds (10));
        const int poll_rc = event ? 1 : 0;
            if (poll_rc < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                responder_ok.store (false, std::memory_order_release);
                return;
            }
            if (poll_rc == 0
                || (static_cast<short> (event->revents) & static_cast<short> (zlink::poll_event_flag_t::pollin))
                     == 0) {
                continue;
            }

            for (;;) {
                try {
                    std::optional<zlink::received_t> received =
                      responder.recv (ZLINK_DONTWAIT);
                    if (!received.has_value ())
                        break;

                    if (received->parts ().size () == 1
                        && received->parts ()[0].size ()
                             == std::strlen (k_stop_token)
                        && std::memcmp (received->parts ()[0].data (),
                                        k_stop_token,
                                        std::strlen (k_stop_token))
                             == 0) {
                        stop_requested.store (true, std::memory_order_release);
                        break;
                    }

                    if (!reply_with_payload (*received)) {
                        responder_ok.store (false, std::memory_order_release);
                        return;
                    }
                }
                catch (const zlink::recv_error_t &err) {
                    if (err.result () == zlink::recv_result_t::no_data
                        || err.result () == zlink::recv_result_t::busy) {
                        break;
                    }
                    responder_ok.store (false, std::memory_order_release);
                    return;
                }
                catch (const zlink::zlink_error_t &) {
                    responder_ok.store (false, std::memory_order_release);
                    return;
                }
            }
        }
    });

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    if (!warmup_probe (requester, payload_size, msg_size)) {
        stop_requested.store (true, std::memory_order_release);
        responder_thread.join ();
        if (perf_debug_enabled ())
            std::cerr << "spot_reqrep: warmup probe failed" << std::endl;
        return false;
    }

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());
    unsigned long long active_count = 0;
    uint64_t seq = 1;
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);

    while (std::chrono::steady_clock::now () < deadline) {
        std::vector<char> payload (payload_size, 'r');
        if (!perf_single_metric::stamp_payload (payload.data (),
                                                payload.size (),
                                                1U,
                                                perf_single_metric::phase_active,
                                                msg_size,
                                                seq++,
                                                perf_single_metric::now_ns ())) {
            responder_ok.store (false, std::memory_order_release);
            break;
        }

        std::vector<zlink::message_t> request_parts;
        request_parts.push_back (
          zlink::message_t::from_bytes (payload.data (), payload.size ()));
        if (!request_parts.front ().valid ()) {
            responder_ok.store (false, std::memory_order_release);
            break;
        }

        try {
            zlink::async_result_t<std::vector<zlink::message_t> > future =
              requester.request_channel (k_channel)
                .message (request_parts.front ())
                .timeout (std::chrono::milliseconds (2000))
                .submit_async ();
            std::vector<zlink::message_t> reply_parts = future.get ();
            if (reply_parts.size () != 1 || !reply_parts.front ().valid ()
                || reply_parts.front ().size () != payload_size) {
                responder_ok.store (false, std::memory_order_release);
                break;
            }

            perf_single_metric::header_t header;
            if (!perf_single_metric::decode_payload_header (
                  reply_parts.front ().data (),
                  reply_parts.front ().size (),
                  &header)
                || !perf_single_metric::is_expected (
                  header, 1U, perf_single_metric::phase_active, msg_size)) {
                responder_ok.store (false, std::memory_order_release);
                break;
            }

            const uint64_t now_ns = perf_single_metric::now_ns ();
            const uint64_t sent_ts_ns =
              header.sent_ts_ns >= 0 ? static_cast<uint64_t> (header.sent_ts_ns) : 0u;
            latency_builder.add (
              now_ns >= sent_ts_ns ? static_cast<double> (now_ns - sent_ts_ns)
                                   : 0.0);
            ++active_count;
        }
        catch (const std::exception &) {
            responder_ok.store (false, std::memory_order_release);
            break;
        }
    }

    stop_requested.store (true, std::memory_order_release);
    responder_thread.join ();

    if (!responder_ok.load (std::memory_order_acquire) || active_count == 0
        || latency_builder.count () == 0) {
        if (perf_debug_enabled ())
            std::cerr << "spot_reqrep: active_count=" << active_count << std::endl;
        return false;
    }

    const perf::single::latency_stats_t latency = latency_builder.snapshot ();
    const double throughput =
      static_cast<double> (active_count) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "SPOT_REQREP",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns);
    return true;
}

} // namespace

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (
      argc, argv, run_pattern_spot_reqrep);
}
