#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace {

inline void debug_router_router (const char *message_)
{
    if (bench_debug_enabled ())
        std::cerr << "[perf-router-router] " << message_ << std::endl;
}

struct router_router_callback_state_t
{
    router_router_callback_state_t () :
        run_id (0),
        msg_size (0),
        payload_size (0),
        expected_source_rid (NULL),
        expected_source_rid_size (0),
        active_deadline_us (0),
        fatal (false),
        warmup_received (0),
        active_received (0),
        recv_activity (0),
        callback_enqueued (0),
        callback_processed (0),
        probe (NULL),
        callback_queue (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    size_t payload_size;
    const char *expected_source_rid;
    size_t expected_source_rid_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<bool> fatal;
    std::atomic<unsigned long long> warmup_received;
    std::atomic<unsigned long long> active_received;
    std::atomic<unsigned long long> recv_activity;
    std::atomic<unsigned long long> callback_enqueued;
    std::atomic<unsigned long long> callback_processed;
    latency_stats_builder_t latency;
    queue_probe_t *probe;
    single_callback_metric_queue_t *callback_queue;
    std::mutex mutex;
    std::mutex latency_mutex;
    std::mutex callback_wait_mutex;
    std::condition_variable cv;
    std::condition_variable callback_wait_cv;
};

inline void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
}

inline bool wait_for_receive_quiet (router_router_callback_state_t &state_,
                                    int idle_timeout_ms_,
                                    int total_timeout_ms_)
{
    return single_wait_for_receive_quiet (
      state_, idle_timeout_ms_, total_timeout_ms_);
}

void router_router_recv_handler (const zlink_routing_id_t *source_rid_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 void *userdata_)
{
    router_router_callback_state_t *state =
      static_cast<router_router_callback_state_t *> (userdata_);
    if (!state) {
        close_parts (parts_, part_count_);
        return;
    }

    bool fatal = false;
    perf_single_metric::header_t header;
    bool header_ok = false;
    const bool rid_ok =
      source_rid_ && source_rid_->size == state->expected_source_rid_size
      && std::memcmp (source_rid_->data,
                      state->expected_source_rid,
                      state->expected_source_rid_size)
           == 0;
    if (!rid_ok || part_count_ != 1) {
        fatal = true;
    } else {
        const size_t actual_size = zlink_msg_size (&parts_[0]);
        if (actual_size != state->payload_size) {
            fatal = true;
        } else {
            header_ok = perf_single_metric::decode_payload_header (
              zlink_msg_data (&parts_[0]), actual_size, &header);
        }
    }

    close_parts (parts_, part_count_);

    if (fatal) {
        single_mark_callback_fatal (state);
        return;
    }

    single_note_callback_receive (state);
    if (header_ok && header.run_id == state->run_id
        && header.msg_size == state->msg_size)
        (void) single_enqueue_metric_event (state, header);
}

inline bool setup_router_router_session (void *router1,
                                         void *router2,
                                         const std::string &transport,
                                         const std::string &pair_id)
{
    if (!router1 || !router2)
        return false;

    zlink_set_routing_id (router1, "ROUTER1", 7);
    zlink_set_routing_id (router2, "ROUTER2", 7);
    zlink_set_router_option (router2, ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
                             "ROUTER1", 7);

    if (!setup_tls_server (router1, transport)
        || !setup_tls_client (router2, transport)) {
        return false;
    }

    apply_single_hwm (router1);
    apply_single_hwm (router2);

    const std::string endpoint =
      bind_and_resolve_endpoint (router1, transport, pair_id);
    if (endpoint.empty ())
        return false;

    connect_monitor_t bind_monitor;
    connect_monitor_t connect_monitor;
    if (!open_connect_monitor (router1, bind_monitor))
        return false;
    if (!open_connect_monitor (router2, connect_monitor)) {
        close_connect_monitor (bind_monitor);
        return false;
    }
    if (!connect_checked (router2, endpoint))
    {
        close_connect_monitor (connect_monitor);
        close_connect_monitor (bind_monitor);
        return false;
    }

    apply_single_benchmark_socket_options (router1, transport);
    apply_single_benchmark_socket_options (router2, transport);

    const int timeout_ms = parse_positive_env ("PERF_CONNECT_READY_TIMEOUT_MS",
                                               3000);
    const bool bind_ready = wait_connect_ready_count (bind_monitor, 1, timeout_ms);
    const bool connect_ready =
      wait_connect_ready_count (connect_monitor, 1, timeout_ms);
    close_connect_monitor (connect_monitor);
    close_connect_monitor (bind_monitor);
    return bind_ready && connect_ready;
}

