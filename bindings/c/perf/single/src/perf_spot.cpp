#include "../common/bench_common.hpp"
#include "../common/perf_single_latency.hpp"
#include "../common/perf_single_metric_header.hpp"
#include "../common/perf_single_phase.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

namespace {

static const char *k_pattern = "SPOT";
static const char *k_topic = "bench";

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

struct spot_recv_state_t
{
    spot_recv_state_t () : run_id (0), msg_size (0), active_received (0) {}

    uint32_t run_id;
    size_t msg_size;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
};

std::string bind_node (void *node_, const std::string &transport_, int base_port_)
{
    return perf_bind_fixed_endpoint_range (
      node_, transport_, base_port_, 64, &perf_bind_spot_node_endpoint);
}

int resolve_spot_subscription_ready_timeout_ms (const std::string &transport_)
{
    const int connect_timeout_ms =
      parse_positive_env ("PERF_CONNECT_READY_TIMEOUT_MS",
                          1000);
    return parse_positive_env ("PERF_SINGLE_SPOT_SUBJECT_READY_TIMEOUT_MS",
                               connect_timeout_ms);
}

bool topic_matches (const char *topic_, size_t topic_len_)
{
    if (!topic_)
        return false;
    if (topic_len_ > 0 && topic_[topic_len_ - 1] == '\0')
        --topic_len_;
    return topic_len_ == std::strlen (k_topic)
           && std::memcmp (topic_, k_topic, topic_len_) == 0;
}

const char *socket_type_name (zlink_socket_type_t type_)
{
    switch (type_) {
    case ZLINK_SOCKET_PAIR:
        return "pair";
    case ZLINK_SOCKET_PUB:
        return "pub";
    case ZLINK_SOCKET_SUB:
        return "sub";
    case ZLINK_SOCKET_DEALER:
        return "dealer";
    case ZLINK_SOCKET_ROUTER:
        return "router";
    case ZLINK_SOCKET_XPUB:
        return "xpub";
    case ZLINK_SOCKET_XSUB:
        return "xsub";
    case ZLINK_SOCKET_STREAM:
        return "stream";
    default:
        return "unknown";
    }
}

const char *auto_hwm_role_name (uint32_t role_)
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

const char *spot_socket_owner_name (zlink_spot_node_socket_owner_t owner_)
{
    switch (owner_) {
    case ZLINK_SPOT_NODE_SOCKET_OWNER_NODE:
        return "node";
    case ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT:
        return "spot";
    default:
        return "unknown";
    }
}

void emit_spot_hwm_detail (void *node_,
                           const char *component_,
                           const std::string &transport_,
                           size_t msg_size_)
{
    if (!node_ || !component_)
        return;

    size_t count = 0;
    if (zlink_spot_node_internal_sockets (node_, NULL, NULL, &count)
        != ZLINK_CONFIG_OK
        || count == 0) {
        return;
    }

    std::vector<zlink_spot_node_socket_entry_t> entries (count);
    if (zlink_spot_node_internal_sockets (
          node_, NULL, entries.data (), &count)
        != ZLINK_CONFIG_OK) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        const zlink_spot_node_socket_entry_t &entry = entries[i];
        if (entry.auto_hwm_visible == 0)
            continue;
        const zlink_monitor_status_t &snapshot = entry.monitor_status;
        if (snapshot.auto_hwm_applied_sndhwm <= 0
            && snapshot.auto_hwm_applied_rcvhwm <= 0) {
            continue;
        }
        std::cout << "AUTO_HWM_DETAIL"
                  << ",pattern=" << k_pattern
                  << ",transport=" << transport_
                  << ",component=" << component_
                  << ",msg_size=" << msg_size_
                  << ",owner=" << spot_socket_owner_name (entry.owner)
                  << ",owner_id=" << entry.owner_id
                  << ",socket=" << entry.socket_name
                  << ",socket_type=" << socket_type_name (entry.socket_type)
                  << ",role=" << auto_hwm_role_name (snapshot.auto_hwm_role)
                  << ",sndhwm=" << snapshot.auto_hwm_applied_sndhwm
                  << ",rcvhwm=" << snapshot.auto_hwm_applied_rcvhwm
                  << ",effective_message_bytes="
                  << snapshot.auto_hwm_effective_message_bytes
                  << ",effective_sndbuf=" << snapshot.auto_hwm_effective_sndbuf
                  << ",effective_rcvbuf=" << snapshot.auto_hwm_effective_rcvbuf
                  << ",socket_message_slots="
                  << snapshot.auto_hwm_socket_message_slots
                  << std::endl;
    }
}

