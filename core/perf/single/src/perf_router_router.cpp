#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace {

inline void debug_router_router (const char *message_)
{
    if (bench_debug_enabled ())
        std::cerr << "[perf-router-router] " << message_ << std::endl;
}

// State for the receiver socket (router1) which handles both handshake
// and data-phase messages.
struct recv_state_t {
    recv_state_t () :
        handshake_mode (true),
        handshake_received (false),
        expected_payload_size (0),
        current_phase (0),
        active_phase (false),
        deadline (),
        queue_probe (NULL),
        received (0),
        latency_builder (),
        sender_done (false),
        recv_failed (false),
        total_dispatched (0)
    {}

    // Handshake coordination
    std::atomic<bool> handshake_mode;
    bool handshake_received;

    // Phase configuration (set before phase, read-only during phase)
    size_t expected_payload_size;
    uint32_t current_phase;
    bool active_phase;
    std::chrono::steady_clock::time_point deadline;
    queue_probe_t *queue_probe;

    // Recv stats (written by handler on I/O thread, read after completion)
    unsigned long long received;
    latency_stats_builder_t latency_builder;

    // Drain coordination
    std::atomic<bool> sender_done;
    std::atomic<bool> recv_failed;

    // Completion signaling (shared between handshake and data phases)
    std::mutex done_mutex;
    std::condition_variable done_cv;
    std::atomic<unsigned long long> total_dispatched;

private:
    recv_state_t (const recv_state_t &);
    recv_state_t &operator= (const recv_state_t &);
};