inline int send_router_parts_blocking (void *socket, zlink_msg_t *parts)
{
    if (!socket || !parts)
        return -1;

    while (true) {
        if (::zlink_send (socket, parts, 2, 0) >= 0)
            return 1;

        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        return err == EAGAIN ? 0 : -1;
    }
}

inline bool run_oneway_phase (void *sender,
                              std::vector<char> *payload,
                              router_router_callback_state_t *state,
                              uint64_t *seq,
                              perf_single_metric::phase_t phase,
                              int duration_s,
                              int recv_timeout_ms,
                              queue_probe_t *queue_probe,
                              unsigned long long *out_received,
                              latency_stats_t *out_latency)
{
    if (!sender || !payload || !state || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now ()
        + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    state->fatal.store (false, std::memory_order_release);
    state->warmup_received.store (0, std::memory_order_release);
    state->active_received.store (0, std::memory_order_release);
    state->recv_activity.store (0, std::memory_order_release);
    state->callback_enqueued.store (0, std::memory_order_release);
    state->callback_processed.store (0, std::memory_order_release);
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
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        bool send_ok = false;
        if (perf_single_metric::stamp_payload (payload->data (),
                                               state->payload_size,
                                               state->run_id,
                                               phase,
                                               state->msg_size,
                                               (*seq)++,
                                               sent_ts)) {
            zlink_msg_t parts[2];
            if (zlink_msg_init_size (&parts[0], 7) == 0) {
                if (zlink_msg_init_size (&parts[1], state->payload_size) == 0) {
                    std::memcpy (zlink_msg_data (&parts[0]), "ROUTER1", 7);
                    if (state->payload_size > 0) {
                        std::memcpy (
                          zlink_msg_data (&parts[1]), payload->data (),
                          state->payload_size);
                    }
                    const int send_rc =
                      send_router_parts_blocking (sender, parts);
                    send_ok = send_rc > 0;
                    if (send_rc < 0) {
                        zlink_msg_close (&parts[0]);
                        zlink_msg_close (&parts[1]);
                    } else if (send_rc == 0) {
                        zlink_msg_close (&parts[0]);
                        zlink_msg_close (&parts[1]);
                        break;
                    }
                } else {
                    zlink_msg_close (&parts[0]);
                }
            }
        }
        if (!send_ok) {
            debug_router_router ("active/warmup send failed");
            send_failed = true;
            break;
        }
        if (active_phase && queue_probe)
            queue_probe->sample_send_if_due ();
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    const int idle_timeout_ms = std::max (10, recv_timeout_ms);
    const int total_timeout_ms = std::max (100, idle_timeout_ms * 2);
    const bool quiet = wait_for_receive_quiet (
      *state, idle_timeout_ms, total_timeout_ms);
    const bool worker_idle =
      quiet && single_wait_for_metric_worker_idle (*state, total_timeout_ms);

    if (send_failed || !quiet || !worker_idle
        || state->fatal.load (std::memory_order_acquire)) {
        debug_router_router ("phase failed before metrics were collected");
        return false;
    }

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

inline bool prime_router_route (void *sender,
                                std::vector<char> *payload,
                                router_router_callback_state_t *state,
                                int recv_timeout_ms)
{
    if (!sender || !payload || !state)
        return false;

    state->fatal.store (false, std::memory_order_release);
    state->warmup_received.store (0, std::memory_order_release);
    state->active_received.store (0, std::memory_order_release);
    state->recv_activity.store (0, std::memory_order_release);
    state->callback_enqueued.store (0, std::memory_order_release);
    state->callback_processed.store (0, std::memory_order_release);
    state->active_deadline_us.store (0, std::memory_order_release);
    state->probe = NULL;
    {
        std::lock_guard<std::mutex> lock (state->latency_mutex);
        state->latency = latency_stats_builder_t ();
    }

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (
        std::max (2000, std::max (1, recv_timeout_ms) * 4));
    uint64_t seq = 1;

    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        if (!perf_single_metric::stamp_payload (payload->data (),
                                                state->payload_size,
                                                state->run_id,
                                                perf_single_metric::phase_warmup,
                                                state->msg_size,
                                                seq++,
                                                sent_ts)) {
            return false;
        }

        zlink_msg_t parts[2];
        if (zlink_msg_init_size (&parts[0], 7) != 0)
            return false;
        if (zlink_msg_init_size (&parts[1], state->payload_size) != 0) {
            zlink_msg_close (&parts[0]);
            return false;
        }

        std::memcpy (zlink_msg_data (&parts[0]), "ROUTER1", 7);
        if (state->payload_size > 0) {
            std::memcpy (
              zlink_msg_data (&parts[1]), payload->data (), state->payload_size);
        }

        const int send_rc = send_router_parts_blocking (sender, parts);
        if (send_rc < 0) {
            debug_router_router ("route prime send failed");
            zlink_msg_close (&parts[0]);
            zlink_msg_close (&parts[1]);
            return false;
        }
        if (send_rc == 0) {
            debug_router_router ("route prime send would block");
            zlink_msg_close (&parts[0]);
            zlink_msg_close (&parts[1]);
            (void) perf_socket_poll (NULL, 0, 1);
            continue;
        }

        {
            std::unique_lock<std::mutex> lock (state->mutex);
            (void) state->cv.wait_for (
              lock,
              std::chrono::milliseconds (
                std::max (50, std::min (250, recv_timeout_ms))),
              [state]() {
                  return state->recv_activity.load (
                           std::memory_order_acquire)
                           > 0
                         || state->fatal.load (std::memory_order_acquire);
              });
        }

        if (state->fatal.load (std::memory_order_acquire))
            return false;
        if (state->recv_activity.load (std::memory_order_acquire) > 0) {
            if (!single_wait_for_metric_worker_idle (
                  *state, std::max (100, recv_timeout_ms * 2))) {
                debug_router_router ("route prime worker idle wait failed");
                return false;
            }
            return state->warmup_received.load (std::memory_order_acquire) > 0;
        }
        debug_router_router ("route prime no receive activity yet");
    }

    return false;
}

} // namespace