int recv_spot_header_flags (void *subscriber_,
                            int flags_,
                            perf_single_metric::header_t *header_out_,
                            bool *header_ok_out_)
{
    if (!subscriber_)
        return -1;

    if (header_ok_out_)
        *header_ok_out_ = false;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof (topic);
    const int rc = perf_zlink_spot_subscribe_parts (
      subscriber_, NULL, &parts, &part_count, topic, &topic_len,
      static_cast<zlink_recv_flags_t> (flags_));
    if (rc != 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    const bool topic_ok = topic_matches (topic, topic_len);
    if (topic_ok && parts && part_count >= 1
        && is_stop_token (zlink_msg_data (&parts[0]),
                          zlink_msg_size (&parts[0]))) {
        zlink_multipart_close (parts, part_count);
        std::free (parts);
        return 2; // stop token
    }
    const bool header_ok =
      topic_ok && parts && part_count >= 1 && header_out_
      && perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts[0]), zlink_msg_size (&parts[0]), header_out_);
    if (header_ok_out_)
        *header_ok_out_ = header_ok;
    zlink_multipart_close (parts, part_count);
    std::free (parts);
    return 1;
}

bool publish_metric_payload (void *publisher_,
                             std::vector<char> *payload_,
                             size_t msg_size_,
                             uint32_t run_id_,
                             uint64_t seq_,
                             perf_single_metric::phase_t phase_,
                             int flags_,
                             bool *sent_out_ = NULL)
{
    if (!publisher_ || !payload_)
        return false;

    if (sent_out_)
        *sent_out_ = false;

    const size_t payload_size =
      std::max (msg_size_, perf_single_metric::header_size ());
    if (payload_->size () != payload_size)
        payload_->assign (payload_size, 's');
    if (!perf_single_metric::stamp_payload (
          payload_->data (),
          payload_->size (),
          run_id_,
          phase_,
          msg_size_,
          seq_,
          perf_single_metric::now_ns ())) {
        return false;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size (&part, payload_->size ()) != 0)
        return false;
    std::memcpy (zlink_msg_data (&part), payload_->data (), payload_->size ());
    if (perf_zlink_spot_publish_parts (
          publisher_, k_topic, &part, 1, static_cast<zlink_send_flags_t> (flags_))
        != 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        const bool retry_ready_probe =
          (flags_ & ZLINK_DONTWAIT) != 0
          && (err == EAGAIN || err == ENOTCONN || err == EHOSTUNREACH
              || err == ENETUNREACH);
        if (err == EINTR || err == EAGAIN || retry_ready_probe)
            return true;
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] publish failed err=" << err << std::endl;
        }
        return false;
    }

    if (sent_out_)
        *sent_out_ = true;
    return true;
}

bool wait_for_spot_ready_barrier (void *publisher_,
                                  void *subscriber_,
                                  spot_recv_state_t *state_,
                                  size_t msg_size_,
                                  int timeout_ms_)
{
    if (!publisher_ || !subscriber_ || !state_)
        return false;

    state_->active_received.store (0, std::memory_order_release);
    state_->latency = latency_stats_builder_t ();
    std::vector<char> payload;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    while (std::chrono::steady_clock::now () < deadline) {
        if (!publish_metric_payload (publisher_,
                                     &payload,
                                     msg_size_,
                                     state_->run_id,
                                     0,
                                     perf_single_metric::phase_warmup,
                                     ZLINK_DONTWAIT)) {
            if (bench_debug_enabled ())
                std::cerr << "[perf-spot] probe publish failed" << std::endl;
            return false;
        }

        const auto probe_deadline =
          std::min (deadline, std::chrono::steady_clock::now ()
                                 + std::chrono::milliseconds (50));
        while (std::chrono::steady_clock::now () < probe_deadline) {
            bool progressed = false;
            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc =
              recv_spot_header_flags (
                subscriber_, ZLINK_DONTWAIT, &header, &header_ok);
            if (recv_rc > 0) {
                progressed = true;
                if (header_ok) {
                    const bool ready_seen =
                      header.magic == perf_single_metric::k_magic
                      && header.run_id == state_->run_id
                      && header.phase
                           == static_cast<uint8_t> (
                             perf_single_metric::phase_warmup)
                      && header.msg_size
                           == static_cast<uint32_t> (state_->msg_size);
                    if (ready_seen) {
                        if (bench_debug_enabled ())
                            std::cerr << "[perf-spot] ready barrier complete"
                                      << std::endl;
                        return true;
                    }
                }
            } else if (recv_rc < 0) {
                return false;
            }

            if (!progressed
                && !wait_socket_event_until (
                  subscriber_, ZLINK_POLLIN, probe_deadline)) {
                break;
            }
        }
    }

    return false;
}

