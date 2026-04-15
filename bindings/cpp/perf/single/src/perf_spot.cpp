// SPOT benchmark: one-way publisher->subscriber recv loop.
// Topology: publisher spot(bind) -> subscriber spot(connect)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#endif

#if !defined(ZLINK_HAVE_WINDOWS)
#include <unistd.h>
#endif

namespace {

const char *const k_topic = "bench";

unsigned current_process_id ()
{
#if defined(ZLINK_HAVE_WINDOWS)
    return static_cast<unsigned> (_getpid ());
#else
    return static_cast<unsigned> (getpid ());
#endif
}

std::string make_spot_endpoint (const std::string &transport_)
{
    static unsigned counter = 0;
    const unsigned port = 34000u + ((current_process_id () % 1000u) * 20u)
                          + (++counter);
    return perf::single::make_fixed_endpoint (transport_, static_cast<int> (port));
}

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

bool send_spot_payload (void *userdata_,
                        const std::string &service_name_,
                        const void *data_,
                        size_t size_)
{
    zlink::service::spot_t *spot = static_cast<zlink::service::spot_t *> (userdata_);
    if (!spot)
        return false;

    zlink::message_t part = zlink::message_t::from_bytes (data_, size_);
    if (!part.valid ())
        return false;
    try {
        spot->publish (service_name_, k_topic, part);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

bool record_spot_payload (const zlink::topic_message_t &message,
                          const std::string &expected_service_name,
                          uint32_t run_id,
                          size_t msg_size,
                          size_t payload_size,
                          std::atomic<unsigned long long> &received_count,
                          perf::single::latency_stats_builder_t &latency_builder)
{
    if (!message.service_name ()
        || *message.service_name () != expected_service_name
        || message.topic () != k_topic || message.parts ().size () != 1
        || message.parts ()[0].size () != payload_size) {
        return true;
    }

    perf_single_metric::header_t header;
    if (!perf_single_metric::decode_payload_header (
          message.parts ()[0].data (), message.parts ()[0].size (), &header)) {
        return true;
    }
    if (!perf_single_metric::is_expected (
          header, run_id, perf_single_metric::phase_active, msg_size)) {
        return true;
    }

    received_count.fetch_add (1, std::memory_order_release);
    const uint64_t now = perf_single_metric::now_ns ();
    latency_builder.add (
      now >= header.sent_ts_ns ? static_cast<double> (now - header.sent_ts_ns)
                               : 0.0);
    return true;
}

} // namespace

void run_pattern_spot (const std::string &transport,
                       size_t msg_size,
                       const std::string &lib_name)
{
    if (transport != "tcp" && transport != "tls" && transport != "ws"
        && transport != "wss") {
        std::cout << "UNSUPPORTED,SPOT," << transport << std::endl;
        return;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "spot: invalid context" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    zlink::service::spot_node_t pub_node (ctx.ctx ());
    zlink::service::spot_node_t sub_node (ctx.ctx ());
    if (!pub_node.valid () || !sub_node.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "spot: invalid service topology" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    const std::string pub_service_name = "spot-bench";
    zlink::service::discovery_t pub_discovery (
      ctx.ctx (), zlink::service_type::spot, pub_service_name);
    if (!pub_discovery.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "spot: discovery setup failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }
    pub_node.attach_discovery (pub_discovery);

    zlink::service::spot_t pub_spot = pub_node.create_spot ();
    zlink::service::spot_t sub_spot = sub_node.create_spot ();
    if (!pub_spot.valid () || !sub_spot.valid ()) {
        if (perf_debug_enabled ())
            std::cerr << "spot: invalid service topology" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }

    if (transport == "tls" || transport == "wss") {
        std::string cert;
        std::string key;
        std::string ca;
        if (!perf::try_resolve_tls_paths (cert, key, ca)) {
            if (perf_debug_enabled ())
                std::cerr << "spot: tls setup failed" << std::endl;
            perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
            return;
        }
        try {
            pub_node.set_tls_server (cert, key, false);
            sub_node.set_tls_client (ca, std::string ("localhost"), false);
        }
        catch (const std::exception &) {
            if (perf_debug_enabled ())
                std::cerr << "spot: tls setup failed" << std::endl;
            perf::single::print_fail_result (
              lib_name, "SPOT", transport, msg_size);
            return;
        }
    }

    pub_spot.options ().send_hwm (
      perf::single::resolve_single_socket_hwm (true));
    sub_spot.options ().recv_hwm (
      perf::single::resolve_single_socket_hwm (false));
    pub_spot.options ().send_timeout (
      perf::single::resolve_single_send_timeout_ms ());
    sub_spot.options ().recv_timeout (
      perf::single::resolve_single_recv_timeout_ms ());

    const std::string endpoint = make_spot_endpoint (transport);
    try {
        pub_node.bind (endpoint);
        sub_node.connect_peer (endpoint);
        sub_spot.set_subscription (k_topic);
    }
    catch (const std::exception &) {
        if (perf_debug_enabled ())
            std::cerr << "spot: ready gate failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 's');

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_ns ());
    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();
    std::atomic<unsigned long long> sent_count (0);
    std::atomic<unsigned long long> received_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<bool> sender_done (false);
    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());

    std::thread sender_thread ([&]() {
        uint64_t seq = 1;
        const auto deadline =
          std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);
        while (std::chrono::steady_clock::now () < deadline) {
            if (!perf_single_metric::stamp_payload (payload.data (),
                                                    payload.size (),
                                                    run_id,
                                                    perf_single_metric::phase_active,
                                                    msg_size,
                                                    seq++,
                                                    perf_single_metric::now_ns ())
                || !send_spot_payload (
                  &pub_spot, pub_service_name, payload.data (), payload.size ())) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            sent_count.fetch_add (1, std::memory_order_release);
        }
        sender_done.store (true, std::memory_order_release);
    });

    zlink::poller_t poller;
    poller.add (sub_spot, zlink::poll_event::pollin);
    while (!sender_done.load (std::memory_order_acquire)) {
        zlink::poll_event_t event = {};
        const int poll_rc = poller.wait (&event, 5);
        if (poll_rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            sender_ok.store (false, std::memory_order_release);
            break;
        }
        if (poll_rc == 0
            || (event.revents & static_cast<short> (zlink::poll_event::pollin))
                 == 0) {
            continue;
        }

        for (;;) {
            try {
                const zlink::topic_message_t message =
                  sub_spot.subscribe (zlink::recv_flags_t::dontwait);
                if (!record_spot_payload (message,
                                          pub_service_name,
                                          run_id,
                                          msg_size,
                                          payload_size,
                                          received_count,
                                          latency_builder)) {
                    sender_ok.store (false, std::memory_order_release);
                    break;
                }
            }
            catch (const zlink::recv_error_t &err) {
                if (err.result () == zlink::recv_result_t::no_data
                    || err.result () == zlink::recv_result_t::busy) {
                    break;
                }
                sender_ok.store (false, std::memory_order_release);
                break;
            }
        }
    }

    sender_thread.join ();
    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (recv_timeout * 2);
    while (received_count.load (std::memory_order_acquire)
             < sent_count.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < drain_deadline) {
        try {
            const zlink::topic_message_t message =
              sub_spot.subscribe (zlink::recv_flags_t::dontwait);
            if (!record_spot_payload (message,
                                      pub_service_name,
                                      run_id,
                                      msg_size,
                                      payload_size,
                                      received_count,
                                      latency_builder)) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
        }
        catch (const zlink::recv_error_t &err) {
            if (err.result () == zlink::recv_result_t::no_data
                || err.result () == zlink::recv_result_t::busy) {
                break;
            }
            sender_ok.store (false, std::memory_order_release);
            break;
        }
    }

    const unsigned long long received =
      received_count.load (std::memory_order_acquire);
    if (!sender_ok.load (std::memory_order_acquire) || received == 0
        || latency_builder.count () == 0) {
        if (perf_debug_enabled ())
            std::cerr << "spot: active phase failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return;
    }
    const perf::single::latency_stats_t latency = latency_builder.snapshot ();

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "SPOT",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns);
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_spot);
}
