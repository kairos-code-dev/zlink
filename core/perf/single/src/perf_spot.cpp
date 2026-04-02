#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include "../../common/perf_spot_handle.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

namespace
{
static const char *k_pattern = "SPOT";
static const char *k_topic = "bench";
static const size_t k_metric_queue_capacity = 65536;

bool validate_spot_target_recv_mode ()
{
    if (!single_perf_callback_mode ()) {
        std::cerr << "policy violation: perf_spot requires --recv callback"
                  << std::endl;
        return false;
    }
    return true;
}

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

struct spot_client_state_t
{
    spot_client_state_t () :
        run_id (0),
        msg_size (0),
        active_deadline_us (0),
        fatal (false),
        warmup_received (0),
        active_received (0),
        probe (NULL),
        callback_queue (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
    std::atomic<unsigned long long> warmup_received;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
    queue_probe_t *probe;
    single_callback_metric_queue_t *callback_queue;
    std::mutex mutex;
    std::mutex latency_mutex;
    std::condition_variable cv;
};

std::atomic<spot_client_state_t *> g_spot_client_state (NULL);

struct spot_client_state_scope_t
{
    explicit spot_client_state_scope_t (spot_client_state_t *state_) :
        state (state_)
    {
        g_spot_client_state.store (state_, std::memory_order_release);
    }

    ~spot_client_state_scope_t ()
    {
        g_spot_client_state.store (NULL, std::memory_order_release);
    }

    spot_client_state_t *state;
};

std::string bind_node (void *node_, const std::string &transport_, int base_port_)
{
    return perf_bind_fixed_endpoint_range (
      node_, transport_, base_port_, 64, &perf_bind_spot_node_endpoint);
}

int resolve_spot_subscription_ready_timeout_ms (
  const std::string &transport_)
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

bool decode_spot_metric_header (spot_client_state_t *state_,
                                const char *topic_,
                                size_t topic_len_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                perf_single_metric::header_t *header_out_)
{
    if (!state_ || !topic_ || !parts_ || part_count_ == 0 || !header_out_
        || !topic_matches (topic_, topic_len_)) {
        return false;
    }

    const bool header_ok =
      perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), header_out_)
      && header_out_->run_id == state_->run_id
      && header_out_->msg_size == state_->msg_size;
    if (!header_ok)
        return false;

    return true;
}

void spot_client_handler (const zlink_routing_id_t *,
                          const char *topic_,
                          size_t topic_len_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *)
{
    spot_client_state_t *state =
      g_spot_client_state.load (std::memory_order_acquire);
    perf_single_metric::header_t header;
    const bool header_ok = decode_spot_metric_header (
      state, topic_, topic_len_, parts_, part_count_, &header);
    perf_close_multipart (parts_, part_count_);
    if (!state || !header_ok)
        return;

    single_note_callback_receive (state, header);
    (void) single_enqueue_metric_event (state, header);
}

void cleanup_spot_case (service_event_probe_t *sub_monitor_,
                        service_event_probe_t *pub_monitor_,
                        void **sub_handle_,
                        void **pub_handle_,
                        void **sub_node_,
                        void **pub_node_)
{
    if (sub_monitor_)
        close_service_event_probe (*sub_monitor_);
    if (pub_monitor_)
        close_service_event_probe (*pub_monitor_);
    if (sub_handle_ && *sub_handle_)
        perf_destroy_default_spot_handle (sub_handle_);
    if (pub_handle_ && *pub_handle_)
        perf_destroy_default_spot_handle (pub_handle_);
    if (sub_node_ && *sub_node_)
        (void) zlink_spot_node_destroy (sub_node_);
    if (pub_node_ && *pub_node_)
        (void) zlink_spot_node_destroy (pub_node_);
}

bool publish_payload (void *pub_,
                      std::vector<char> &payload_,
                      size_t msg_size_,
                      uint32_t run_id_,
                      perf_single_metric::phase_t phase_,
                      uint64_t seq_,
                      int flags_)
{
    const size_t payload_size =
      std::max (msg_size_, perf_single_metric::header_size ());
    if (payload_.size () != payload_size)
        payload_.assign (payload_size, 's');
    if (!perf_single_metric::stamp_payload (
          payload_.data (),
          payload_.size (),
          run_id_,
          phase_,
          msg_size_,
          seq_,
          perf_single_metric::now_us ())) {
        return false;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size (&part, payload_.size ()) != 0)
        return false;
    std::memcpy (zlink_msg_data (&part), payload_.data (), payload_.size ());
    if (zlink_publish (pub_, k_topic, &part, 1, flags_) != 0) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
        return false;
    }

    return true;
}

