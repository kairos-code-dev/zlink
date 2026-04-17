#include "../common/bench_common.hpp"
#include "../common/perf_single_latency.hpp"
#include "../common/perf_single_metric_header.hpp"
#include "../common/perf_single_phase.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
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
    spot_recv_state_t () :
        run_id (0),
        msg_size (0),
        active_received (0),
        fatal (false),
        last_recv_ns (0)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    uint32_t run_id;
    size_t msg_size;
    std::atomic<unsigned long long> active_received;
    std::atomic<bool> fatal;
    std::atomic<uint64_t> last_recv_ns;
    latency_stats_builder_t latency;
};

std::string bind_node (void *node_, const std::string &transport_, int base_port_)
{
    return perf_bind_fixed_endpoint_range (
      node_, transport_, base_port_, 64, &perf_bind_spot_node_endpoint);
}

int resolve_spot_subscription_ready_timeout_ms (const std::string &transport_)
{
    if (transport_ == "tls" || transport_ == "wss")
        return 10000;
    return 3000;
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
    const int rc = zlink_subscribe (
      subscriber_, (const zlink_routing_id_t **) NULL, &parts, &part_count, topic, &topic_len,
      static_cast<zlink_recv_flags_t> (flags_));
    if (rc != 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    const bool header_ok =
      topic_matches (topic, topic_len) && parts && part_count >= 1
      && header_out_
      && perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts[0]), zlink_msg_size (&parts[0]), header_out_);
    if (parts)
        zlink_multipart_close (parts, part_count);

    if (header_ok_out_)
        *header_ok_out_ = header_ok;
    return 1;
}

void spot_dispatch_event_handler (void *spot_,
                                  zlink_spot_dispatch_event_t event_,
                                  void *userdata_)
{
    if (event_ != ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE || !spot_
        || !userdata_)
        return;

    spot_recv_state_t *state = static_cast<spot_recv_state_t *> (userdata_);
    while (true) {
        perf_single_metric::header_t header;
        bool header_ok = false;
        const int recv_rc =
          recv_spot_header_flags (spot_, ZLINK_DONTWAIT, &header, &header_ok);
        if (recv_rc > 0) {
            state->last_recv_ns.store (perf_single_metric::now_ns (),
                                       std::memory_order_release);
            if (header_ok && single_header_matches_run (*state, header)) {
                state->active_received.fetch_add (1,
                                                  std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lock (state->mutex);
                    state->latency.add (single_latency_ns (header));
                }
            }
            continue;
        }

        if (recv_rc == 0)
            break;

        state->fatal.store (true, std::memory_order_release);
        break;
    }
    state->cv.notify_all ();
}

bool install_spot_dispatch_handler (void *subscriber_, spot_recv_state_t *state_)
{
    if (!subscriber_ || !state_)
        return false;

    if (zlink_spot_dispatch_event_handler (
          subscriber_, &spot_dispatch_event_handler, state_)
        != ZLINK_HANDLER_OK) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] dispatch handler install failed"
                      << std::endl;
        return false;
    }
    return true;
}

void reset_spot_recv_state (spot_recv_state_t *state_)
{
    if (!state_)
        return;

    state_->active_received.store (0, std::memory_order_release);
    state_->latency = latency_stats_builder_t ();
    state_->fatal.store (false, std::memory_order_release);
    state_->last_recv_ns.store (0, std::memory_order_release);
}

bool wait_for_spot_recv_signal (spot_recv_state_t *state_,
                                std::chrono::milliseconds timeout_,
                                bool require_message_)
{
    if (!state_)
        return false;
    (void) require_message_;

    std::unique_lock<std::mutex> lock (state_->mutex);
    const bool ready = state_->cv.wait_for (lock, timeout_, [&] () {
        return state_->fatal.load (std::memory_order_acquire)
               || state_->active_received.load (std::memory_order_acquire) > 0;
    });
    return ready && !state_->fatal.load (std::memory_order_acquire);
}

