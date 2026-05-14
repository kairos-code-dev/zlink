// SPOT benchmark: one-way publisher->subscriber recv loop.
// Topology: publisher spot(bind) -> subscriber spot(connect)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <zlink_enum.h>

#if defined(ZLINK_HAVE_WINDOWS)
#include <process.h>
#endif

#if !defined(ZLINK_HAVE_WINDOWS)
#include <unistd.h>
#endif

namespace {

const std::string k_topic = "bench";

zlink::routing_id_t routing_id_from_ascii (const char *value_)
{
    return zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (value_), std::strlen (value_));
}

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

struct spot_recv_state_t
{
    spot_recv_state_t ()
        : msg_size (0),
          payload_size (0),
          run_id (1U),
          active_received (0)
    {
    }

    size_t msg_size;
    size_t payload_size;
    uint32_t run_id;
    std::atomic<unsigned long long> active_received;
};

bool header_matches_active_run (const spot_recv_state_t &state_,
                                const perf_single_metric::header_t &header_)
{
    return header_.magic == perf_single_metric::k_magic
           && header_.run_id == state_.run_id
           && header_.msg_size == static_cast<uint32_t> (state_.msg_size)
           && header_.phase
                == static_cast<uint8_t> (perf_single_metric::phase_active);
}

bool send_spot_payload (zlink::service::spot_t &spot_,
                        const void *data_,
                        size_t size_)
{
    return perf::single::publish_payload_blocking (
      spot_, k_topic, data_, size_);
}

bool stamp_and_publish (zlink::service::spot_t &spot_,
                        std::vector<char> &payload_,
                        uint32_t run_id_,
                        size_t msg_size_,
                        uint64_t seq_,
                        perf_single_metric::phase_t phase_,
                        bool *sent_out_)
{
    if (sent_out_)
        *sent_out_ = false;
    const bool ok = perf_single_metric::stamp_payload (
                      payload_.data (),
                      payload_.size (),
                      run_id_,
                      phase_,
                      msg_size_,
                      seq_,
                      perf_single_metric::now_ns ())
                    && send_spot_payload (
                      spot_, payload_.data (), payload_.size ());
    if (sent_out_)
        *sent_out_ = ok;
    return ok;
}

bool decode_spot_header (const zlink::topic_message_t &message_,
                         size_t payload_size_,
                         perf_single_metric::header_t *header_out_)
{
    if (!header_out_ || message_.topic () != k_topic
        || !message_.is_single_part ()) {
        return false;
    }

    zlink::message_t &part = const_cast<zlink::topic_message_t &> (
      message_).first_part ();
    if (part.size () != payload_size_)
        return false;

    return perf_single_metric::decode_payload_header (
      part.data (), part.size (), header_out_);
}

int recv_spot_header_flags (zlink::service::spot_t &subscriber_,
                            size_t payload_size_,
                            zlink::recv_flags_t flags_,
                            perf_single_metric::header_t *header_out_,
                            bool *header_ok_out_,
                            bool *stop_out_ = NULL)
{
    if (header_ok_out_)
        *header_ok_out_ = false;
    if (stop_out_)
        *stop_out_ = false;
    try {
        std::optional<zlink::topic_message_t> message =
          subscriber_.subscribe (flags_);
        if (!message.has_value ())
            return 0;
        if (stop_out_ && message->parts ().size () == 1
            && perf::single::is_stop_token_message (message->parts ()[0])) {
            *stop_out_ = true;
            return 1;
        }
        bool header_ok =
          decode_spot_header (*message, payload_size_, header_out_);
        if (header_ok_out_)
            *header_ok_out_ = header_ok;
        return 1;
    }
    catch (const zlink::recv_error_t &err) {
        const int internal_errno = err.internal_errno ();
        if (internal_errno == EAGAIN || internal_errno == EINTR)
            return 0;
        return -1;
    }
    catch (const std::exception &) {
        return -1;
    }
}

[[noreturn]] void fast_exit_process (int exit_code_)
{
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (exit_code_);
}

