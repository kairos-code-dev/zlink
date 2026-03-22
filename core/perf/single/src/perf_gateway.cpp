#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <sstream>
#include <thread>
#include <vector>

namespace
{
static const char *k_pattern = "GATEWAY";
static std::atomic<int> g_gateway_debug_send_logs (0);

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

std::string make_gateway_run_token ()
{
    std::ostringstream oss;
    oss << current_process_id () << "-"
        << static_cast<unsigned long long> (perf_single_metric::now_us ());
    return oss.str ();
}

struct gateway_server_state_t
{
    gateway_server_state_t () :
        run_id (0),
        msg_size (0),
        active_deadline_us (0),
        warmup_received (0),
        active_received (0),
        fatal (false),
        probe (NULL),
        callback_queue (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    std::atomic<uint64_t> active_deadline_us;
    std::atomic<unsigned long long> warmup_received;
    std::atomic<unsigned long long> active_received;
    std::atomic<bool> fatal;
    latency_stats_builder_t latency;
    queue_probe_t *probe;
    std::mutex mutex;
    std::mutex latency_mutex;
    std::condition_variable cv;
    single_callback_metric_queue_t *callback_queue;
};

struct gateway_recv_loop_t
{
    gateway_recv_loop_t () :
        gateway (NULL), state (NULL), stop (false), ready (false)
    {
    }

    void *gateway;
    gateway_server_state_t *state;
    std::atomic<bool> stop;
    std::atomic<bool> ready;
    std::thread thread;
};

bool wait_gateway_send_ready_event (void *gateway_, int timeout_ms_)
{
    void *monitor =
      open_configured_service_monitor (gateway_, ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED);
    if (!monitor)
        return false;
    const bool ready = wait_for_service_monitor_event (
      monitor, ZLINK_GATEWAY_MONITOR_EVENT_SEND_READY_CHANGED, 0, timeout_ms_);
    zlink_monitor_close (&monitor);
    return ready;
}

void cleanup_gateway_case (void **client_gateway_, void **server_gateway_)
{
    if (client_gateway_ && *client_gateway_)
        zlink_gateway_destroy (client_gateway_);
    if (server_gateway_ && *server_gateway_)
        zlink_gateway_destroy (server_gateway_);
}

std::string bind_server_gateway (void *gateway_,
                                 const std::string &transport_,
                                 int base_port_)
{
    return perf_bind_fixed_endpoint_range (
      gateway_, transport_, base_port_, 64, &perf_bind_gateway_endpoint, true);
}

bool send_payload (void *gateway_,
                   const zlink_routing_id_t *target_rid_,
                   std::vector<char> &payload_,
                   size_t msg_size_,
                   uint32_t run_id_,
                   perf_single_metric::phase_t phase_,
                   uint64_t seq_,
                   int flags_)
{
    while (true) {
        const size_t payload_size =
          std::max (msg_size_, perf_single_metric::header_size ());
        payload_.assign (payload_size, 'g');
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
        if (zlink_gateway_send_rid (gateway_, target_rid_, &part, 1, flags_) == 0)
            return true;

        const int err = errno;
        zlink_msg_close (&part);
        if (err == EINTR)
            continue;
        if (getenv ("PERF_DEBUG")) {
            const int slot =
              g_gateway_debug_send_logs.fetch_add (1, std::memory_order_acq_rel);
            if (slot < 16) {
                std::cerr << "[perf-gateway] send failed phase="
                          << static_cast<int> (phase_) << " seq=" << seq_
                          << " errno=" << err << std::endl;
            }
        }
        errno = err;
        return false;
    }
}

void process_gateway_parts (gateway_server_state_t *state_,
                            const zlink_routing_id_t *,
                            zlink_msg_t *parts_,
                            size_t part_count_)
{
    if (!state_ || part_count_ == 0)
        return;

    perf_single_metric::header_t header;
    const bool header_ok =
      perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &header)
      && header.run_id == state_->run_id
      && header.msg_size == state_->msg_size;