bool publish_metric_payload (void *publisher_,
                             std::vector<char> *payload_,
                             size_t msg_size_,
                             uint32_t run_id_,
                             uint64_t seq_,
                             perf_single_metric::phase_t phase_,
                             int flags_)
{
    if (!publisher_ || !payload_)
        return false;

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
    if (zlink_publish (
          publisher_, k_topic, &part, 1,
          static_cast<zlink_send_flags_t> (flags_))
        != 0) {
        return false;
    }

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

    (void) subscriber_;
    reset_spot_recv_state (state_);
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
                                     perf_single_metric::phase_active,
                                     ZLINK_DONTWAIT)) {
            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR) {
                std::this_thread::yield ();
                continue;
            }
            if (bench_debug_enabled ())
                std::cerr << "[perf-spot] probe publish failed err=" << err
                          << std::endl;
            return false;
        }

        const bool ready =
          wait_for_spot_recv_signal (
            state_, std::chrono::milliseconds (50), true);
        if (ready
            && state_->active_received.load (std::memory_order_acquire) > 0)
            return true;
        if (state_->fatal.load (std::memory_order_acquire))
            return false;
    }

    return state_->active_received.load (std::memory_order_acquire) > 0;
}

bool run_spot_post_ready_settle ()
{
    const int settle_ms = resolve_single_spot_ready_settle_ms ();
    return settle_ms <= 0 || perf_socket_poll (NULL, 0, settle_ms) >= 0;
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
        if (!publish_metric_payload (publisher_,
                                     payload_,
                                     state_->msg_size,
                                     state_->run_id,
                                     seq,
                                     perf_single_metric::phase_active,
                                     ZLINK_DONTWAIT)) {
            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR) {
                std::this_thread::yield ();
                continue;
            }
            return false;
        }
        sent_count_->fetch_add (1, std::memory_order_release);
        ++seq;
    }
    return true;
}

bool run_active_window (void *publisher_,
                        void *subscriber_,
                        spot_recv_state_t *state_,
                        int duration_s_,
                        int recv_timeout_ms_,
                        double *throughput_out_,
                        latency_stats_t *latency_out_)
{
    if (!publisher_ || !subscriber_ || !state_ || !throughput_out_
        || !latency_out_) {
        return false;
    }

    (void) subscriber_;
    reset_spot_recv_state (state_);

    std::vector<char> payload;
    std::atomic<unsigned long long> sent_count (0);
    std::atomic<bool> sender_ok (true);
    std::atomic<bool> sender_done (false);
    (void) recv_timeout_ms_;

    std::thread sender_thread ([&]() {
        sender_ok.store (
          send_spot_samples (
            publisher_, &payload, state_, duration_s_, &sent_count),
          std::memory_order_release);
        sender_done.store (true, std::memory_order_release);
        state_->cv.notify_all ();
    });

    sender_thread.join ();
    {
        std::unique_lock<std::mutex> lock (state_->mutex);
        const auto drain_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::seconds (std::max (5, duration_s_ + 2));
        while (!state_->fatal.load (std::memory_order_acquire)) {
            const bool done = sender_done.load (std::memory_order_acquire);
            const uint64_t last_recv_ns =
                state_->last_recv_ns.load (std::memory_order_acquire);
            const unsigned long long sent =
              sent_count.load (std::memory_order_acquire);
            const unsigned long long received =
              state_->active_received.load (std::memory_order_acquire);
            if (done && received >= sent && sent > 0)
                break;
            if (std::chrono::steady_clock::now () >= drain_deadline) {
                if (done && received >= sent)
                    break;
                if (bench_debug_enabled ()) {
                    std::cerr << "[perf-spot] drain deadline hit sent="
                              << sent << " received=" << received
                              << " last_recv_ns=" << last_recv_ns << std::endl;
                }
                break;
            }
            state_->cv.wait_until (lock, drain_deadline);
        }
    }
    if (!sender_ok.load (std::memory_order_acquire)) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] active phase failed sent="
                      << sent_count.load (std::memory_order_relaxed)
                      << " received="
                      << state_->active_received.load (std::memory_order_relaxed)
                      << " latency_count=" << state_->latency.count ()
                      << " latency_mean=" << state_->latency.snapshot ().mean_ns
                      << std::endl;
        }
        return false;
    }

    const unsigned long long recv_total =
      state_->active_received.load (std::memory_order_relaxed);
    latency_stats_t latency_snapshot;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        latency_snapshot = state_->latency.snapshot ();
    }
    if (recv_total == 0 || latency_snapshot.mean_ns <= 0.0)
        return false;

    *throughput_out_ =
      static_cast<double> (recv_total)
      / static_cast<double> (std::max (1, duration_s_));
    *latency_out_ = latency_snapshot;
    return true;
}

