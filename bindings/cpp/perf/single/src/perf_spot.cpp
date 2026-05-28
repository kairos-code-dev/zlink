// SPOT benchmark: one-way publisher->subscriber recv loop.
// Topology: publisher spot(bind) -> subscriber spot(connect)

#include "../common/perf_single_common.hpp"
#include "../common/perf_single_runner.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <zlink.hpp>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

const std::string k_topic = "bench";

unsigned current_process_id ()
{
#if defined(_WIN32)
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
                        size_t size_,
                        bool dontwait_,
                        bool *sent_out_)
{
    if (sent_out_)
        *sent_out_ = false;

    zlink::message_t msg = zlink::message_t::from_bytes (
      std::as_bytes (std::span<const char> (
        static_cast<const char *> (data_), size_)));
    if (!msg.valid ())
        return false;
    try {
        const bool sent =
          dontwait_
            ? std::move (spot_.publish (k_topic)
                           .message (std::move (msg))
                           .flags (static_cast<int>(zlink::send_flags_t::dontwait)))
                .submit ()
            : std::move (spot_.publish (k_topic).message (std::move (msg))).submit ();
        if (sent_out_)
            *sent_out_ = sent;
        return true;
    }
    catch (const zlink::submit_error_t &err) {
        errno = err.internal_errno ();
        if (dontwait_
            && perf::single::is_transient_spot_publish_errno (errno)) {
            return true;
        }
        return false;
    }
    catch (const zlink::binding_error_t &err) {
        errno = err.internal_errno ();
        if (dontwait_
            && perf::single::is_transient_spot_publish_errno (errno)) {
            return true;
        }
        return false;
    }
}

bool stamp_and_publish (zlink::service::spot_t &spot_,
                        std::vector<char> &payload_,
                        uint32_t run_id_,
                        size_t msg_size_,
                        uint64_t seq_,
                        perf_single_metric::phase_t phase_,
                        bool dontwait_,
                        bool *sent_out_)
{
    if (sent_out_)
        *sent_out_ = false;
    if (!perf_single_metric::stamp_payload (payload_.data (),
                                            payload_.size (),
                                            run_id_,
                                            phase_,
                                            msg_size_,
                                            seq_,
                                            perf_single_metric::now_ns ())) {
        return false;
    }
    return send_spot_payload (
      spot_, payload_.data (), payload_.size (), dontwait_, sent_out_);
}

bool decode_spot_header (const std::string &topic_,
                         zlink::message_t &part_,
                         size_t payload_size_,
                         perf_single_metric::header_t *header_out_)
{
    if (!header_out_ || topic_ != k_topic) {
        return false;
    }

    if (part_.size () != payload_size_)
        return false;

    return perf_single_metric::decode_payload_header (
      part_.data (), part_.size (), header_out_);
}