void run_router_router (const std::string &transport,
                        size_t msg_size,
                        const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail_result (lib_name, "ROUTER_ROUTER", transport, msg_size);
        return;
    }

    socket_guard_t router1 (ctx.get (), ZLINK_SOCKET_ROUTER);
    socket_guard_t router2 (ctx.get (), ZLINK_SOCKET_ROUTER);
    queue_probe_t queue_probe (router2.get (), router1.get ());
    auto print_fail_with_queue = [&] () {
        print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size, &queue_probe);
    };
    if (!router1.valid () || !router2.valid ()) {
        print_fail_with_queue ();
        return;
    }

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');
    router_router_callback_state_t callback_state;
    single_callback_metric_queue_t callback_queue (65536);
    single_metric_worker_t<router_router_callback_state_t> metric_worker;

    callback_state.run_id =
      static_cast<uint32_t> (perf_single_metric::now_us ());
    callback_state.msg_size = msg_size;
    callback_state.payload_size = payload_size;
    callback_state.expected_source_rid = "ROUTER2";
    callback_state.expected_source_rid_size = 7;
    callback_state.callback_queue = &callback_queue;
    metric_worker.state = &callback_state;
    metric_worker.queue = &callback_queue;
    if (zlink_recv_handler (
          router1.get (), &router_router_recv_handler, &callback_state)
        != 0) {
        print_fail_with_queue ();
        return;
    }
    if (!start_single_metric_worker (&metric_worker)) {
        print_fail_with_queue ();
        return;
    }

    if (!setup_router_router_session (
          router1.get (), router2.get (), transport,
          lib_name + "_router_router")) {
        stop_single_metric_worker (&metric_worker);
        debug_router_router ("session setup failed");
        print_fail_with_queue ();
        return;
    }

    if (!prime_router_route (
          router2.get (), &payload, &callback_state, recv_timeout_ms)) {
        stop_single_metric_worker (&metric_worker);
        debug_router_router ("route prime failed");
        print_fail_with_queue ();
        return;
    }

    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_s = resolve_single_warmup_seconds ();
    if (!run_oneway_phase (router2.get (),
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
        debug_router_router ("warmup phase failed");
        print_fail_with_queue ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_oneway_phase (router2.get (),
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
        debug_router_router ("active phase failed");
        print_fail_with_queue ();
        return;
    }
    stop_single_metric_worker (&metric_worker);

    const double throughput =
      static_cast<double> (received) / static_cast<double> (duration_s);
    const queue_stats_t queue_stats = queue_probe.snapshot ();

    print_result (lib_name,
                  "ROUTER_ROUTER",
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
    return run_standard_bench_main (
      argc, argv, "ROUTER_ROUTER", run_router_router);
}
