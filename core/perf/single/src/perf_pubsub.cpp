#include "../common/bench_common.hpp"
#include "../common/perf_single_latency.hpp"
#include "../common/perf_single_metric_header.hpp"
#include "../common/perf_single_monitor.hpp"
#include "../common/perf_single_phase.hpp"

#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {

static const char *k_pubsub_topic = "bench";

inline int resolve_pubsub_xpub_nodrop_opt ()
{
    const char *env = std::getenv ("PERF_SINGLE_PUBSUB_XPUB_NODROP");
    return (env && std::strcmp (env, "0") == 0) ? 0 : 1;
}

struct pubsub_recv_state_t
{
    pubsub_recv_state_t () : run_id (0), msg_size (0), payload_size (0), active_received (0) {}

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
};

bool setup_connected_pubsub_pair (void *pub_socket_,
                                  void *sub_socket_,
                                  const std::string &transport_,
                                  const std::string &id_)
{
    if (!setup_tls_server (pub_socket_, transport_)
        || !setup_tls_client (sub_socket_, transport_)) {
        return false;
    }
    apply_single_hwm (pub_socket_);
    apply_single_hwm (sub_socket_);
    if (zlink_set_subscription (sub_socket_, "") != ZLINK_CONFIG_OK)
        return false;
    const std::string endpoint =
      bind_and_resolve_endpoint (pub_socket_, transport_, id_);
    if (endpoint.empty ())
        return false;
    void *sub_monitor = open_configured_socket_monitor (
      sub_socket_, ZLINK_EVENT_CONNECTION_READY);
    void *pub_monitor = open_configured_socket_monitor (
      pub_socket_, ZLINK_EVENT_CONNECTION_READY);
    if (!sub_monitor || !pub_monitor || !connect_checked (sub_socket_, endpoint))
        return false;
    apply_single_benchmark_socket_options (pub_socket_, transport_);
    apply_single_benchmark_socket_options (sub_socket_, transport_);
    const int timeout_ms =
      parse_positive_env ("PERF_CONNECT_READY_TIMEOUT_MS", 3000);
    const bool sub_ready = wait_for_socket_monitor_event (
      sub_monitor, ZLINK_EVENT_CONNECTION_READY, timeout_ms);
    const bool pub_ready = wait_for_socket_monitor_event (
      pub_monitor, ZLINK_EVENT_CONNECTION_READY, timeout_ms);
    zlink_monitor_close (&sub_monitor);
    zlink_monitor_close (&pub_monitor);
    if (sub_ready && pub_ready) {
        const int settle_ms = resolve_single_pubsub_ready_settle_ms ();
        if (settle_ms > 0 && perf_socket_poll (NULL, 0, settle_ms) < 0)
            return false;
    }
    return sub_ready && pub_ready;
}

int recv_pubsub_header_flags (void *subscriber_,
                              size_t payload_size_,
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
    const int rc = zlink_subscribe (
      subscriber_, NULL, &parts, &part_count, topic, &topic_len,
      static_cast<zlink_recv_flags_t> (flags_));
    if (rc != 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    const bool topic_ok =
      topic_len == std::strlen (k_pubsub_topic)
      && std::memcmp (topic, k_pubsub_topic, topic_len) == 0;
    if (!topic_ok || !parts || part_count != 1) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-pubsub] invalid recv topic_ok="
                      << (topic_ok ? 1 : 0)
                      << " part_count=" << part_count << std::endl;
        }
        if (parts)
            zlink_multipart_close (parts, part_count);
        return -1;
    }

    const size_t actual_size = zlink_msg_size (&parts[0]);
    const bool size_ok = actual_size == payload_size_;
    bool header_ok = false;
    if (size_ok && header_out_) {
        header_ok = perf_single_metric::decode_payload_header (
          zlink_msg_data (&parts[0]), actual_size, header_out_);
    }
    zlink_multipart_close (parts, part_count);
    if (!size_ok) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-pubsub] unexpected payload size="
                      << actual_size << " expected=" << payload_size_
                      << std::endl;
        }
        return -1;
    }

    if (header_ok_out_)
        *header_ok_out_ = header_ok;
    return 1;
}

bool send_pubsub_samples (void *publisher_,
                          std::vector<char> *payload_,
                          pubsub_recv_state_t *state_,
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
        if (!perf_single_metric::stamp_payload (
              payload_->data (),
              payload_->size (),
              state_->run_id,
              perf_single_metric::phase_active,
              state_->msg_size,
              seq,
              perf_single_metric::now_ns ())) {
            return false;
        }

        zlink_msg_t part;
        if (zlink_msg_init_size (&part, payload_->size ()) != 0)
            return false;
        if (!payload_->empty ())
            std::memcpy (
              zlink_msg_data (&part), payload_->data (), payload_->size ());

        if (zlink_publish (
              publisher_, k_pubsub_topic, &part, 1,
              static_cast<zlink_send_flags_t> (0))
            != 0) {
            const int err = zlink_errno ();
            if (bench_debug_enabled ()) {
                std::cerr << "[perf-pubsub] publish failed err=" << err
                          << std::endl;
            }
            if (err == EINTR)
                continue;
            return false;
        }

        sent_count_->fetch_add (1, std::memory_order_release);
        ++seq;
    }

    return true;
}