bool run_spot_post_ready_settle ()
{
    const int settle_ms = resolve_single_spot_ready_settle_ms ();
    return settle_ms <= 0 || perf_socket_poll (NULL, 0, settle_ms) >= 0;
}

// PERF_SINGLE_TEST_POLICY § 1.4: publish wire-level stop token on the
// SPOT topic so the subscriber recv loop exits via is_stop_token.
// Bounded retry through transient backpressure.
bool send_spot_stop_token (void *publisher_)
{
    if (!publisher_)
        return false;
    for (int retry = 0; retry < 100; ++retry) {
        zlink_msg_t part;
        if (zlink_msg_init_size (&part, std::strlen (k_stop_token)) != 0)
            return false;
        std::memcpy (zlink_msg_data (&part), k_stop_token,
                     std::strlen (k_stop_token));
        if (perf_zlink_spot_publish_parts (publisher_, k_topic, &part, 1,
                                ZLINK_SEND_FLAGS_NONE)
            == 0)
            return true;
        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err != EINTR && err != EAGAIN && err != EWOULDBLOCK
            && err != ETIMEDOUT && err != ENOTCONN && err != EHOSTUNREACH
            && err != ENETUNREACH)
            return false;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return false;
}

bool send_spot_samples (void *publisher_,
                        std::vector<char> *payload_,
                        spot_recv_state_t *state_,
                        int duration_s_,
                        std::atomic<unsigned long long> *sent_count_)
{
    if (!publisher_ || !payload_ || !state_ || !sent_count_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, duration_s_));
    uint64_t seq = 1;
    while (std::chrono::steady_clock::now () < deadline) {
        bool sent = false;
        if (!publish_metric_payload (publisher_,
                                     payload_,
                                     state_->msg_size,
                                     state_->run_id,
                                     seq,
                                     perf_single_metric::phase_active,
                                     ZLINK_DONTWAIT,
                                     &sent)) {
            return false;
        }
        if (!sent) {
            (void) perf_socket_poll (NULL, 0, 1);
            continue;
        }
        sent_count_->fetch_add (1, std::memory_order_release);
        ++seq;
    }
    return true;
}

