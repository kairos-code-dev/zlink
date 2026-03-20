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
        probe (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    uint64_t active_deadline_us;
    unsigned long long warmup_received;
    unsigned long long active_received;
    bool fatal;
    latency_stats_builder_t latency;
    queue_probe_t *probe;
    std::mutex mutex;
    std::condition_variable cv;
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

struct gateway_ready_monitor_t
{
    gateway_ready_monitor_t () :
        gateway (NULL),
        monitor (NULL),
        send_ready (false),
        error_code (0)
    {
    }

    void *gateway;
    void *monitor;
    std::mutex mutex;
    std::condition_variable cv;
    bool send_ready;
    int error_code;
};

void gateway_ready_monitor_handler (const zlink_service_event_t *event_,
                                    void *userdata_)
{
    gateway_ready_monitor_t *monitor =
      static_cast<gateway_ready_monitor_t *> (userdata_);
    if (!monitor || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (monitor->mutex);
        switch (event_->event_type) {
            case ZLINK_GATEWAY_SEND_READY_CHANGED:
                monitor->send_ready = event_->value > 0;
                break;

            case ZLINK_GATEWAY_MONITOR_EVENT_ERROR:
                if (monitor->error_code == 0) {
                    monitor->error_code =
                      event_->error_code != 0 ? event_->error_code : EIO;
                }
                break;

            default:
                break;
        }
    }
    monitor->cv.notify_all ();
}

bool open_gateway_ready_monitor (void *gateway_,
                                 gateway_ready_monitor_t *out_)
{
    if (!gateway_ || !out_)
        return false;

    zlink_service_monitor_open_options_t opts;
    memset (&opts, 0, sizeof (opts));
    opts.events = ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
                  | ZLINK_GATEWAY_ROUTE_DOWN
                  | ZLINK_GATEWAY_MONITOR_EVENT_ERROR;
    out_->gateway = gateway_;
    out_->monitor = zlink_service_monitor_open (gateway_, &opts);
    return out_->monitor != NULL
           && zlink_service_monitor_handler (
                out_->monitor, &gateway_ready_monitor_handler, out_)
                == 0;
}

bool wait_gateway_ready (gateway_ready_monitor_t *monitor_,
                         size_t expected_,
                         int timeout_ms_)
{
    if (expected_ == 0)
        return true;
    if (!monitor_ || !monitor_->gateway || !monitor_->monitor)
        return false;

    std::unique_lock<std::mutex> lock (monitor_->mutex);
    if (monitor_->error_code != 0)
        return false;
    if (monitor_->send_ready)
        return true;

    const bool signaled = monitor_->cv.wait_for (
      lock,
      std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1),
      [monitor_] () {
          return monitor_->error_code != 0 || monitor_->send_ready;
      });
    return signaled && monitor_->error_code == 0 && monitor_->send_ready;
}

bool wait_gateway_send_ready_until (gateway_ready_monitor_t *monitor_,
                                    size_t expected_,
                                    const std::chrono::steady_clock::time_point
                                      &deadline_)
{
    if (expected_ == 0)
        return true;
    if (!monitor_ || !monitor_->gateway || !monitor_->monitor)
        return false;

    std::unique_lock<std::mutex> lock (monitor_->mutex);
    if (monitor_->error_code != 0)
        return false;
    if (monitor_->send_ready)
        return true;

    while (std::chrono::steady_clock::now () < deadline_) {
        if (monitor_->cv.wait_until (
              lock, deadline_,
              [monitor_] () {
                  return monitor_->error_code != 0 || monitor_->send_ready;
              })) {
            return monitor_->error_code == 0 && monitor_->send_ready;
        }
    }

    return monitor_->error_code == 0 && monitor_->send_ready;
}

bool wait_gateway_settle_phase (gateway_ready_monitor_t *monitor_, int settle_ms_)
{
    if (settle_ms_ <= 0)
        return true;
    if (!monitor_ || !monitor_->gateway || !monitor_->monitor)
        return false;

    std::unique_lock<std::mutex> lock (monitor_->mutex);
    if (monitor_->error_code != 0 || !monitor_->send_ready)
        return false;

    const std::chrono::steady_clock::time_point settle_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (settle_ms_);
    while (std::chrono::steady_clock::now () < settle_deadline) {
        if (monitor_->cv.wait_until (
              lock, settle_deadline,
              [monitor_] () { return monitor_->error_code != 0; })) {
            return false;
        }
    }

    return monitor_->error_code == 0 && monitor_->send_ready;
}