bool run_active_phase (void *publisher_,
                       void *subscriber_,
                       std::vector<char> *payload_,
                       pubsub_recv_state_t *state_,
                       int duration_s_,
                       int recv_timeout_ms_,
                       unsigned long long *received_out_,
                       latency_stats_t *latency_out_)
{
    if (!publisher_ || !subscriber_ || !payload_ || !state_ || !received_out_
        || !latency_out_) {
        return false;
    }

    state_->active_received.store (0, std::memory_order_release);
    state_->latency = latency_stats_builder_t ();

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (1, duration_s_));
    const auto drain_idle_limit =
      std::chrono::milliseconds (recv_timeout_ms_ > 0 ? recv_timeout_ms_ : 200);

    std::atomic<unsigned long long> sent_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<bool> sender_done (false);
    std::atomic<unsigned long long> received (0);
    latency_stats_builder_t latency_builder;

    std::thread receiver_thread ([&]() {
        auto last_recv_at = std::chrono::steady_clock::now ();
        while (true) {
            const bool done = sender_done.load (std::memory_order_acquire);
            perf_single_metric::header_t header;
            bool header_ok = false;
            const int recv_rc = recv_pubsub_header_flags (
              subscriber_, state_->payload_size, 0, &header, &header_ok);
            if (recv_rc > 0) {
                last_recv_at = std::chrono::steady_clock::now ();
                if (header_ok && single_header_matches_run (*state_, header)
                    && std::chrono::steady_clock::now () < deadline) {
                    received.fetch_add (1, std::memory_order_relaxed);
                    latency_builder.add (single_latency_ns (header));
                }

                for (;;) {
                    perf_single_metric::header_t burst_header;
                    bool burst_header_ok = false;
                    const int burst_rc = recv_pubsub_header_flags (
                      subscriber_,
                      state_->payload_size,
                      ZLINK_DONTWAIT,
                      &burst_header,
                      &burst_header_ok);
                    if (burst_rc > 0) {
                        last_recv_at = std::chrono::steady_clock::now ();
                        if (burst_header_ok
                            && single_header_matches_run (*state_, burst_header)
                            && std::chrono::steady_clock::now () < deadline) {
                            received.fetch_add (1, std::memory_order_relaxed);
                            latency_builder.add (
                              single_latency_ns (burst_header));
                        }
                        continue;
                    }
                    if (burst_rc == 0)
                        break;
                    sender_ok.store (false, std::memory_order_release);
                    return;
                }
                continue;
            }

            if (recv_rc == 0) {
                if (done
                    && std::chrono::steady_clock::now () - last_recv_at
                         >= drain_idle_limit) {
                    break;
                }
                continue;
            }

            sender_ok.store (false, std::memory_order_release);
            return;
        }
    });

    std::thread sender_thread ([&]() {
        sender_ok.store (
          send_pubsub_samples (
            publisher_, payload_, state_, duration_s_, &sent_count),
          std::memory_order_release);
        sender_done.store (true, std::memory_order_release);
    });

    sender_thread.join ();
    receiver_thread.join ();
    if (!sender_ok.load (std::memory_order_acquire)) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-pubsub] active phase failed sent="
                      << sent_count.load (std::memory_order_relaxed)
                      << " received="
                      << received.load (std::memory_order_relaxed)
                      << std::endl;
        }
        return false;
    }

    *received_out_ = received.load (std::memory_order_relaxed);
    if (*received_out_ == 0)
        return false;
    *latency_out_ = latency_builder.snapshot ();
    if (latency_builder.count () == 0)
        return false;
    return true;
}

} // namespace

void run_pubsub (const std::string &transport,
                 size_t msg_size,
                 const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    auto print_fail = [&] () {
        print_fail_result (lib_name, "PUBSUB", transport, msg_size);
    };

    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    pubsub_recv_state_t state;
    state.run_id = next_single_metric_run_id ();
    state.msg_size = msg_size;
    state.payload_size = payload_size;

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail ();
        return;
    }

    socket_guard_t publisher (ctx.get (), ZLINK_SOCKET_PUB);
    socket_guard_t subscriber (ctx.get (), ZLINK_SOCKET_SUB);
    if (!publisher.valid () || !subscriber.valid ()) {
        print_fail ();
        return;
    }

    set_pub_opt_int (
      publisher.get (), ZLINK_PUB_OPT_NODROP, resolve_pubsub_xpub_nodrop_opt (),
      "ZLINK_PUB_OPT_NODROP");
    if (!setup_connected_pubsub_pair (
          publisher.get (), subscriber.get (), transport, lib_name + "_pubsub")) {
        print_fail ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    const int recv_timeout_ms = resolve_single_pubsub_recv_timeout_ms ();
    unsigned long long received = 0;
    latency_stats_t latency;
    if (!run_active_phase (publisher.get (),
                           subscriber.get (),
                           &payload,
                           &state,
                           duration_s,
                           recv_timeout_ms,
                           &received,
                           &latency)) {
        print_fail ();
        return;
    }

    print_result (lib_name,
                  "PUBSUB",
                  transport,
                  msg_size,
                  static_cast<double> (received)
                    / static_cast<double> (duration_s),
                  latency.mean_ns,
                  latency.p95_ns,
                  latency.p99_ns);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, "PUBSUB", run_pubsub);
}