    single_note_callback_receive (state_);
    if (header_ok)
        (void) single_enqueue_metric_event (state_, header);
}

void gateway_recv_handler (const zlink_routing_id_t *source_rid_,
                           zlink_msg_t *parts_,
                           size_t part_count_,
                           void *userdata_)
{
    gateway_server_state_t *state =
      static_cast<gateway_server_state_t *> (userdata_);
    if (!state) {
        perf_close_multipart (parts_, part_count_);
        return;
    }

    process_gateway_parts (state, source_rid_, parts_, part_count_);
    perf_close_multipart (parts_, part_count_);
    state->cv.notify_all ();
}

void start_gateway_recv_loop (gateway_recv_loop_t *loop_)
{
    if (!loop_ || !loop_->gateway || !loop_->state)
        return;

    loop_->stop.store (false, std::memory_order_release);
    loop_->ready.store (false, std::memory_order_release);
    loop_->thread = std::thread ([loop_] () {
        void *poller = zlink_poller_new ();
        if (!poller
            || zlink_poller_add (poller, loop_->gateway, loop_->gateway,
                                 ZLINK_POLLIN)
                 != 0) {
            if (poller)
                (void) zlink_poller_destroy (&poller);
            loop_->state->fatal.store (true, std::memory_order_release);
            loop_->state->cv.notify_all ();
            return;
        }
        loop_->ready.store (true, std::memory_order_release);

        while (!loop_->stop.load (std::memory_order_acquire)) {
            zlink_poller_event_t event;
            std::memset (&event, 0, sizeof (event));
            const int poll_rc = zlink_poller_wait (poller, &event, 10);
            if (poll_rc < 0) {
                const int poll_err = zlink_errno ();
                if (poll_err == EINTR || poll_err == EAGAIN)
                    continue;
                loop_->state->fatal.store (true, std::memory_order_release);
                loop_->state->cv.notify_all ();
                break;
            }
            if (poll_rc == 0)
                continue;

            zlink_routing_id_t source_rid;
            std::memset (&source_rid, 0, sizeof (source_rid));
            zlink_msg_t *parts = NULL;
            size_t part_count = 0;
            const int rc =
              zlink_recv (loop_->gateway, &source_rid, &parts, &part_count, 0);
            if (rc == 0) {
                process_gateway_parts (
                  loop_->state, &source_rid, parts, part_count);
                perf_close_multipart (parts, part_count);
                free (parts);
                loop_->state->cv.notify_all ();
                continue;
            }

            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR) {
                continue;
            }

            loop_->state->fatal.store (true, std::memory_order_release);
            loop_->state->cv.notify_all ();
            break;
        }
        (void) zlink_poller_destroy (&poller);
    });
}

void stop_gateway_recv_loop (gateway_recv_loop_t *loop_)
{
    if (!loop_)
        return;
    loop_->stop.store (true, std::memory_order_release);
    if (loop_->thread.joinable ())
        loop_->thread.join ();
}

bool wait_gateway_recv_loop_ready (gateway_recv_loop_t *loop_, int timeout_ms_)
{
    if (!loop_)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    while (std::chrono::steady_clock::now () < deadline) {
        if (loop_->ready.load (std::memory_order_acquire))
            return true;
        if (perf_socket_poll (NULL, 0, 1) < 0 && zlink_errno () != EINTR)
            return false;
    }
    return loop_->ready.load (std::memory_order_acquire);
}