void close_gateway_ready_monitor (gateway_ready_monitor_t *monitor_)
{
    if (!monitor_)
        return;

    if (monitor_->monitor)
        (void) zlink_monitor_close (&monitor_->monitor);
    monitor_->gateway = NULL;
    monitor_->monitor = NULL;
}

void cleanup_gateway_case (void **client_gateway_,
                           void **server_gateway_,
                           gateway_ready_monitor_t *client_monitor_)
{
    close_gateway_ready_monitor (client_monitor_);
    if (client_gateway_ && *client_gateway_)
        zlink_gateway_destroy (client_gateway_);
    if (server_gateway_ && *server_gateway_)
        zlink_gateway_destroy (server_gateway_);
}

bool is_supported_transport (const std::string &transport_)
{
    return transport_ == "tcp" || transport_ == "tls" || transport_ == "ws"
           || transport_ == "wss";
}

void close_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close (&parts_[i]);
}

bool configure_tls_client (void *gateway_, const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    static const std::string ca_path =
      write_temp_cert (test_certs::ca_cert_pem, "perf_gateway_ca");
    return zlink_set_tls_client (
             gateway_, ca_path.c_str (), "localhost", 0)
           == 0;
}

bool configure_tls_server (void *gateway_, const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    static const std::string cert_path =
      write_temp_cert (test_certs::server_cert_pem, "perf_gateway_cert");
    static const std::string key_path =
      write_temp_cert (test_certs::server_key_pem, "perf_gateway_key");
    return zlink_set_tls_server (
             gateway_, cert_path.c_str (), key_path.c_str (), 0)
           == 0;
}

std::string bind_server_gateway (void *gateway_,
                                 const std::string &transport_,
                                 int base_port_)
{
    for (int i = 0; i < 64; ++i) {
        const std::string endpoint =
          make_fixed_endpoint (transport_, base_port_ + i);
        if (zlink_gateway_bind (gateway_, endpoint.c_str ()) == 0)
            return endpoint;
    }
    return std::string ();
}

bool wait_for_counter (std::condition_variable &cv_,
                       std::mutex &mutex_,
                       unsigned long long *value_,
                       unsigned long long expected_,
                       int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (mutex_);
    return cv_.wait_for (
      lock,
      std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1),
      [value_, expected_] () { return *value_ >= expected_; });
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
    if (zlink_gateway_send_rid (gateway_, target_rid_, &part, 1, flags_) != 0) {
        const int err = errno;
        zlink_msg_close (&part);
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

    return true;
}

void process_gateway_parts (gateway_server_state_t *state_,
                            const zlink_routing_id_t *source_rid_,
                            zlink_msg_t *parts_,
                            size_t part_count_)
{
    if (!state_ || !source_rid_ || part_count_ == 0)
        return;

    perf_single_metric::header_t header;
    const bool header_ok =
      perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &header)
      && header.run_id == state_->run_id
      && header.msg_size == state_->msg_size;

    if (header_ok) {
        std::lock_guard<std::mutex> lock (state_->mutex);
        if (header.phase
            == static_cast<uint32_t> (perf_single_metric::phase_warmup)) {
            ++state_->warmup_received;
        } else if (header.phase
                   == static_cast<uint32_t> (
                        perf_single_metric::phase_active)) {
            const uint64_t now_us = perf_single_metric::now_us ();
            if (state_->active_deadline_us > 0
                && now_us <= state_->active_deadline_us) {
                ++state_->active_received;
                state_->latency.add (
                  now_us >= header.sent_ts_us
                    ? static_cast<double> (now_us - header.sent_ts_us)
                    : 0.0);
            }
        }
        if (state_->probe)
            state_->probe->sample_recv_if_due ();
    }
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
            std::lock_guard<std::mutex> lock (loop_->state->mutex);
            loop_->state->fatal = true;
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
                std::lock_guard<std::mutex> lock (loop_->state->mutex);
                loop_->state->fatal = true;
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
                close_parts (parts, part_count);
                free (parts);
                loop_->state->cv.notify_all ();
                continue;
            }

            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR) {
                continue;
            }

            {
                std::lock_guard<std::mutex> lock (loop_->state->mutex);
                loop_->state->fatal = true;
            }
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

