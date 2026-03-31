#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
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
    if (!env || !*env)
        return 1;

    if (std::strcmp (env, "1") == 0 || std::strcmp (env, "true") == 0
        || std::strcmp (env, "TRUE") == 0 || std::strcmp (env, "on") == 0
        || std::strcmp (env, "ON") == 0) {
        return 1;
    }
    if (std::strcmp (env, "0") == 0 || std::strcmp (env, "false") == 0
        || std::strcmp (env, "FALSE") == 0 || std::strcmp (env, "off") == 0
        || std::strcmp (env, "OFF") == 0) {
        return 0;
    }
    return 1;
}

struct pubsub_callback_state_t
{
    pubsub_callback_state_t () :
        run_id (0),
        msg_size (0),
        payload_size (0),
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
    size_t payload_size;
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

struct pubsub_recv_state_t
{
    pubsub_recv_state_t () :
        run_id (0),
        msg_size (0),
        payload_size (0),
        active_deadline_us (0),
        fatal (false),
        warmup_received (0),
        active_received (0),
        probe (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
    std::atomic<unsigned long long> warmup_received;
    std::atomic<unsigned long long> active_received;
    latency_stats_builder_t latency;
    queue_probe_t *probe;
    std::mutex mutex;
    std::mutex latency_mutex;
    std::condition_variable cv;
};

struct pubsub_recv_loop_t
{
    pubsub_recv_loop_t () :
        sub_socket (NULL), state (NULL), stop (false), ready (false)
    {
    }