int recv_spot_header_flags (zlink::service::spot_t &subscriber_,
                            size_t payload_size_,
                            zlink::recv_flags_t flags_,
                            std::string &topic_,
                            zlink::message_t &part_,
                            perf_single_metric::header_t *header_out_,
                            bool *header_ok_out_,
                            bool *stop_out_ = NULL)
{
    if (header_ok_out_)
        *header_ok_out_ = false;
    if (stop_out_)
        *stop_out_ = false;
    try {
        bool has_more = false;
        const int rc = subscriber_.subscribe_part (
          topic_, part_, has_more, flags_);
        if (rc == static_cast<int> (zlink::recv_result_t::no_data))
            return 0;
        if (rc != static_cast<int> (zlink::recv_result_t::ok))
            return -1;
        if (has_more)
            return -1;
        if (stop_out_) {
            if (perf::single::is_stop_token_message (part_)) {
                *stop_out_ = true;
                return 1;
            }
        }
        bool header_ok =
          decode_spot_header (topic_, part_, payload_size_, header_out_);
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

template<typename Clock, typename Duration>
std::chrono::milliseconds remaining_milliseconds_until (
  const std::chrono::time_point<Clock, Duration> &deadline_)
{
    const auto now = Clock::now ();
    if (now >= deadline_)
        return std::chrono::milliseconds (0);
    return std::chrono::duration_cast<std::chrono::milliseconds> (
      deadline_ - now);
}

template<typename Clock, typename Duration>
bool wait_for_spot_input_until (
  zlink::poller_t &poller_,
  const std::chrono::time_point<Clock, Duration> &deadline_)
{
    const std::chrono::milliseconds remaining =
      remaining_milliseconds_until (deadline_);
    if (remaining.count () <= 0)
        return false;
    try {
        zlink::poll_event_t event;
        return poller_.wait (&event, 1, remaining) == 1;
    }
    catch (const zlink::recv_error_t &) {
        return false;
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
    zlink::poller_t poller;
    try {
        poller.add (subscriber_, zlink::poll_event_flag_t::pollin, 0);
    }
    catch (const zlink::config_error_t &) {
        return false;
    }

    uint64_t seq = 0;
    std::string topic;
    zlink::message_t part;
    while (std::chrono::steady_clock::now () < deadline) {
        bool sent = false;
        if (!stamp_and_publish (publisher_,
                                payload_,
                                state_->run_id,
                                state_->msg_size,
                                seq++,
                                perf_single_metric::phase_warmup,
                                true,
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
              subscriber_, state_->payload_size, static_cast<int>(zlink::send_flags_t::dontwait), topic, part,
              &header, &header_ok);
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
            if (!wait_for_spot_input_until (poller, probe_deadline))
                break;
        }
    }

    return false;
}

// C-faithful SPOT AUTO_HWM_DETAIL emitter. Mirrors
// bindings/c/perf/single/src/perf_spot.cpp emit_spot_hwm_detail
// byte-for-byte so the runner's "## Auto-HWM Detail" SPOT block
// is identical to the C reference.
const char *spot_socket_type_name (zlink::spot_node_socket_type_t type_)
{
    switch (type_) {
    case zlink::spot_node_socket_type_t::pair:
        return "pair";
    case zlink::spot_node_socket_type_t::pub:
        return "pub";
    case zlink::spot_node_socket_type_t::sub:
        return "sub";
    case zlink::spot_node_socket_type_t::dealer:
        return "dealer";
    case zlink::spot_node_socket_type_t::router:
        return "router";
    case zlink::spot_node_socket_type_t::xpub:
        return "xpub";
    case zlink::spot_node_socket_type_t::xsub:
        return "xsub";
    case zlink::spot_node_socket_type_t::stream:
        return "stream";
    default:
        return "unknown";
    }
}

const char *spot_socket_owner_name (zlink::spot_node_socket_owner_t owner_)
{
    switch (owner_) {
    case zlink::spot_node_socket_owner::node:
        return "node";
    case zlink::spot_node_socket_owner::spot:
        return "spot";
    default:
        return "unknown";
    }
}

const char *spot_auto_hwm_role_name (uint32_t role_)
{
    switch (role_) {
    case 1:
        return "control";
    case 2:
        return "routed";
    case 3:
        return "fanout";
    case 4:
        return "recv_ingress";
    case 5:
        return "spot_data";
    case 6:
        return "peer_queue";
    case 7:
        return "stream";
    default:
        return "none";
    }
}

void emit_spot_hwm_detail (zlink::service::spot_node_t &node_,
                           const char *component_,
                           const std::string &transport_,
                           size_t msg_size_)
{
    if (!component_)
        return;

    std::vector<zlink::spot_node_socket_entry_t> entries;
    try {
        entries = node_.internal_sockets ();
    }
    catch (const zlink::binding_error_t &) {
        return;
    }

    for (size_t i = 0; i < entries.size (); ++i) {
        const zlink::spot_node_socket_entry_t &entry = entries[i];
        if (!entry.auto_hwm_visible ())
            continue;
        const zlink::monitor_status_t &snapshot = entry.monitor_status ();
        if (snapshot.auto_hwm_applied_sndhwm <= 0
            && snapshot.auto_hwm_applied_rcvhwm <= 0) {
            continue;
        }
        std::cout << "AUTO_HWM_DETAIL"
                  << ",pattern=SPOT"
                  << ",transport=" << transport_
                  << ",component=" << component_
                  << ",msg_size=" << msg_size_
                  << ",owner=" << spot_socket_owner_name (entry.owner ())
                  << ",owner_id=" << entry.owner_id ()
                  << ",socket=" << entry.socket_name ()
                  << ",socket_type="
                  << spot_socket_type_name (entry.socket_type ())
                  << ",role="
                  << spot_auto_hwm_role_name (snapshot.auto_hwm_role)
                  << ",sndhwm=" << snapshot.auto_hwm_applied_sndhwm
                  << ",rcvhwm=" << snapshot.auto_hwm_applied_rcvhwm
                  << ",effective_message_bytes="
                  << snapshot.auto_hwm_effective_message_bytes
                  << ",effective_sndbuf=" << snapshot.auto_hwm_effective_sndbuf
                  << ",effective_rcvbuf=" << snapshot.auto_hwm_effective_rcvbuf
                  << ",socket_message_slots="
                  << snapshot.auto_hwm_socket_message_slots << std::endl;
    }
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
    if (!perf::single::apply_single_auto_hwm_msg_unit (ctx, msg_size)) {
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
        pub_node.set_pub_bind (publisher_endpoint);
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
        std::string topic;
        zlink::message_t part;
        auto collect_header =
          [&] (const perf_single_metric::header_t &header_,
               bool header_ok_) {
              if (header_ok_ && header_matches_active_run (state, header_)
                  && std::chrono::steady_clock::now () < active_deadline) {
                  received.fetch_add (1, std::memory_order_relaxed);
                  const int64_t now_ns = perf_single_metric::now_ns ();
                  latency_builder.add (
                    perf_single_metric::elapsed_latency_ns (
                      now_ns, header_.sent_ts_ns));
              }
          };
        while (true) {
            perf_single_metric::header_t header = {};
            bool header_ok = false;
            bool stop = false;
            const int recv_rc = recv_spot_header_flags (
              sub_spot, payload_size, static_cast<int>(zlink::send_flags_t::dontwait), topic, part, &header,
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
                                    true,
                                    &sent)) {
                sender_ok.store (false, std::memory_order_release);
                break;
            }
            if (!sent) {
                // PERF_SINGLE_TEST_POLICY: mirror C reference
                // perf_spot.cpp send_spot_samples, which calls
                // perf_socket_poll(NULL, 0, 1) (a 1ms idle wait) on
                // backpressure. Without this pacing the sender busy-spins
                // and floods the pipeline with stale-timestamp messages,
                // inflating delivered latency ~2000x at unchanged
                // throughput.
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
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

    emit_spot_hwm_detail (pub_node, "publisher", transport, msg_size);
    emit_spot_hwm_detail (sub_node, "subscriber", transport, msg_size);

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