bool wait_for_local_probe_ready (zlink::service::spot_t &publisher_,
                                 zlink::service::spot_t &subscriber_,
                                 std::vector<char> &payload_,
                                 spot_recv_state_t *state_,
                                 int timeout_ms_)
{
    if (!state_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    uint64_t seq = 0;
    while (std::chrono::steady_clock::now () < deadline) {
        bool sent = false;
        if (!stamp_and_publish (publisher_,
                                payload_,
                                state_->run_id,
                                state_->msg_size,
                                seq++,
                                perf_single_metric::phase_warmup,
                                &sent)) {
            return false;
        }

        const auto probe_deadline =
          std::min (deadline, std::chrono::steady_clock::now ()
                                 + std::chrono::milliseconds (50));
        while (std::chrono::steady_clock::now () < probe_deadline) {
            perf_single_metric::header_t header = {};
            bool header_ok = false;
            const int recv_rc = recv_spot_header_flags (
              subscriber_, state_->payload_size, ZLINK_DONTWAIT, &header,
              &header_ok);
            if (recv_rc > 0) {
                if (header_ok
                    && header.magic == perf_single_metric::k_magic
                    && header.run_id == state_->run_id
                    && header.phase
                         == static_cast<uint8_t> (
                           perf_single_metric::phase_warmup)
                    && header.msg_size
                         == static_cast<uint32_t> (state_->msg_size))
                    return true;
                continue;
            }
            if (recv_rc < 0)
                return false;
            std::this_thread::yield ();
        }
    }

    return false;
}

} // namespace