bool run_publish_window (void *pub_,
                         spot_client_state_t &client_state_,
                         queue_probe_t &probe_,
                         std::vector<char> &payload_,
                         size_t msg_size_,
                         perf_single_metric::phase_t phase_,
                         int duration_s_,
                         unsigned long long *out_sent_count_)
{
    if (out_sent_count_)
        *out_sent_count_ = 0;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (std::max (0, duration_s_));
    uint64_t seq = 1;
    unsigned long long sent_count = 0;

    while (std::chrono::steady_clock::now () < deadline) {
        if (client_state_.fatal.load (std::memory_order_acquire))
            return false;

        if (!publish_payload (pub_,
                              payload_,
                              msg_size_,
                              client_state_.run_id,
                              phase_,
                              seq,
                              0)) {
            if (errno == EINTR)
                continue;
            return false;
        }
        probe_.sample_send_if_due ();
        ++sent_count;
        ++seq;
    }

    if (out_sent_count_)
        *out_sent_count_ = sent_count;
    return !client_state_.fatal.load (std::memory_order_acquire);
}

bool run_phase_window (void *pub_,
                       spot_client_state_t &client_state_,
                       queue_probe_t &probe_,
                       std::vector<char> &payload_,
                       size_t msg_size_,
                       perf_single_metric::phase_t phase_,
                       int duration_s_,
                       double *throughput_out_,
                       latency_stats_t *latency_out_)
{
    client_state_.fatal.store (false, std::memory_order_release);
    client_state_.warmup_received.store (0, std::memory_order_release);
    client_state_.active_received.store (0, std::memory_order_release);
    client_state_.active_deadline_us.store (
      phase_ == perf_single_metric::phase_active
        ? perf_single_metric::now_us ()
            + static_cast<uint64_t> (std::max (1, duration_s_) * 1000000ULL)
        : 0,
      std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock (client_state_.latency_mutex);
        client_state_.latency = latency_stats_builder_t ();
    }

    unsigned long long sent_count = 0;
    if (!run_publish_window (pub_,
                             client_state_,
                             probe_,
                             payload_,
                             msg_size_,
                             phase_,
                             duration_s_,
                             &sent_count)) {
        return false;
    }

    const bool active_phase = phase_ == perf_single_metric::phase_active;
    const int drain_timeout_ms =
      single_phase_drain_timeout_ms (
        duration_s_, resolve_single_recv_timeout_ms ());
    const bool drained = single_wait_for_phase_processed (
      client_state_, phase_, sent_count, drain_timeout_ms);
    if (!drained) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot] phase drain failed phase="
                      << static_cast<int> (phase_)
                      << " sent=" << sent_count
                      << " processed="
                      << single_load_phase_received (client_state_, phase_)
                      << " fatal="
                      << (client_state_.fatal.load (
                            std::memory_order_acquire)
                            ? 1
                            : 0)
                      << std::endl;
        }
        return false;
    }
    if (phase_ != perf_single_metric::phase_active) {
        return !client_state_.fatal.load (std::memory_order_acquire)
               && client_state_.warmup_received.load (std::memory_order_acquire)
                    > 0;
    }

    client_state_.active_deadline_us.store (0, std::memory_order_release);
    const double elapsed_s = std::max (0.001, static_cast<double> (duration_s_));
    if (throughput_out_) {
        *throughput_out_ =
          static_cast<double> (
            client_state_.active_received.load (std::memory_order_acquire))
          / elapsed_s;
    }
    if (latency_out_) {
        std::lock_guard<std::mutex> lock (client_state_.latency_mutex);
        *latency_out_ = client_state_.latency.snapshot ();
    }

    return !client_state_.fatal.load (std::memory_order_acquire)
           && client_state_.active_received.load (std::memory_order_acquire)
                > 0;
}