bool run_gateway_phase_window (void *gateway_,
                               const zlink_routing_id_t *target_rid_,
                               gateway_server_state_t &server_state_,
                               queue_probe_t &probe_,
                               std::vector<char> &payload_,
                               size_t msg_size_,
                               perf_single_metric::phase_t phase_,
                               int duration_s_,
                               double *throughput_out_,
                               latency_stats_t *latency_out_)
{
    const bool active_phase = phase_ == perf_single_metric::phase_active;
    {
        server_state_.warmup_received.store (0, std::memory_order_release);
        server_state_.active_received.store (0, std::memory_order_release);
        server_state_.active_deadline_us.store (
          active_phase
            ? perf_single_metric::now_us ()
                + static_cast<uint64_t> (
                    std::max (1, duration_s_) * 1000000ULL)
            : 0,
          std::memory_order_release);
        server_state_.fatal.store (false, std::memory_order_release);
    }
    {
        std::lock_guard<std::mutex> latency_lock (server_state_.latency_mutex);
        server_state_.latency = latency_stats_builder_t ();
    }

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (
        active_phase ? std::max (1, duration_s_) : std::max (0, duration_s_));
    const int send_flags = 0;
    uint64_t seq = 1;
    unsigned long long sent_count = 0;

    while (std::chrono::steady_clock::now () < deadline) {
        probe_.sample_send_if_due ();
        if (send_payload (gateway_,
                          target_rid_,
                          payload_,
                          msg_size_,
                          server_state_.run_id,
                          phase_,
                          seq,
                          send_flags)) {
            ++sent_count;
            ++seq;
            continue;
        }

        return false;
    }

    if (!single_wait_for_phase_processed (
          server_state_,
          phase_,
          sent_count,
          single_phase_drain_timeout_ms (
            duration_s_, resolve_single_recv_timeout_ms ()))) {
        server_state_.active_deadline_us.store (0, std::memory_order_release);
        return false;
    }

    if (phase_ != perf_single_metric::phase_active) {
        if (getenv ("PERF_DEBUG")) {
            std::cerr << "[perf-gateway] warmup received="
                      << server_state_.warmup_received.load (
                           std::memory_order_acquire)
                      << std::endl;
        }
        return !server_state_.fatal.load (std::memory_order_acquire)
               && server_state_.warmup_received.load (
                    std::memory_order_acquire)
                    > 0;
    }

    const unsigned long long active_received =
      server_state_.active_received.load (std::memory_order_acquire);
    if (latency_out_) {
        std::lock_guard<std::mutex> lock (server_state_.latency_mutex);
        *latency_out_ = server_state_.latency.snapshot ();
    }
    server_state_.active_deadline_us.store (0, std::memory_order_release);
    if (getenv ("PERF_DEBUG")) {
        std::cerr << "[perf-gateway] active received=" << active_received
                  << " duration_s=" << duration_s_ << std::endl;
    }

    if (throughput_out_)
        *throughput_out_ =
          static_cast<double> (active_received)
          / static_cast<double> (std::max (1, duration_s_));
    return !server_state_.fatal.load (std::memory_order_acquire)
           && active_received > 0;
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
    if (!ctx.valid ()) {
        print_fail_result (lib_name_, k_pattern, transport_, msg_size_);
        return 1;
    }

    gateway_server_state_t server_state;
    queue_probe_t *probe = NULL;
    single_callback_metric_queue_t callback_queue (65536);
    single_metric_worker_t<gateway_server_state_t> metric_worker;
    auto print_fail = [&] () {
        print_fail_result (lib_name_, k_pattern, transport_, msg_size_, probe);
    };
    const std::string run_token = make_gateway_run_token ();
    const std::string service_name = "perf-gateway-" + run_token;
    const std::string server_routing_id = "perf-gateway-server-" + run_token;
    const std::string client_routing_id = "perf-gateway-client-" + run_token;

    void *server_gateway = zlink_gateway_new (ctx.get (), service_name.c_str ());
    void *client_gateway = zlink_gateway_new (ctx.get (), service_name.c_str ());
    if (!server_gateway || !client_gateway) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }
    if (zlink_set_routing_id (server_gateway, server_routing_id.c_str (),
                                      server_routing_id.size ())
          != 0
        || zlink_set_routing_id (client_gateway, client_routing_id.c_str (),
                                         client_routing_id.size ())
             != 0) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }

    queue_probe_t *client_probe = new (std::nothrow)
      queue_probe_t (client_gateway, server_gateway);
    if (!client_probe) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }
    probe = client_probe;

    const int linger = 0;
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    (void) zlink_set_option (
      server_gateway, ZLINK_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_set_option (
      server_gateway, ZLINK_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_set_option (
      server_gateway, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));
    (void) zlink_set_option (
      client_gateway, ZLINK_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_set_option (
      client_gateway, ZLINK_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_set_option (
      client_gateway, ZLINK_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));

    if (!setup_tls_server (server_gateway, transport_)
        || !setup_tls_client (client_gateway, transport_)) {
        print_fail ();
        delete client_probe;
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }

    const int base_port =
      33000 + (current_process_id () % 1000) * 8;
    const std::string endpoint =
      bind_server_gateway (server_gateway, transport_, base_port);
    if (endpoint.empty ()) {
        print_fail ();
        delete client_probe;
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }

    zlink_routing_id_t server_rid;
    std::memset (&server_rid, 0, sizeof (server_rid));
    if (zlink_get_routing_id (server_gateway, &server_rid) != 0) {
        print_fail ();
        delete client_probe;
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }

    if (zlink_gateway_connect (client_gateway, endpoint.c_str (), &server_rid)
        != 0
        || !wait_gateway_send_ready_event (client_gateway, 5000)) {
        print_fail ();
        delete client_probe;
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }

    server_state.run_id = static_cast<uint32_t> (current_process_id ());
    server_state.msg_size = msg_size_;
    server_state.probe = probe;
    server_state.callback_queue = &callback_queue;
    metric_worker.state = &server_state;
    metric_worker.queue = &callback_queue;
    if (zlink_recv_handler (server_gateway, &gateway_recv_handler,
                            &server_state)
        != 0) {
        print_fail ();
        delete client_probe;
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }
    if (!start_single_metric_worker (&metric_worker)) {
        print_fail ();
        delete client_probe;
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }

    std::vector<char> payload;
    if (!run_gateway_phase_window (
          client_gateway, &server_rid, server_state, *probe,
          payload, msg_size_, perf_single_metric::phase_warmup,
          resolve_single_warmup_seconds (), NULL, NULL)) {
        const queue_stats_t queue_stats = probe->snapshot ();
        server_state.probe = NULL;
        stop_single_metric_worker (&metric_worker);
        delete client_probe;
        probe = NULL;
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        cleanup_gateway_case (&client_gateway, &server_gateway);
        return 1;
    }
    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok = run_gateway_phase_window (
      client_gateway, &server_rid, server_state, *probe,
      payload, msg_size_, perf_single_metric::phase_active,
      resolve_single_duration_seconds (), &throughput, &latency);
    stop_single_metric_worker (&metric_worker);
    const queue_stats_t queue_stats = probe->snapshot ();
    server_state.probe = NULL;
    delete client_probe;
    probe = NULL;
    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  active_ok ? throughput : 0.0,
                  active_ok ? latency.mean_us : 0.0,
                  active_ok ? latency.p95_us : 0.0,
                  active_ok ? latency.p99_us : 0.0,
                  queue_stats);

    cleanup_gateway_case (&client_gateway, &server_gateway);
    return active_ok ? 0 : 1;
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