bool run_active_window (void *publisher_,
                        void *stop_publisher_,
                        void *subscriber_,
                        spot_recv_state_t *state_,
                        int duration_s_,
                        int recv_timeout_ms_,
                        double *throughput_out_,
                        latency_stats_t *latency_out_)
{
    (void) recv_timeout_ms_;
    if (!publisher_ || !stop_publisher_ || !subscriber_ || !state_
        || !throughput_out_ || !latency_out_) {
        return false;
    }

    state_->active_received.store (0, std::memory_order_release);
    state_->latency = latency_stats_builder_t ();
    std::vector<char> payload;
    std::atomic<unsigned long long> sent_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<unsigned long long> received (0);
    latency_stats_builder_t latency_builder;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, duration_s_));

    // PERF_SINGLE_TEST_POLICY § 1.4: receiver no longer checks an atomic
    // sender_done flag.  Phase end is signaled purely by the wire-level
    // stop token published by the sender at end-of-active. The spot
    // handle does not expose a poller-compatible socket handle for
    // signal-driven recv, so the loop uses DONTWAIT + yield (same idiom
    // as bindings/cpp/perf/single/src/perf_spot.cpp).
    std::thread receiver_thread ([&]() {
        while (true) {
            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_spot_header_flags (
              subscriber_, ZLINK_DONTWAIT, &header, &header_ok);
            if (recv_rc == 1) {
                if (header_ok && single_header_matches_run (*state_, header)
                    && std::chrono::steady_clock::now () < deadline) {
                    received.fetch_add (1, std::memory_order_relaxed);
                    latency_builder.add (single_latency_ns (header));
                }
                continue;
            }
            if (recv_rc == 0) {
                std::this_thread::yield ();
                continue;
            }
            if (recv_rc == 2) {
                if (bench_debug_enabled ())
                    std::cerr << "[perf-spot] receiver stop token observed"
                              << std::endl;
                return;
            }
            sender_ok.store (false, std::memory_order_release);
            return;
        }
    });

    std::thread sender_thread ([&]() {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] sender start" << std::endl;
        const bool active_ok = send_spot_samples (
          publisher_, &payload, state_, duration_s_, &sent_count);
        // Keep phase-end signaling on a SPOT message, but avoid queuing the
        // terminator behind the saturated one-way data path.
        const bool stop_ok = send_spot_stop_token (stop_publisher_);
        sender_ok.store (active_ok && stop_ok, std::memory_order_release);
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] sender done ok="
                      << (sender_ok.load (std::memory_order_acquire) ? 1 : 0)
                      << " sent="
                      << sent_count.load (std::memory_order_relaxed)
                      << std::endl;
        }
    });

    sender_thread.join ();
    receiver_thread.join ();
    if (!sender_ok.load (std::memory_order_acquire)) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] active phase failed sent="
                      << sent_count.load (std::memory_order_relaxed)
                      << " received="
                      << received.load (std::memory_order_relaxed)
                      << std::endl;
        }
        return false;
    }

    const unsigned long long recv_total =
      received.load (std::memory_order_relaxed);
    if (recv_total == 0 || latency_builder.count () == 0)
        return false;

    *throughput_out_ =
      static_cast<double> (recv_total)
      / static_cast<double> (std::max (1, duration_s_));
    *latency_out_ = latency_builder.snapshot ();
    return true;
}

void cleanup_spot_case (void **subscriber_,
                        void **stop_publisher_,
                        void **publisher_,
                        void **subscriber_discovery_,
                        void **publisher_discovery_,
                        void **registry_,
                        void **subscriber_node_,
                        void **publisher_node_)
{
    if (subscriber_ && *subscriber_)
        zlink_spot_destroy (subscriber_);
    if (stop_publisher_ && *stop_publisher_)
        zlink_spot_destroy (stop_publisher_);
    if (publisher_ && *publisher_)
        zlink_spot_destroy (publisher_);
    if (subscriber_discovery_ && *subscriber_discovery_)
        (void) zlink_discovery_destroy (subscriber_discovery_);
    if (publisher_discovery_ && *publisher_discovery_)
        (void) zlink_discovery_destroy (publisher_discovery_);
    if (registry_ && *registry_)
        (void) zlink_registry_destroy (registry_);
    if (subscriber_node_ && *subscriber_node_)
        (void) zlink_spot_node_destroy (subscriber_node_);
    if (publisher_node_ && *publisher_node_)
        (void) zlink_spot_node_destroy (publisher_node_);
}

void fast_exit_process (int exit_code_)
{
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (exit_code_);
}