int run_case (const std::string &lib_name_,
              const std::string &transport_,
              size_t msg_size_)
{
    if (!perf_supports_service_transport (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_
                  << std::endl;
        return 0;
    }
    if (!transport_available (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_
                  << std::endl;
        return 0;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    void *pub_node = zlink_spot_node_new (ctx.get ());
    void *sub_node = zlink_spot_node_new (ctx.get ());
    void *pub = NULL;
    void *sub = NULL;
    if (!pub_node || !sub_node) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] node create failed err=" << zlink_errno ()
                      << std::endl;
        if (pub_node)
            zlink_spot_node_destroy (&pub_node);
        if (sub_node)
            zlink_spot_node_destroy (&sub_node);
        return 1;
    }

    const int linger = 0;
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    const int sndtimeo_ms = resolve_single_send_timeout_ms ();
    const int rcvtimeo_ms = resolve_single_recv_timeout_ms ();
    const int nodrop = 1;
    if (!setup_tls_server (pub_node, transport_)
        || !setup_tls_client (sub_node, transport_)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] tls configure failed err="
                      << zlink_errno () << std::endl;
        zlink_spot_node_destroy (&sub_node);
        zlink_spot_node_destroy (&pub_node);
        return 1;
    }

    pub = perf_create_default_spot_handle (pub_node);
    sub = perf_create_default_spot_handle (sub_node);
    if (!pub || !sub) {
        perf_destroy_default_spot_handle (&sub);
        perf_destroy_default_spot_handle (&pub);
        zlink_spot_node_destroy (&sub_node);
        zlink_spot_node_destroy (&pub_node);
        return 1;
    }

    (void) zlink_set_option (pub, ZLINK_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_set_option (pub, ZLINK_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_set_option (
      pub, ZLINK_OPT_SNDTIMEO, &sndtimeo_ms, sizeof (sndtimeo_ms));
    (void) zlink_set_pub_option (pub, ZLINK_PUB_OPT_NODROP, &nodrop,
                                 sizeof (nodrop));
    (void) zlink_set_option (sub, ZLINK_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_set_option (sub, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));
    (void) zlink_set_option (
      sub, ZLINK_OPT_RCVTIMEO, &rcvtimeo_ms, sizeof (rcvtimeo_ms));

    if (zlink_subscribe_handler (sub, &spot_client_handler, NULL) != 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] callback handler attach failed err="
                      << zlink_errno () << std::endl;
        perf_destroy_default_spot_handle (&sub);
        perf_destroy_default_spot_handle (&pub);
        zlink_spot_node_destroy (&sub_node);
        zlink_spot_node_destroy (&pub_node);
        return 1;
    }

    service_event_probe_t sub_monitor;
    service_event_probe_t pub_monitor;
    single_callback_metric_queue_t metric_queue (k_metric_queue_capacity);
    single_metric_worker_t<spot_client_state_t> metric_worker;
    if (!open_service_event_probe (
          sub,
          ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED
            | ZLINK_MONITOR_EVENT_ERROR,
          ZLINK_MONITOR_EVENT_ERROR,
          sub_monitor)
        || !open_service_event_probe (
          pub,
          ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED
            | ZLINK_MONITOR_EVENT_ERROR,
          ZLINK_MONITOR_EVENT_ERROR,
          pub_monitor)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] monitor open failed sub="
                      << (sub_monitor.monitor != NULL) << " pub="
                      << (pub_monitor.monitor != NULL) << " err="
                      << zlink_errno () << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    const int base_port = 35000 + (current_process_id () % 1000) * 8;
    const std::string endpoint = bind_node (pub_node, transport_, base_port);
    if (endpoint.empty ()) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] bind failed err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    if (zlink_spot_node_connect_peer (sub_node, endpoint.c_str ()) != 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] connect peer failed err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    if (zlink_set_subscription (sub, k_topic) != 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] subscribe failed err=" << zlink_errno ()
                      << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    spot_client_state_t client_state;
    client_state.run_id = static_cast<uint32_t> (current_process_id ());
    client_state.msg_size = msg_size_;
    client_state.callback_queue = &metric_queue;
    spot_client_state_scope_t client_state_scope (&client_state);
    queue_probe_t probe (pub, sub);
    client_state.probe = &probe;
    metric_worker.state = &client_state;
    metric_worker.queue = &metric_queue;

    if (!wait_for_service_event (sub_monitor,
                                    ZLINK_SPOT_MONITOR_EVENT_SUB_DELIVERY_READY_CHANGED,
                                    NULL,
                                    k_topic,
                                    1,
                                    resolve_spot_subscription_ready_timeout_ms (
                                      transport_))
        || !wait_for_service_event (pub_monitor,
                                    ZLINK_SPOT_MONITOR_EVENT_PUB_FIRST_DELIVERY_READY_CHANGED,
                                    NULL,
                                    k_topic,
                                    1,
                                    resolve_spot_subscription_ready_timeout_ms (
                                      transport_))) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot] ready wait failed" << std::endl;
        cleanup_spot_case (
          &sub_monitor, &pub_monitor, &sub, &pub, &sub_node, &pub_node);
        return 1;
    }

    if (!start_single_metric_worker (&metric_worker)) {
        cleanup_spot_case (&sub_monitor, &pub_monitor, &sub, &pub, &sub_node,
                           &pub_node);
        return 1;
    }

    std::vector<char> payload;
    if (!run_phase_window (
          pub, client_state, probe, payload, msg_size_,
          perf_single_metric::phase_warmup,
          resolve_single_warmup_seconds (), NULL, NULL)) {
        stop_single_metric_worker (&metric_worker);
        const queue_stats_t queue_stats = probe.snapshot ();
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        cleanup_spot_case (&sub_monitor, &pub_monitor, &sub, &pub, &sub_node,
                           &pub_node);
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok = run_phase_window (
      pub, client_state, probe, payload, msg_size_,
      perf_single_metric::phase_active, resolve_single_duration_seconds (),
      &throughput, &latency);
    stop_single_metric_worker (&metric_worker);
    const queue_stats_t queue_stats = probe.snapshot ();
    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  active_ok ? throughput : 0.0,
                  active_ok ? latency.mean_us : 0.0,
                  active_ok ? latency.p95_us : 0.0,
                  active_ok ? latency.p99_us : 0.0,
                  queue_stats);

    cleanup_spot_case (&sub_monitor, &pub_monitor, &sub, &pub, &sub_node,
                       &pub_node);
    return active_ok ? 0 : 1;
}
} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!single_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;
    if (!validate_spot_target_recv_mode ())
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t msg_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    return run_case (lib_name, transport, msg_size > 0 ? msg_size : 64);
}