bool wait_gateway_receive_quiet (gateway_server_state_t &server_state_,
                                 bool active_phase_)
{
    std::unique_lock<std::mutex> lock (server_state_.mutex);
    const std::chrono::milliseconds idle_timeout (
      active_phase_ ? std::max (1000, resolve_single_recv_timeout_ms () * 10)
                    : std::max (10, resolve_single_recv_timeout_ms ()));
    while (true) {
        const unsigned long long observed =
          active_phase_ ? server_state_.active_received
                        : server_state_.warmup_received;
        const bool changed = server_state_.cv.wait_for (
          lock, idle_timeout, [&server_state_, observed, active_phase_] () {
              if (server_state_.fatal)
                  return true;
              return active_phase_ ? server_state_.active_received != observed
                                   : server_state_.warmup_received != observed;
          });
        if (server_state_.fatal)
            return false;
        if (!changed)
            return true;
    }
}

bool run_gateway_phase_window (void *gateway_,
                               const zlink_routing_id_t *target_rid_,
                               gateway_ready_monitor_t *monitor_,
                               gateway_server_state_t &server_state_,
                               queue_probe_t &probe_,
                               std::vector<char> &payload_,
                               size_t msg_size_,
                               perf_single_metric::phase_t phase_,
                               int duration_s_,
                               double *throughput_out_,
                               latency_stats_t *latency_out_)
{
    {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        server_state_.warmup_received = 0;
        server_state_.active_received = 0;
        server_state_.active_deadline_us =
          phase_ == perf_single_metric::phase_active
            ? perf_single_metric::now_us ()
                + static_cast<uint64_t> (
                    std::max (1, duration_s_) * 1000000ULL)
            : 0;
        server_state_.fatal = false;
        server_state_.latency = latency_stats_builder_t ();
    }

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (
        phase_ == perf_single_metric::phase_active
          ? std::max (1, duration_s_)
          : std::max (0, duration_s_));
    const int send_flags =
      phase_ == perf_single_metric::phase_active ? ZLINK_DONTWAIT : 0;
    uint64_t seq = 1;

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
            ++seq;
            continue;
        }

        if (phase_ != perf_single_metric::phase_active)
            return false;
        if (errno != EAGAIN && errno != EHOSTUNREACH && errno != ENOTCONN)
            return false;
        if (!wait_gateway_send_ready_until (monitor_, 1, deadline))
            break;
        if (std::chrono::steady_clock::now () >= deadline)
            break;
    }

    if (!wait_gateway_receive_quiet (
          server_state_, phase_ == perf_single_metric::phase_active)) {
        server_state_.active_deadline_us = 0;
        return false;
    }

    if (phase_ != perf_single_metric::phase_active) {
        if (getenv ("PERF_DEBUG")) {
            std::lock_guard<std::mutex> lock (server_state_.mutex);
            std::cerr << "[perf-gateway] warmup received="
                      << server_state_.warmup_received << std::endl;
        }
        return !server_state_.fatal && server_state_.warmup_received > 0;
    }

    unsigned long long active_received = 0;
    {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        active_received = server_state_.active_received;
        if (latency_out_)
            *latency_out_ = server_state_.latency.snapshot ();
    }
    server_state_.active_deadline_us = 0;
    if (getenv ("PERF_DEBUG")) {
        std::cerr << "[perf-gateway] active received=" << active_received
                  << " duration_s=" << duration_s_ << std::endl;
    }

    if (throughput_out_)
        *throughput_out_ =
          static_cast<double> (active_received)
          / static_cast<double> (std::max (1, duration_s_));
    return !server_state_.fatal && active_received > 0;
}

bool prime_gateway_route (void *gateway_,
                          const zlink_routing_id_t *target_rid_,
                          gateway_ready_monitor_t *monitor_,
                          gateway_server_state_t &server_state_,
                          queue_probe_t &probe_,
                          std::vector<char> &payload_,
                          size_t msg_size_)
{
    const unsigned long long preflight_messages = 32;

    {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        server_state_.warmup_received = 0;
        server_state_.active_received = 0;
        server_state_.active_deadline_us = 0;
        server_state_.fatal = false;
        server_state_.latency = latency_stats_builder_t ();
    }

    for (unsigned long long seq = 1; seq <= preflight_messages; ++seq) {
        probe_.sample_send_if_due ();
        while (!send_payload (gateway_,
                              target_rid_,
                              payload_,
                              msg_size_,
                              server_state_.run_id,
                              perf_single_metric::phase_warmup,
                              seq,
                              ZLINK_DONTWAIT)) {
            if (errno != EAGAIN && errno != EHOSTUNREACH && errno != ENOTCONN)
                return false;
            if (!wait_gateway_send_ready_until (
                  monitor_, 1,
                  std::chrono::steady_clock::now ()
                    + std::chrono::milliseconds (
                      std::max (1000, resolve_single_recv_timeout_ms () * 2)))) {
                return false;
            }
        }
    }

    const bool primed =
      wait_for_counter (server_state_.cv,
                        server_state_.mutex,
                        &server_state_.warmup_received,
                        preflight_messages,
                        std::max (2000, resolve_single_recv_timeout_ms () * 2));
    if (!primed && getenv ("PERF_DEBUG")) {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        std::cerr << "[perf-gateway] preflight wait failed received="
                  << server_state_.warmup_received << " expected="
                  << preflight_messages << std::endl;
    }
    return primed && !server_state_.fatal;
}