int run_case (const std::string &lib_name_,
              const std::string &transport_,
              size_t msg_size_)
{
    if (!perf_supports_service_transport (transport_)
        || !transport_available (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_
                  << std::endl;
        return 0;
    }

    auto print_fail = [&] () {
        print_fail_result (lib_name_, k_pattern, transport_, msg_size_);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail ();
        return 1;
    }
    if (!apply_single_auto_hwm_msg_unit (ctx.get (), msg_size_)) {
        print_fail ();
        return 1;
    }

    void *publisher_node = zlink_spot_node_new (ctx.get (), NULL);
    void *subscriber_node = zlink_spot_node_new (ctx.get (), NULL);
    void *registry = NULL;
    void *publisher_discovery = NULL;
    void *subscriber_discovery = NULL;
    void *publisher = NULL;
    void *stop_publisher = NULL;
    void *subscriber = NULL;
    if (!publisher_node || !subscriber_node) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] object creation failed" << std::endl;
        cleanup_spot_case (&subscriber,
                           &stop_publisher,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    if (!setup_tls_server (publisher_node, transport_)
        || !setup_tls_client (publisher_node, transport_)
        || !setup_tls_server (subscriber_node, transport_)
        || !setup_tls_client (subscriber_node, transport_)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] tls setup failed err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (&subscriber,
                           &stop_publisher,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    const int base_port = 25000 + (current_process_id () % 32) * 512;
    publisher = zlink_spot_new (publisher_node);
    // The stop publisher lives on the subscriber node so the stop token is a
    // SPOT topic message without depending on the active remote backlog.
    stop_publisher = zlink_spot_new (subscriber_node);
    subscriber = zlink_spot_new (subscriber_node);
    if (!publisher || !stop_publisher || !subscriber) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] spot handle creation failed"
                      << std::endl;
        cleanup_spot_case (&subscriber,
                           &stop_publisher,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    apply_single_hwm (publisher);
    apply_single_hwm (stop_publisher);
    apply_single_hwm (subscriber);
    apply_single_benchmark_socket_options (publisher, transport_);
    apply_single_benchmark_socket_options (stop_publisher, transport_);
    apply_single_benchmark_socket_options (subscriber, transport_);

    if (zlink_set_subscription (subscriber, k_topic) != ZLINK_CONFIG_OK) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] set subscription failed err="
                      << zlink_errno () << std::endl;
        cleanup_spot_case (&subscriber,
                           &stop_publisher,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    const std::string publisher_endpoint =
      bind_node (publisher_node, transport_, base_port + 128);
    if (publisher_endpoint.empty ()
        || zlink_spot_node_connect_peer (
             subscriber_node, publisher_endpoint.c_str ())
             != ZLINK_CONNECT_OK) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] node direct connect failed"
                      << " pub=" << publisher_endpoint
                      << " err=" << zlink_errno () << std::endl;
        }
        cleanup_spot_case (&subscriber,
                           &stop_publisher,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }
    spot_recv_state_t state;
    state.run_id = next_single_metric_run_id ();
    state.msg_size = msg_size_;
    const int ready_timeout_ms =
      resolve_spot_subscription_ready_timeout_ms (transport_);
    if (!wait_for_spot_ready_barrier (
          publisher,
          subscriber,
          &state,
          msg_size_,
          ready_timeout_ms)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] ready barrier failed" << std::endl;
        cleanup_spot_case (&subscriber,
                           &stop_publisher,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }
    if (!run_spot_post_ready_settle ()) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] post-ready settle failed" << std::endl;
        cleanup_spot_case (&subscriber,
                           &stop_publisher,
                           &publisher,
                           &subscriber_discovery,
                           &publisher_discovery,
                           &registry,
                           &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }
    if (bench_debug_enabled ())
        std::cerr << "[perf-spot] post-ready settle complete" << std::endl;

    double throughput = 0.0;
    latency_stats_t latency;
    if (bench_debug_enabled ())
        std::cerr << "[perf-spot] active window start" << std::endl;
    const bool active_ok = run_active_window (publisher,
                                              stop_publisher,
                                              subscriber,
                                              &state,
                                              std::max (
                                                1, resolve_single_duration_seconds ()),
                                              resolve_single_recv_timeout_ms (),
                                              &throughput,
                                              &latency);
    if (bench_debug_enabled ()) {
        std::cerr << "[perf-spot] active window complete ok="
                  << (active_ok ? 1 : 0) << std::endl;
    }
    if (bench_debug_enabled () && !active_ok)
        std::cerr << "[perf-spot] active window failed" << std::endl;
    if (!active_ok) {
        print_fail ();
        fast_exit_process (1);
        return 1;
    }

    emit_spot_hwm_detail (
      publisher_node, "publisher", transport_, msg_size_);
    emit_spot_hwm_detail (
      subscriber_node, "subscriber", transport_, msg_size_);

    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  throughput,
                  latency.mean_ns,
                  latency.p95_ns,
                  latency.p99_ns);
    fast_exit_process (0);
    return 0;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!single_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t msg_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    return run_case (lib_name, transport, msg_size > 0 ? msg_size : 64);
}