void cleanup_spot_case (void **subscriber_,
                        void **publisher_,
                        void **subscriber_node_,
                        void **publisher_node_)
{
    if (bench_debug_enabled ())
        std::cerr << "[perf-spot] cleanup begin" << std::endl;
    if (subscriber_ && *subscriber_)
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] destroy subscriber" << std::endl;
        zlink_spot_destroy (subscriber_);
    if (publisher_ && *publisher_)
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] destroy publisher" << std::endl;
        zlink_spot_destroy (publisher_);
    if (subscriber_node_ && *subscriber_node_)
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] destroy subscriber node" << std::endl;
        (void) zlink_spot_node_destroy (subscriber_node_);
    if (publisher_node_ && *publisher_node_)
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] destroy publisher node" << std::endl;
        (void) zlink_spot_node_destroy (publisher_node_);
    if (bench_debug_enabled ())
        std::cerr << "[perf-spot] cleanup end" << std::endl;
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

    void *publisher_node = zlink_spot_node_new (ctx.get ());
    void *subscriber_node = zlink_spot_node_new (ctx.get ());
    void *publisher = NULL;
    void *subscriber = NULL;
    if (!publisher_node || !subscriber_node) {
        cleanup_spot_case (&subscriber, &publisher, &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    if (!setup_tls_server (publisher_node, transport_)
        || !setup_tls_client (subscriber_node, transport_)) {
        cleanup_spot_case (&subscriber, &publisher, &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    publisher = zlink_spot_new (publisher_node);
    subscriber = zlink_spot_new (subscriber_node);
    if (!publisher || !subscriber) {
        cleanup_spot_case (&subscriber, &publisher, &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    const int linger = 0;
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    const int sndtimeo_ms = resolve_single_send_timeout_ms ();
    const int rcvtimeo_ms = resolve_single_recv_timeout_ms ();
    const int nodrop = 1;
    (void) zlink_set_option (publisher, ZLINK_OPT_LINGER, &linger,
                             sizeof (linger));
    (void) zlink_set_option (publisher, ZLINK_OPT_SNDHWM, &sndhwm,
                             sizeof (sndhwm));
    (void) zlink_set_option (publisher, ZLINK_OPT_SNDTIMEO, &sndtimeo_ms,
                             sizeof (sndtimeo_ms));
    (void) zlink_set_pub_option (publisher, ZLINK_PUB_OPT_NODROP, &nodrop,
                                 sizeof (nodrop));
    (void) zlink_set_option (subscriber, ZLINK_OPT_LINGER, &linger,
                             sizeof (linger));
    (void) zlink_set_option (subscriber, ZLINK_OPT_RCVHWM, &rcvhwm,
                             sizeof (rcvhwm));
    (void) zlink_set_option (subscriber, ZLINK_OPT_RCVTIMEO, &rcvtimeo_ms,
                             sizeof (rcvtimeo_ms));

    const int base_port = 35000 + (current_process_id () % 1000) * 8;
    const std::string endpoint = bind_node (publisher_node, transport_, base_port);
    if (endpoint.empty ()
        || zlink_spot_node_connect_peer (subscriber_node, endpoint.c_str ())
             != ZLINK_CONNECT_OK
        || zlink_set_subscription (subscriber, k_topic) != ZLINK_CONFIG_OK) {
        cleanup_spot_case (&subscriber, &publisher, &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    spot_recv_state_t state;
    state.run_id = next_single_metric_run_id ();
    state.msg_size = msg_size_;
    if (!install_spot_dispatch_handler (subscriber, &state)) {
        cleanup_spot_case (&subscriber, &publisher, &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }
    if (!wait_for_spot_ready_barrier (
          publisher,
          subscriber,
          &state,
          msg_size_,
          resolve_spot_subscription_ready_timeout_ms (transport_))) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] ready barrier failed" << std::endl;
        cleanup_spot_case (&subscriber, &publisher, &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }
    if (!run_spot_post_ready_settle ()) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] post-ready settle failed" << std::endl;
        cleanup_spot_case (&subscriber, &publisher, &subscriber_node,
                           &publisher_node);
        print_fail ();
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok = run_active_window (publisher,
                                              subscriber,
                                              &state,
                                              std::max (
                                                1, resolve_single_duration_seconds ()),
                                              resolve_single_recv_timeout_ms (),
                                              &throughput,
                                              &latency);
    if (bench_debug_enabled () && !active_ok)
        std::cerr << "[perf-spot] active window failed" << std::endl;
    cleanup_spot_case (&subscriber, &publisher, &subscriber_node,
                       &publisher_node);
    if (!active_ok) {
        print_fail ();
        return 1;
    }

    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  throughput,
                  latency.mean_ns,
                  latency.p95_ns,
                  latency.p99_ns);
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