bool run_pattern_spot (const std::string &transport,
                       size_t msg_size,
                       const std::string &lib_name)
{
    if (transport != "tcp" && transport != "tls" && transport != "ws"
        && transport != "wss") {
        std::cout << "UNSUPPORTED," << lib_name << ",SPOT," << transport
                  << std::endl;
        return true;
    }

    perf::single::ctx_guard_t ctx;
    if (!ctx.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    zlink::service::spot_node_t pub_node (ctx.ctx ());
    zlink::service::spot_node_t sub_node (ctx.ctx ());
    if (!pub_node.valid () || !sub_node.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    if (transport == "tls" || transport == "wss") {
        std::string cert;
        std::string key;
        std::string ca;
        if (!perf::try_resolve_tls_paths (cert, key, ca)) {
            perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
            return false;
        }
        try {
            pub_node.set_tls_server (cert, key, false);
            pub_node.set_tls_client (ca, std::string ("localhost"), false);
            sub_node.set_tls_server (cert, key, false);
            sub_node.set_tls_client (ca, std::string ("localhost"), false);
        }
        catch (const std::exception &) {
            perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
            return false;
        }
    }

    const std::string publisher_endpoint = make_spot_endpoint (transport);

    zlink::service::spot_t pub_spot = pub_node.create_spot ();
    zlink::service::spot_t stop_spot = sub_node.create_spot ();
    zlink::service::spot_t sub_spot = sub_node.create_spot ();
    if (!pub_spot.valid () || !stop_spot.valid () || !sub_spot.valid ()) {
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }
    try {
        pub_node.set_routing_id (
          routing_id_from_ascii ("z-cpp-perf-spot-publisher"));
        sub_node.set_routing_id (
          routing_id_from_ascii ("a-cpp-perf-spot-subscriber"));
        pub_spot.set_routing_id (
          routing_id_from_ascii ("z-cpp-perf-spot-publisher-spot"));
        stop_spot.set_routing_id (
          routing_id_from_ascii ("a-cpp-perf-spot-stop-spot"));
        sub_spot.set_routing_id (
          routing_id_from_ascii ("a-cpp-perf-spot-subscriber-spot"));
        if (perf::single::single_manual_socket_overrides_enabled ()) {
            const int pubsub_hwm =
              perf::single::resolve_single_socket_hwm (true);
            const int router_hwm =
              perf::single::resolve_single_socket_hwm (false);
            pub_node.pubsub_admission_hwm (
              zlink::message_count_t::value (pubsub_hwm));
            sub_node.pubsub_admission_hwm (
              zlink::message_count_t::value (pubsub_hwm));
            pub_node.router_admission_hwm (
              zlink::message_count_t::value (router_hwm));
            sub_node.router_admission_hwm (
              zlink::message_count_t::value (router_hwm));
        }
        pub_node.bind (publisher_endpoint);
        sub_node.connect_peer (publisher_endpoint);
    }
    catch (const std::exception &e) {
        if (perf_debug_enabled ())
            std::cerr << "spot: setup failed: " << e.what () << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    sub_spot.set_subscription (k_topic);

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 's');
    spot_recv_state_t state;
    state.run_id = 1U;
    state.msg_size = msg_size;
    state.payload_size = payload_size;

    if (!wait_for_local_probe_ready (
          pub_spot,
          sub_spot,
          payload,
          &state,
          perf::single::resolve_single_connect_ready_timeout_ms ())) {
        if (perf_debug_enabled ())
            std::cerr << "spot: local probe ready barrier failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    const int settle_ms = perf::single::resolve_single_spot_ready_settle_ms ();
    if (settle_ms > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (settle_ms));

    const int duration_s =
      std::max (1, perf::single::resolve_single_duration_seconds ());
    const int recv_timeout = perf::single::resolve_single_recv_timeout_ms ();

    std::atomic<bool> sender_ok (true);
    std::atomic<unsigned long long> received (0);
    perf::single::latency_stats_builder_t latency_builder (
      perf::single::resolve_single_latency_sample_cap ());
    const auto active_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);
    (void) recv_timeout;

    std::thread receiver_thread ([&]() {
        // PERF_SINGLE_TEST_POLICY § 1.4: receiver exits on wire-level
        // stop token instead of sender_done + drain timer. spot_t does
        // not expose a poller-compatible socket handle, so the receive
        // loop uses DONTWAIT + yield (same idiom dotnet/java/node use
        // for SPOT in this binding family).
        auto collect_header =
          [&] (const perf_single_metric::header_t &header_,
               bool header_ok_) {
              if (header_ok_ && header_matches_active_run (state, header_)
                  && std::chrono::steady_clock::now () < active_deadline) {
                  received.fetch_add (1, std::memory_order_relaxed);
                  const int64_t now_ns = perf_single_metric::now_ns ();
                  latency_builder.add (
                    header_.sent_ts_ns > 0 && now_ns >= header_.sent_ts_ns
                      ? static_cast<double> (now_ns - header_.sent_ts_ns)
                      : 0.0);
              }
          };
        while (true) {
            perf_single_metric::header_t header = {};
            bool header_ok = false;
            bool stop = false;
            const int recv_rc = recv_spot_header_flags (
              sub_spot, payload_size, ZLINK_DONTWAIT, &header,
              &header_ok, &stop);
            if (recv_rc > 0) {
                if (stop)
                    return;
                collect_header (header, header_ok);
                continue;
            }

            if (recv_rc == 0) {
                std::this_thread::yield ();
                continue;
            }

            sender_ok.store (false, std::memory_order_release);
            return;
        }
    });

    std::thread sender_thread ([&]() {
        uint64_t seq = 1;
        while (std::chrono::steady_clock::now () < active_deadline) {
            bool sent = false;
            if (!stamp_and_publish (pub_spot,
                                    payload,
                                    state.run_id,
                                    msg_size,
                                    seq,
                                    perf_single_metric::phase_active,
                                    &sent)) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            ++seq;
        }
        // PERF_SINGLE_TEST_POLICY § 1.4: publish one wire-level blocking
        // stop token under k_topic.
        if (!perf::single::publish_stop_token_blocking (stop_spot, k_topic))
            sender_ok.store (false, std::memory_order_release);
    });

    sender_thread.join ();
    receiver_thread.join ();

    const unsigned long long received_total =
      received.load (std::memory_order_relaxed);
    if (!sender_ok.load (std::memory_order_acquire) || received_total == 0
        || latency_builder.count () == 0) {
        if (perf_debug_enabled ())
            std::cerr << "spot: active phase failed" << std::endl;
        perf::single::print_fail_result (lib_name, "SPOT", transport, msg_size);
        return false;
    }

    const perf::single::latency_stats_t latency = latency_builder.snapshot ();

    const double throughput =
      static_cast<double> (received_total) / static_cast<double> (duration_s);
    perf::single::print_result (lib_name,
                                "SPOT",
                                transport,
                                msg_size,
                                throughput,
                                latency.mean_ns,
                                latency.p95_ns,
                                latency.p99_ns);
    fast_exit_process (0);
}

int main (int argc, char **argv)
{
    return perf::single::run_standard_bench_main (argc, argv, run_pattern_spot);
}