    void *sub_socket;
    pubsub_recv_state_t *state;
    std::atomic<bool> stop;
    std::atomic<bool> ready;
    std::thread thread;
};

inline int recv_pubsub_metric_header_flags (
  void *sub_socket_,
  pubsub_recv_state_t *state_,
  int flags_,
  perf_single_metric::header_t *header_out_)
{
    if (!sub_socket_ || !state_ || !header_out_)
        return -1;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic_id[256];
    size_t topic_len = sizeof (topic_id);
    const int subscribe_rc = zlink_subscribe (
      sub_socket_, NULL, &parts, &part_count, topic_id, &topic_len,
      static_cast<zlink_send_flags_t> (flags_));
    if (subscribe_rc < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }
    if (!parts || part_count != 1)
        goto recv_fail;

    {
        const bool topic_ok =
          topic_len == std::strlen (k_pubsub_topic)
          && std::memcmp (topic_id, k_pubsub_topic, topic_len) == 0;
        const size_t actual_size = zlink_msg_size (&parts[0]);
        const bool size_ok = actual_size == state_->payload_size;
        const bool header_ok =
          topic_ok && size_ok
          && perf_single_metric::decode_payload_header (
            zlink_msg_data (&parts[0]), actual_size, header_out_)
          && header_out_->run_id == state_->run_id
          && header_out_->msg_size == state_->msg_size;

        zlink_multipart_close (parts, part_count);

        return header_ok ? 1 : -1;
    }

recv_fail:
    if (parts)
        zlink_multipart_close (parts, part_count);
    return -1;
}

inline void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
}

template <typename StateT>
bool decode_pubsub_metric_header (StateT *state_,
                                  const char *topic_,
                                  size_t topic_len_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  perf_single_metric::header_t *header_out_)
{
    if (!state_ || !topic_ || !parts_ || part_count_ != 1 || !header_out_)
        return false;

    const bool topic_ok =
      topic_len_ == std::strlen (k_pubsub_topic)
      && std::memcmp (topic_, k_pubsub_topic, topic_len_) == 0;
    if (!topic_ok)
        return false;

    const size_t actual_size = zlink_msg_size (&parts_[0]);
    if (actual_size != state_->payload_size)
        return false;

    return perf_single_metric::decode_payload_header (
             zlink_msg_data (&parts_[0]), actual_size, header_out_)
           && header_out_->run_id == state_->run_id
           && header_out_->msg_size == state_->msg_size;
}

template <typename StateT>
void account_pubsub_metric_header (
  StateT *state_,
  const perf_single_metric::header_t &header_)
{
    if (!state_)
        return;

    single_note_callback_receive (state_, header_);
}

void pubsub_recv_handler (const zlink_routing_id_t *,
                          const char *topic_,
                          size_t topic_len_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          void *userdata_)
{
    pubsub_callback_state_t *state =
      static_cast<pubsub_callback_state_t *> (userdata_);
    if (!state) {
        close_parts (parts_, part_count_);
        return;
    }

    perf_single_metric::header_t header;
    const bool header_ok = decode_pubsub_metric_header (
      state, topic_, topic_len_, parts_, part_count_, &header);

    close_parts (parts_, part_count_);
    if (!header_ok)
        return;

    single_note_callback_receive (state, header);
    (void) single_enqueue_metric_event (state, header);
}

inline bool send_pubsub_sample (void *pub_socket_,
                                std::vector<char> *payload_,
                                size_t payload_size_,
                                size_t msg_size_,
                                uint32_t run_id_,
                                uint64_t *seq_,
                                perf_single_metric::phase_t phase_,
                                int flags_)
{
    if (!pub_socket_ || !payload_ || !seq_)
        return false;

    const uint64_t sent_ts = perf_single_metric::now_us ();
    const uint64_t current_seq = *seq_;
    if (!perf_single_metric::stamp_payload (payload_->data (),
                                            payload_size_,
                                            run_id_,
                                            phase_,
                                            msg_size_,
                                            current_seq,
                                            sent_ts)) {
        return false;
    }

    zlink_msg_t payload_part;
    if (zlink_msg_init_size (&payload_part, payload_size_) != 0)
        return false;

    std::memcpy (
      zlink_msg_data (&payload_part), payload_->data (), payload_size_);
    if (::zlink_publish (pub_socket_, k_pubsub_topic, &payload_part, 1, flags_)
        < 0) {
        zlink_msg_close (&payload_part);
        return false;
    }

    ++(*seq_);
    return true;
}

inline bool setup_connected_pubsub_pair (void *pub_socket_,
                                         void *sub_socket_,
                                         const std::string &transport_,
                                         const std::string &id_)
{
    if (!pub_socket_ || !sub_socket_)
        return false;

    if (!setup_tls_server (pub_socket_, transport_)
        || !setup_tls_client (sub_socket_, transport_)) {
        return false;
    }

    apply_single_hwm (pub_socket_);
    apply_single_hwm (sub_socket_);

    if (zlink_set_subscription (sub_socket_, "") != 0)
        return false;

    const std::string endpoint =
      bind_and_resolve_endpoint (pub_socket_, transport_, id_);
    if (endpoint.empty ())
        return false;

    void *sub_monitor = open_configured_socket_monitor (
      sub_socket_, ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED);
    if (!sub_monitor)
        return false;
    void *pub_monitor = open_configured_socket_monitor (
      pub_socket_, ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED);
    if (!pub_monitor) {
        zlink_monitor_close (&sub_monitor);
        return false;
    }

    if (!connect_checked (sub_socket_, endpoint))
    {
        zlink_monitor_close (&pub_monitor);
        zlink_monitor_close (&sub_monitor);
        return false;
    }

    apply_single_benchmark_socket_options (pub_socket_, transport_);
    apply_single_benchmark_socket_options (sub_socket_, transport_);

    const int timeout_ms = parse_positive_env ("PERF_CONNECT_READY_TIMEOUT_MS",
                                               3000);
    const bool sub_ready = wait_for_socket_monitor_event (
      sub_monitor,
      ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED,
      timeout_ms);
    const bool pub_ready = wait_for_socket_monitor_event (
      pub_monitor,
      ZLINK_EVENT_PUB_DELIVERY_READY_CHANGED,
      timeout_ms);
    zlink_monitor_close (&pub_monitor);
    zlink_monitor_close (&sub_monitor);

    if (bench_debug_enabled () && !(sub_ready && pub_ready)) {
        std::cerr << "[perf-pubsub] delivery-ready gate failed"
                  << " sub_ready=" << (sub_ready ? 1 : 0)
                  << " pub_ready=" << (pub_ready ? 1 : 0) << std::endl;
    }
    return sub_ready && pub_ready;
}

inline int send_pubsub_sample_blocking (void *pub_socket_,
                                        std::vector<char> *payload_,
                                        size_t payload_size_,
                                        size_t msg_size_,
                                        uint32_t run_id_,
                                        uint64_t *seq_,
                                        perf_single_metric::phase_t phase_)
{
    while (true) {
        if (send_pubsub_sample (pub_socket_,
                                payload_,
                                payload_size_,
                                msg_size_,
                                run_id_,
                                seq_,
                                phase_,
                                0)) {
            return 1;
        }

        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        return err == EAGAIN ? 0 : -1;
    }
}

void start_pubsub_recv_loop (pubsub_recv_loop_t *loop_)
{
    if (!loop_ || !loop_->sub_socket || !loop_->state)
        return;

    loop_->stop.store (false, std::memory_order_release);
    loop_->ready.store (false, std::memory_order_release);
    loop_->thread = std::thread ([loop_]() {
        loop_->ready.store (true, std::memory_order_release);

        while (!loop_->stop.load (std::memory_order_acquire)) {
            perf_single_metric::header_t header;
            const int rc = recv_pubsub_metric_header_flags (
              loop_->sub_socket, loop_->state, 0, &header);
            if (rc > 0) {
                account_pubsub_metric_header (loop_->state, header);
                continue;
            }
            if (rc == 0)
                continue;

            const int err = zlink_errno ();
            if (err == EINTR || err == EAGAIN)
                continue;
            single_mark_callback_fatal (loop_->state);
            break;
        }
    });
}

void stop_pubsub_recv_loop (pubsub_recv_loop_t *loop_)
{
    if (!loop_)
        return;
    loop_->stop.store (true, std::memory_order_release);
    if (loop_->thread.joinable ())
        loop_->thread.join ();
}

bool wait_pubsub_recv_loop_ready (pubsub_recv_loop_t *loop_, int timeout_ms_)
{
    if (!loop_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    while (std::chrono::steady_clock::now () < deadline) {
        if (loop_->ready.load (std::memory_order_acquire))
            return true;
        const long remaining_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - std::chrono::steady_clock::now ())
            .count ();
        const long wait_ms = remaining_ms > 0
                               ? std::min<long> (
                                   remaining_ms, perf_aux_poll_wait_ms ())
                               : 0;
        if (perf_socket_poll (NULL, 0, wait_ms) < 0 && zlink_errno () != EINTR)
            return false;
    }
    return loop_->ready.load (std::memory_order_acquire);
}

inline bool run_oneway_phase (void *pub_socket,
                              std::vector<char> *payload,
                              pubsub_callback_state_t *state,
                              uint64_t *seq,
                              perf_single_metric::phase_t phase,
                              int duration_s,
                              int recv_timeout_ms,
                              queue_probe_t *queue_probe,
                              unsigned long long *out_received,
                              latency_stats_t *out_latency)
{
    if (!pub_socket || !payload || !state || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now ()
        + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    state->fatal.store (false, std::memory_order_release);
    state->warmup_received.store (0, std::memory_order_release);
    state->active_received.store (0, std::memory_order_release);
    state->active_deadline_us.store (
      active_phase
        ? perf_single_metric::now_us ()
            + static_cast<uint64_t> (std::max (1, duration_s) * 1000000ULL)
        : 0,
      std::memory_order_release);
    state->probe = queue_probe;
    {
        std::lock_guard<std::mutex> lock (state->latency_mutex);
        state->latency = latency_stats_builder_t ();
    }

    bool send_failed = false;
    unsigned long long successful_send_count = 0;
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const int send_rc = send_pubsub_sample_blocking (
          pub_socket,
          payload,
          state->payload_size,
          state->msg_size,
          state->run_id,
          seq,
          phase);
        if (send_rc < 0) {
            send_failed = true;
            break;
        }
        if (send_rc == 0) {
            if (!single_wait_for_send_backpressure (queue_probe)) {
                send_failed = true;
                break;
            }
            continue;
        }
        ++successful_send_count;
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    const int drain_timeout_ms =
      single_phase_drain_timeout_ms (duration_s, recv_timeout_ms);
    const bool drained = single_wait_for_phase_processed (
      *state, phase, successful_send_count, drain_timeout_ms);

    if (send_failed || !drained
        || state->fatal.load (std::memory_order_acquire))
        return false;

    *out_received = active_phase
                      ? state->active_received.load (std::memory_order_relaxed)
                      : state->warmup_received.load (std::memory_order_relaxed);

    if (active_phase) {
        state->active_deadline_us.store (0, std::memory_order_release);
        if (*out_received == 0 || !out_latency) {
            return false;
        }
        std::lock_guard<std::mutex> lock (state->latency_mutex);
        *out_latency = state->latency.snapshot ();
        if (state->latency.count () == 0)
            return false;
    } else if (*out_received == 0) {
        return false;
    }

    return true;
}

inline bool run_oneway_phase_recv (void *pub_socket,
                                   std::vector<char> *payload,
                                   pubsub_recv_state_t *state,
                                   uint64_t *seq,
                                   perf_single_metric::phase_t phase,
                                   int duration_s,
                                   int recv_timeout_ms,
                                   queue_probe_t *queue_probe,
                                   unsigned long long *out_received,
                                   latency_stats_t *out_latency)
{
    if (!pub_socket || !payload || !state || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    state->fatal.store (false, std::memory_order_release);
    state->warmup_received.store (0, std::memory_order_release);
    state->active_received.store (0, std::memory_order_release);
    state->active_deadline_us.store (
      active_phase
        ? perf_single_metric::now_us ()
            + static_cast<uint64_t> (std::max (1, duration_s) * 1000000ULL)
        : 0,
      std::memory_order_release);
    state->probe = queue_probe;
    {
        std::lock_guard<std::mutex> lock (state->latency_mutex);
        state->latency = latency_stats_builder_t ();
    }

    bool send_failed = false;
    unsigned long long successful_send_count = 0;
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const int send_rc = send_pubsub_sample_blocking (
          pub_socket,
          payload,
          state->payload_size,
          state->msg_size,
          state->run_id,
          seq,
          phase);
        if (send_rc < 0) {
            send_failed = true;
            break;
        }
        if (send_rc == 0) {
            if (!single_wait_for_send_backpressure (queue_probe)) {
                send_failed = true;
                break;
            }
            continue;
        }
        ++successful_send_count;
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    const int drain_timeout_ms =
      single_phase_drain_timeout_ms (duration_s, recv_timeout_ms);
    const bool drained = single_wait_for_phase_processed (
      *state, phase, successful_send_count, drain_timeout_ms);

    if (send_failed || !drained
        || state->fatal.load (std::memory_order_acquire))
        return false;

    *out_received = active_phase
                      ? state->active_received.load (std::memory_order_relaxed)
                      : state->warmup_received.load (std::memory_order_relaxed);

    if (active_phase) {
        state->active_deadline_us.store (0, std::memory_order_release);
        if (*out_received == 0 || !out_latency) {
            return false;
        }
        std::lock_guard<std::mutex> lock (state->latency_mutex);
        *out_latency = state->latency.snapshot ();
        if (state->latency.count () == 0)
            return false;
    } else if (*out_received == 0) {
        return false;
    }

    return true;
}

} // namespace

void run_pubsub (const std::string &transport,
                 size_t msg_size,
                 const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    auto print_fail_no_queue = [&] () {
        print_fail_result (lib_name, "PUBSUB", transport, msg_size);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail_no_queue ();
        return;
    }

    socket_guard_t pub (ctx.get (), ZLINK_SOCKET_PUB);
    socket_guard_t sub (ctx.get (), ZLINK_SOCKET_SUB);
    if (!pub.valid () || !sub.valid ()) {
        print_fail_no_queue ();
        return;
    }

    const int xpub_nodrop_opt = resolve_pubsub_xpub_nodrop_opt ();
    set_pub_opt_int (pub.get (), ZLINK_PUB_OPT_NODROP, xpub_nodrop_opt,
                     "ZLINK_PUB_OPT_NODROP");

    if (!setup_connected_pubsub_pair (
          pub.get (), sub.get (), transport, lib_name + "_pubsub")) {
        print_fail_no_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_pubsub_recv_timeout_ms ();
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    queue_probe_t queue_probe (pub.get (), sub.get ());
    auto print_fail_with_queue = [&] () {
        print_fail_result (lib_name, "PUBSUB", transport, msg_size, &queue_probe);
    };
    uint64_t seq = 1;
    unsigned long long warmup_received = 0;
    const int warmup_s = resolve_single_warmup_seconds ();
    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;

    if (single_perf_callback_mode ()) {
        pubsub_callback_state_t callback_state;
        single_callback_metric_queue_t callback_queue (65536);
        single_metric_worker_t<pubsub_callback_state_t> metric_worker;

        callback_state.run_id =
          static_cast<uint32_t> (perf_single_metric::now_us ());
        callback_state.msg_size = msg_size;
        callback_state.payload_size = payload_size;
        callback_state.callback_queue = &callback_queue;
        metric_worker.state = &callback_state;
        metric_worker.queue = &callback_queue;
        if (zlink_subscribe_handler (
              sub.get (), &pubsub_recv_handler, &callback_state)
            != 0) {
            print_fail_with_queue ();
            return;
        }
        if (!start_single_metric_worker (&metric_worker)) {
            print_fail_with_queue ();
            return;
        }

        if (!run_oneway_phase (pub.get (),
                               &payload,
                               &callback_state,
                               &seq,
                               perf_single_metric::phase_warmup,
                               warmup_s,
                               recv_timeout_ms,
                               NULL,
                               &warmup_received,
                               NULL)) {
            stop_single_metric_worker (&metric_worker);
            print_fail_with_queue ();
            return;
        }

        if (!run_oneway_phase (pub.get (),
                               &payload,
                               &callback_state,
                               &seq,
                               perf_single_metric::phase_active,
                               duration_s,
                               recv_timeout_ms,
                               &queue_probe,
                               &received,
                               &latency_stats)) {
            stop_single_metric_worker (&metric_worker);
            print_fail_with_queue ();
            return;
        }
        stop_single_metric_worker (&metric_worker);
    } else {
        pubsub_recv_state_t recv_state;
        pubsub_recv_loop_t recv_loop;

        recv_state.run_id =
          static_cast<uint32_t> (perf_single_metric::now_us ());
        recv_state.msg_size = msg_size;
        recv_state.payload_size = payload_size;
        recv_loop.sub_socket = sub.get ();
        recv_loop.state = &recv_state;
        start_pubsub_recv_loop (&recv_loop);
        if (!wait_pubsub_recv_loop_ready (&recv_loop, 1000)) {
            stop_pubsub_recv_loop (&recv_loop);
            print_fail_with_queue ();
            return;
        }

        if (!run_oneway_phase_recv (pub.get (),
                                    &payload,
                                    &recv_state,
                                    &seq,
                                    perf_single_metric::phase_warmup,
                                    warmup_s,
                                    recv_timeout_ms,
                                    NULL,
                                    &warmup_received,
                                    NULL)) {
            stop_pubsub_recv_loop (&recv_loop);
            print_fail_with_queue ();
            return;
        }

        if (!run_oneway_phase_recv (pub.get (),
                                    &payload,
                                    &recv_state,
                                    &seq,
                                    perf_single_metric::phase_active,
                                    duration_s,
                                    recv_timeout_ms,
                                    &queue_probe,
                                    &received,
                                    &latency_stats)) {
            stop_pubsub_recv_loop (&recv_loop);
            print_fail_with_queue ();
            return;
        }
        stop_pubsub_recv_loop (&recv_loop);
    }

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    const queue_stats_t queue_stats = queue_probe.snapshot ();

    print_result (lib_name,
                  "PUBSUB",
                  transport,
                  msg_size,
                  throughput,
                  latency_stats.mean_us,
                  latency_stats.p95_us,
                  latency_stats.p99_us,
                  queue_stats);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, "PUBSUB", run_pubsub);
}