// Handler for router1 (receiver): processes handshake and data messages.
inline void router1_recv_handler (const zlink_routing_id_t *,
                                   zlink_msg_t *parts_,
                                   size_t part_count_,
                                   void *userdata_)
{
    recv_state_t *state = static_cast<recv_state_t *> (userdata_);

    // Data mode: expect exactly one payload part (routing id is separate)
    if (part_count_ != 1) {
        for (size_t i = 0; i < part_count_; ++i)
            zlink_msg_close (&parts_[i]);
        state->recv_failed.store (true, std::memory_order_release);
        if (state->sender_done.load (std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock (state->done_mutex);
            state->done_cv.notify_one ();
        }
        return;
    }

    const size_t actual_size = zlink_msg_size (&parts_[0]);
    const bool size_ok = actual_size == state->expected_payload_size;

    perf_single_metric::header_t header;
    bool header_ok = false;
    if (size_ok) {
        header_ok = perf_single_metric::decode_payload_header (
          zlink_msg_data (&parts_[0]), actual_size, &header);
    }

    zlink_msg_close (&parts_[0]);

    if (!size_ok) {
        debug_router_router ("router1 data payload size mismatch");
        state->recv_failed.store (true, std::memory_order_release);
        if (state->sender_done.load (std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock (state->done_mutex);
            state->done_cv.notify_one ();
        }
        return;
    }

    // Account header
    if (state->active_phase && state->queue_probe)
        state->queue_probe->sample_recv_if_due ();

    if (header_ok && header.magic == perf_single_metric::k_magic
        && header.phase == state->current_phase) {
        if (state->active_phase) {
            if (std::chrono::steady_clock::now () < state->deadline) {
                ++state->received;
                const uint64_t now = perf_single_metric::now_us ();
                const double latency_us =
                  now >= header.sent_ts_us
                    ? static_cast<double> (now - header.sent_ts_us)
                    : 0.0;
                state->latency_builder.add (latency_us);
            }
        } else {
            ++state->received;
        }
    }

    state->total_dispatched.fetch_add (1, std::memory_order_release);
    if (state->sender_done.load (std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock (state->done_mutex);
        state->done_cv.notify_one ();
    }
}

inline bool perform_router_router_handshake (void *router1, void *router2)
{
    const int handshake_timeout_ms =
      resolve_bench_count ("PERF_ROUTER_HANDSHAKE_TIMEOUT_MS", 3000);
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (
        handshake_timeout_ms > 0 ? handshake_timeout_ms : 3000);

    bool connected = false;
    while (!connected && std::chrono::steady_clock::now () < deadline) {
        zlink_msg_t ping_parts[2];
        if (zlink_msg_init_size (&ping_parts[0], 7) != 0)
            return false;
        if (zlink_msg_init_size (&ping_parts[1], 4) != 0) {
            zlink_msg_close (&ping_parts[0]);
            return false;
        }
        std::memcpy (zlink_msg_data (&ping_parts[0]), "ROUTER1", 7);
        std::memcpy (zlink_msg_data (&ping_parts[1]), "PING", 4);
        if (::zlink_send (router2, ping_parts, 2, ZLINK_DONTWAIT) < 0) {
            zlink_msg_close (&ping_parts[0]);
            zlink_msg_close (&ping_parts[1]);
            const int err = zlink_errno ();
            if (err != EAGAIN && err != EINTR && err != EHOSTUNREACH
                && err != ENOTCONN)
                return false;
        } else {
            zlink_routing_id_t source_rid;
            source_rid.size = 0;
            zlink_msg_t *parts = NULL;
            size_t part_count = 0;
            const int rc = ::zlink_recv (
              router1, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
            if (rc > 0) {
                connected = source_rid.size == 7
                            && std::memcmp (source_rid.data, "ROUTER2", 7) == 0
                            && part_count == 1
                            && zlink_msg_size (&parts[0]) == 4
                            && std::memcmp (
                                 zlink_msg_data (&parts[0]), "PING", 4) == 0;
                if (parts) {
                    zlink_multipart_close (parts, part_count);
                    free (parts);
                }
            } else if (rc < 0) {
                const int err = zlink_errno ();
                if (err != EAGAIN && err != EINTR)
                    return false;
            }
        }

        if (!connected)
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    if (!connected)
        return false;

    const int timeout_ms = resolve_single_recv_timeout_ms ();
    set_sockopt_int (
      router1, ZLINK_OPT_RCVTIMEO, timeout_ms, "ZLINK_OPT_RCVTIMEO");
    set_sockopt_int (
      router2, ZLINK_OPT_RCVTIMEO, timeout_ms, "ZLINK_OPT_RCVTIMEO");
    return true;
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
    const int mandatory = 1;
    zlink_set_router_option (router1, ZLINK_ROUTER_OPT_MANDATORY, &mandatory,
                             sizeof (mandatory));
    zlink_set_router_option (router2, ZLINK_ROUTER_OPT_MANDATORY, &mandatory,
                             sizeof (mandatory));
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
    if (!connect_checked (router2, endpoint))
        return false;

    apply_single_benchmark_socket_options (router1, transport);
    apply_single_benchmark_socket_options (router2, transport);

    return true;
}

inline bool run_oneway_phase (recv_state_t *state,
                              void *sender,
                              std::vector<char> *payload,
                              size_t payload_size,
                              size_t msg_size,
                              uint32_t run_id,
                              uint64_t *seq,
                              perf_single_metric::phase_t phase,
                              int duration_s,
                              int recv_timeout_ms,
                              queue_probe_t *queue_probe,
                              unsigned long long *out_received,
                              latency_stats_t *out_latency)
{
    if (!state || !sender || !payload || !seq || !out_received)
        return false;

    const bool active_phase = phase == perf_single_metric::phase_active;
    const auto deadline =
      std::chrono::steady_clock::now ()
        + std::chrono::seconds (duration_s > 0 ? duration_s : 1);
    const auto drain_idle_limit = std::chrono::milliseconds (
      recv_timeout_ms > 0 ? recv_timeout_ms : 200);

    // Configure recv state for this phase
    state->expected_payload_size = payload_size;
    state->current_phase = static_cast<uint32_t> (phase);
    state->active_phase = active_phase;
    state->deadline = deadline;
    state->queue_probe = queue_probe;
    state->received = 0;
    state->latency_builder = latency_stats_builder_t ();
    state->sender_done.store (false, std::memory_order_release);
    state->recv_failed.store (false, std::memory_order_relaxed);
    state->total_dispatched.store (0, std::memory_order_relaxed);

    if (active_phase && queue_probe)
        queue_probe->force_sample_recv ();

    // Send phase
    bool send_failed = false;
    if (active_phase && queue_probe)
        queue_probe->force_sample_send ();

    while (std::chrono::steady_clock::now () < deadline) {
        const uint64_t sent_ts = perf_single_metric::now_us ();
        bool send_ok = false;
        if (perf_single_metric::stamp_payload (payload->data (),
                                               payload_size,
                                               run_id,
                                               phase,
                                               msg_size,
                                               (*seq)++,
                                               sent_ts)) {
            zlink_msg_t parts[2];
            if (zlink_msg_init_size (&parts[0], 7) == 0) {
                if (zlink_msg_init_size (&parts[1], payload_size) == 0) {
                    std::memcpy (zlink_msg_data (&parts[0]), "ROUTER1", 7);
                    if (payload_size > 0)
                        std::memcpy (
                          zlink_msg_data (&parts[1]), payload->data (),
                          payload_size);
                    send_ok = ::zlink_send (sender, parts, 2, 0) >= 0;
                    if (!send_ok) {
                        zlink_msg_close (&parts[0]);
                        zlink_msg_close (&parts[1]);
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

    // Signal sender done and wait for drain
    state->sender_done.store (true, std::memory_order_release);

    {
        std::unique_lock<std::mutex> lock (state->done_mutex);
        auto drain_deadline =
          std::chrono::steady_clock::now () + drain_idle_limit;
        unsigned long long prev_dispatched =
          state->total_dispatched.load (std::memory_order_acquire);

        while (!state->recv_failed.load (std::memory_order_acquire)) {
            const auto status =
              state->done_cv.wait_until (lock, drain_deadline);
            const unsigned long long curr_dispatched =
              state->total_dispatched.load (std::memory_order_acquire);

            if (state->recv_failed.load (std::memory_order_acquire))
                break;

            if (curr_dispatched != prev_dispatched) {
                prev_dispatched = curr_dispatched;
                drain_deadline =
                  std::chrono::steady_clock::now () + drain_idle_limit;
                continue;
            }

            if (status == std::cv_status::timeout
                || std::chrono::steady_clock::now () >= drain_deadline) {
                break;
            }
        }
    }

    if (active_phase && queue_probe)
        queue_probe->force_sample_recv ();

    if (send_failed || state->recv_failed.load (std::memory_order_acquire))
    {
        debug_router_router ("phase failed before metrics were collected");
        return false;
    }

    *out_received = state->received;

    if (active_phase) {
        if (state->received == 0 || state->latency_builder.count () == 0
            || !out_latency)
            return false;
        *out_latency = state->latency_builder.snapshot ();
    } else if (state->received == 0) {
        return false;
    }

    return true;
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

    recv_state_t recv_state;

    socket_guard_t router1_plain (ctx.get (), ZLINK_ROUTER);
    socket_guard_t router1_with_handler (
      ctx.get (), ZLINK_ROUTER, &router1_recv_handler, &recv_state);
    socket_guard_t router2 (ctx.get (), ZLINK_ROUTER);
    void *router1_socket =
      transport == "inproc" ? router1_with_handler.get () : router1_plain.get ();
    queue_probe_t queue_probe (router2.get (), router1_socket);
    auto print_fail_with_queue = [&] () {
        print_fail_result (
          lib_name, "ROUTER_ROUTER", transport, msg_size, &queue_probe);
    };
    if (!router1_socket || !router2.valid ()) {
        print_fail_with_queue ();
        return;
    }

    if (!setup_router_router_session (
          router1_socket, router2.get (), transport,
          lib_name + "_router_router")) {
        debug_router_router ("session setup failed");
        print_fail_with_queue ();
        return;
    }

    if (transport != "inproc"
        && zlink_recv_handler (
          router1_socket, &router1_recv_handler, &recv_state)
        != 0) {
        debug_router_router ("failed to install router1 recv handler");
        print_fail_with_queue ();
        return;
    }
    recv_state.handshake_mode.store (false, std::memory_order_release);

    const int recv_timeout_ms = resolve_single_recv_timeout_ms ();
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_single_metric::header_size ());
    std::vector<char> payload (payload_size, 'a');

    const uint32_t run_id = static_cast<uint32_t> (perf_single_metric::now_us ());
    uint64_t seq = 1;

    unsigned long long warmup_received = 0;
    const int warmup_s = resolve_single_warmup_seconds ();
    if (!run_oneway_phase (&recv_state,
                           router2.get (),
                           &payload,
                           payload_size,
                           msg_size,
                           run_id,
                           &seq,
                           perf_single_metric::phase_warmup,
                           warmup_s,
                           recv_timeout_ms,
                           NULL,
                           &warmup_received,
                           NULL)) {
        debug_router_router ("warmup phase failed");
        print_fail_with_queue ();
        return;
    }

    const int duration_s = std::max (1, resolve_single_duration_seconds ());
    unsigned long long received = 0;
    latency_stats_t latency_stats;
    if (!run_oneway_phase (&recv_state,
                           router2.get (),
                           &payload,
                           payload_size,
                           msg_size,
                           run_id,
                           &seq,
                           perf_single_metric::phase_active,
                           duration_s,
                           recv_timeout_ms,
                           &queue_probe,
                           &received,
                           &latency_stats)) {
        debug_router_router ("active phase failed");
        print_fail_with_queue ();
        return;
    }

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
    return run_standard_bench_main (argc, argv, run_router_router);
}