int run_case (const std::string &lib_name_,
              const std::string &transport_,
              size_t msg_size_)
{
    if (!is_supported_transport (transport_)) {
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
    gateway_ready_monitor_t client_monitor;
    gateway_recv_loop_t recv_loop;
    queue_probe_t *probe = NULL;
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
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }
    if (zlink_set_routing_id (server_gateway, server_routing_id.c_str (),
                                      server_routing_id.size ())
          != 0
        || zlink_set_routing_id (client_gateway, client_routing_id.c_str (),
                                         client_routing_id.size ())
             != 0) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    queue_probe_t client_probe (client_gateway, server_gateway);
    probe = &client_probe;

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

    if (!configure_tls_server (server_gateway, transport_)
        || !configure_tls_client (client_gateway, transport_)) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    if (!open_gateway_ready_monitor (client_gateway, &client_monitor)) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    const int base_port =
      33000 + (current_process_id () % 1000) * 8;
    const std::string endpoint =
      bind_server_gateway (server_gateway, transport_, base_port);
    if (endpoint.empty ()) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    zlink_routing_id_t server_rid;
    std::memset (&server_rid, 0, sizeof (server_rid));
    if (zlink_get_routing_id (server_gateway, &server_rid) != 0) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    if (zlink_gateway_connect (client_gateway, endpoint.c_str (), &server_rid)
        != 0
        || !wait_gateway_ready (&client_monitor, 1, 5000)) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }
    if (!wait_gateway_settle_phase (&client_monitor, SETTLE_TIME_MS)) {
        print_fail ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    server_state.run_id = static_cast<uint32_t> (current_process_id ());
    server_state.msg_size = msg_size_;
    server_state.probe = probe;
    recv_loop.gateway = server_gateway;
    recv_loop.state = &server_state;
    start_gateway_recv_loop (&recv_loop);
    if (!wait_gateway_recv_loop_ready (&recv_loop, 1000)) {
        print_fail ();
        stop_gateway_recv_loop (&recv_loop);
        cleanup_gateway_case (
          &client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    std::vector<char> payload;
    if (!prime_gateway_route (
          client_gateway, &server_rid, &client_monitor, server_state, *probe,
          payload,
          msg_size_)) {
        const queue_stats_t queue_stats = probe->snapshot ();
        server_state.probe = NULL;
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        stop_gateway_recv_loop (&recv_loop);
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }
    if (!run_gateway_phase_window (
          client_gateway, &server_rid, &client_monitor, server_state, *probe,
          payload, msg_size_, perf_single_metric::phase_warmup,
          resolve_single_warmup_seconds (), NULL, NULL)) {
        const queue_stats_t queue_stats = probe->snapshot ();
        server_state.probe = NULL;
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        stop_gateway_recv_loop (&recv_loop);
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }
    if (!wait_gateway_settle_phase (&client_monitor, SETTLE_TIME_MS)
        || !wait_gateway_ready (&client_monitor, 1, 5000)) {
        const queue_stats_t queue_stats = probe->snapshot ();
        server_state.probe = NULL;
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        stop_gateway_recv_loop (&recv_loop);
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok = run_gateway_phase_window (
      client_gateway, &server_rid, &client_monitor, server_state, *probe,
      payload, msg_size_, perf_single_metric::phase_active,
      resolve_single_duration_seconds (), &throughput, &latency);
    const queue_stats_t queue_stats = probe->snapshot ();
    server_state.probe = NULL;
    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  active_ok ? throughput : 0.0,
                  active_ok ? latency.mean_us : 0.0,
                  active_ok ? latency.p95_us : 0.0,
                  active_ok ? latency.p99_us : 0.0,
                  queue_stats);

    stop_gateway_recv_loop (&recv_loop);
    cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
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
