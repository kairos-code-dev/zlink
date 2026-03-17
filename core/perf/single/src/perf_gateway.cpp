#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"

#include <atomic>
#include <condition_variable>
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

int resolve_gateway_warmup_count ()
{
    return parse_positive_env ("PERF_WARMUP_COUNT", 200);
}

struct gateway_queue_probe_t
{
    gateway_queue_probe_t () :
        monitor (NULL),
        sample_interval_ns (resolve_sample_interval_ns ()),
        last_send_sample_ns (0),
        last_recv_sample_ns (0),
        snd_pending_max (0),
        rcv_pending_max (0),
        rcv_pending_end (0),
        seen_send (false),
        seen_recv (false)
    {
    }

    ~gateway_queue_probe_t () { close (); }

    void set_monitor (void *monitor_) { monitor = monitor_; }
    void sample_send_if_due () { maybe_sample (true, false); }
    void sample_recv_if_due () { maybe_sample (false, false); }
    void force_sample () { maybe_sample (true, true); }
    void close () {}

    queue_stats_t snapshot ()
    {
        force_sample ();
        queue_stats_t stats;
        if (seen_send) {
            stats.has_snd_pending = true;
            stats.snd_pending_max = static_cast<double> (snd_pending_max);
        }
        if (seen_recv) {
            stats.has_rcv_pending = true;
            stats.rcv_pending_max = static_cast<double> (rcv_pending_max);
            stats.rcv_pending_end = static_cast<double> (rcv_pending_end);
        }
        return stats;
    }

    void *monitor;
    unsigned long long sample_interval_ns;
    unsigned long long last_send_sample_ns;
    unsigned long long last_recv_sample_ns;
    unsigned long long snd_pending_max;
    unsigned long long rcv_pending_max;
    unsigned long long rcv_pending_end;
    bool seen_send;
    bool seen_recv;

  private:
    static unsigned long long resolve_sample_interval_ns ()
    {
        const int sample_ms = resolve_single_queue_sample_ms ();
        const unsigned long long clamped_ms =
          static_cast<unsigned long long> (sample_ms > 0 ? sample_ms : 100);
        return clamped_ms * 1000000ULL;
    }

    static unsigned long long now_ns ()
    {
        return static_cast<unsigned long long> (
          std::chrono::duration_cast<std::chrono::nanoseconds> (
            std::chrono::steady_clock::now ().time_since_epoch ())
            .count ());
    }

    void maybe_sample (bool send_path_, bool force_)
    {
        const unsigned long long now = now_ns ();
        unsigned long long &last_sample_ns =
          send_path_ ? last_send_sample_ns : last_recv_sample_ns;
        if (!force_ && last_sample_ns > 0
            && now - last_sample_ns < sample_interval_ns) {
            return;
        }
        last_sample_ns = now;
        if (!monitor)
            return;

        zlink_monitor_snapshot_t snapshot;
        memset (&snapshot, 0, sizeof (snapshot));
        if (zlink_monitor_snapshot (monitor, &snapshot) != 0) {
            return;
        }

        if ((snapshot.detail_flags
             & ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS)
            != 0) {
            seen_send = true;
            if (snapshot.snd_pending_msgs > snd_pending_max)
                snd_pending_max = snapshot.snd_pending_msgs;
        }
        if ((snapshot.detail_flags
             & ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS)
            != 0) {
            seen_recv = true;
            if (snapshot.rcv_pending_msgs > rcv_pending_max)
                rcv_pending_max = snapshot.rcv_pending_msgs;
            rcv_pending_end = snapshot.rcv_pending_msgs;
        }
        (void) send_path_;
        (void) force_;
    }
};

struct gateway_server_state_t
{
    gateway_server_state_t () :
        run_id (0),
        msg_size (0),
        active_deadline_us (0),
        warmup_received (0),
        active_received (0),
        probe (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    uint64_t active_deadline_us;
    unsigned long long warmup_received;
    unsigned long long active_received;
    latency_stats_builder_t latency;
    gateway_queue_probe_t *probe;
    std::mutex mutex;
    std::condition_variable cv;
};

gateway_server_state_t *g_server_state = NULL;

struct gateway_ready_monitor_t
{
    gateway_ready_monitor_t () : gateway (NULL), monitor (NULL) {}

    void *gateway;
    void *monitor;
};

bool read_gateway_monitor_snapshot (void *gateway_,
                                    void *monitor_,
                                    zlink_monitor_snapshot_t *out_)
{
    if (!gateway_ || !monitor_ || !out_)
        return false;

    memset (out_, 0, sizeof (*out_));
    return zlink_monitor_snapshot (monitor_, out_) == 0;
}

bool open_gateway_ready_monitor (void *gateway_,
                                 gateway_ready_monitor_t *out_)
{
    if (!gateway_ || !out_)
        return false;

    out_->gateway = gateway_;
    out_->monitor = zlink_gateway_monitor_open (
      gateway_,
      ZLINK_GATEWAY_SEND_READY_CHANGED | ZLINK_GATEWAY_ROUTE_UP
        | ZLINK_GATEWAY_ROUTE_DOWN | ZLINK_GATEWAY_MONITOR_EVENT_ERROR,
      &zlink_service_monitor_ignore_handler, NULL);
    return out_->monitor != NULL;
}

bool wait_gateway_ready (gateway_ready_monitor_t *monitor_,
                         size_t expected_,
                         int timeout_ms_)
{
    if (expected_ == 0)
        return true;
    if (!monitor_ || !monitor_->gateway || !monitor_->monitor)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    int stable_ready_samples = 0;
    const int required_stable_ready_samples = 5;
    while (std::chrono::steady_clock::now () < deadline) {
        zlink_monitor_snapshot_t snapshot;
        if (read_gateway_monitor_snapshot (
              monitor_->gateway, monitor_->monitor, &snapshot)
            && snapshot.ready_peer_count >= expected_
            && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0) {
            ++stable_ready_samples;
            if (stable_ready_samples >= required_stable_ready_samples)
                return true;
        } else {
            stable_ready_samples = 0;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    zlink_monitor_snapshot_t snapshot;
    return read_gateway_monitor_snapshot (
             monitor_->gateway, monitor_->monitor, &snapshot)
           && snapshot.ready_peer_count >= expected_
           && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0;
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

    while (std::chrono::steady_clock::now () < deadline_) {
        zlink_monitor_snapshot_t snapshot;
        if (read_gateway_monitor_snapshot (
              monitor_->gateway, monitor_->monitor, &snapshot)
            && snapshot.ready_peer_count >= expected_
            && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0) {
            return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    zlink_monitor_snapshot_t snapshot;
    return read_gateway_monitor_snapshot (
             monitor_->gateway, monitor_->monitor, &snapshot)
           && snapshot.ready_peer_count >= expected_
           && (snapshot.state_flags & ZLINK_MONITOR_STATE_SEND_READY) != 0;
}

void close_gateway_ready_monitor (gateway_ready_monitor_t *monitor_)
{
    if (!monitor_)
        return;

    if (monitor_->monitor)
        (void) zlink_service_monitor_close (&monitor_->monitor);
    monitor_->gateway = NULL;
    monitor_->monitor = NULL;
}

void cleanup_gateway_case (void **client_gateway_,
                           void **server_gateway_,
                           gateway_ready_monitor_t *client_monitor_)
{
    g_server_state = NULL;
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
    return zlink_gateway_set_tls_client (
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
    return zlink_gateway_set_tls_server (
             gateway_, cert_path.c_str (), key_path.c_str ())
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

void server_handler (const zlink_routing_id_t *source_rid_,
                     zlink_msg_t *parts_,
                     size_t part_count_,
                     void *)
{
    gateway_server_state_t *state = g_server_state;
    if (!state || !source_rid_ || part_count_ == 0) {
        close_parts (parts_, part_count_);
        return;
    }

    perf_single_metric::header_t header;
    const bool header_ok =
      perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &header)
      && header.run_id == state->run_id && header.msg_size == state->msg_size;

    if (header_ok) {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (header.phase
            == static_cast<uint32_t> (perf_single_metric::phase_warmup)) {
            ++state->warmup_received;
        } else if (header.phase
                   == static_cast<uint32_t> (
                        perf_single_metric::phase_active)) {
            const uint64_t now_us = perf_single_metric::now_us ();
            if (state->active_deadline_us > 0
                && now_us <= state->active_deadline_us) {
                ++state->active_received;
                state->latency.add (
                  now_us >= header.sent_ts_us
                    ? static_cast<double> (now_us - header.sent_ts_us)
                    : 0.0);
            }
        }
        if (state->probe)
            state->probe->sample_recv_if_due ();
    }

    close_parts (parts_, part_count_);
    state->cv.notify_all ();
}

void client_handler (const zlink_routing_id_t *,
                     zlink_msg_t *parts_,
                     size_t part_count_,
                     void *)
{
    close_parts (parts_, part_count_);
}

bool run_warmup (void *gateway_,
                 const zlink_routing_id_t *target_rid_,
                 gateway_server_state_t &server_state_,
                 gateway_queue_probe_t &probe_,
                 std::vector<char> &payload_,
                 size_t msg_size_)
{
    const int warmup_count = resolve_gateway_warmup_count ();
    uint64_t seq = 1;

    {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        server_state_.warmup_received = 0;
        server_state_.active_received = 0;
        server_state_.active_deadline_us = 0;
        server_state_.latency = latency_stats_builder_t ();
    }

    for (int sent_count = 0; sent_count < warmup_count; ++sent_count) {
        probe_.sample_send_if_due ();
        if (!send_payload (gateway_,
                           target_rid_,
                           payload_,
                           msg_size_,
                           server_state_.run_id,
                           perf_single_metric::phase_warmup,
                           seq++,
                           0)) {
            return false;
        }
    }

    // idle-wait: no new warmup messages for idle_timeout → warmup receive done
    {
        std::unique_lock<std::mutex> lock (server_state_.mutex);
        const std::chrono::milliseconds idle_timeout (
          std::max (10, resolve_single_recv_timeout_ms ()));
        while (true) {
            const unsigned long long observed = server_state_.warmup_received;
            const bool changed = server_state_.cv.wait_for (
              lock, idle_timeout, [&server_state_, observed] () {
                  return server_state_.warmup_received != observed;
              });
            if (!changed)
                break;
        }
    }
    if (getenv ("PERF_DEBUG")) {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        std::cerr << "[perf-gateway] warmup received="
                  << server_state_.warmup_received << std::endl;
    }
    return server_state_.warmup_received
           >= static_cast<unsigned long long> (warmup_count);
}

bool prime_gateway_route (void *gateway_,
                          const zlink_routing_id_t *target_rid_,
                          gateway_ready_monitor_t *monitor_,
                          gateway_server_state_t &server_state_,
                          gateway_queue_probe_t &probe_,
                          std::vector<char> &payload_,
                          size_t msg_size_)
{
    const unsigned long long preflight_messages = 32;

    {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        server_state_.warmup_received = 0;
        server_state_.active_received = 0;
        server_state_.active_deadline_us = 0;
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
    return primed;
}

bool run_active (void *gateway_,
                 const zlink_routing_id_t *target_rid_,
                 gateway_ready_monitor_t *monitor_,
                 gateway_server_state_t &server_state_,
                 gateway_queue_probe_t &probe_,
                 std::vector<char> &payload_,
                 size_t msg_size_,
                 double *throughput_out_,
                 latency_stats_t *latency_out_)
{
    const int duration_s = resolve_single_duration_seconds ();
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);
    uint64_t seq = 1;

    {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        server_state_.active_received = 0;
        server_state_.active_deadline_us =
          perf_single_metric::now_us ()
          + static_cast<uint64_t> (
              std::max (1, duration_s) * 1000000ULL);
        server_state_.latency = latency_stats_builder_t ();
    }

    while (std::chrono::steady_clock::now () < deadline) {
        probe_.sample_send_if_due ();
        if (send_payload (gateway_,
                          target_rid_,
                          payload_,
                          msg_size_,
                          server_state_.run_id,
                          perf_single_metric::phase_active,
                          seq,
                          ZLINK_DONTWAIT)) {
            ++seq;
            continue;
        }

        if (errno != EAGAIN && errno != EHOSTUNREACH && errno != ENOTCONN)
            return false;
        if (!wait_gateway_send_ready_until (monitor_, 1, deadline))
            break;
        if (std::chrono::steady_clock::now () >= deadline)
            break;
    }

    {
        std::unique_lock<std::mutex> lock (server_state_.mutex);
        const std::chrono::milliseconds idle_timeout (
          std::max (1000, resolve_single_recv_timeout_ms () * 10));
        while (true) {
            const unsigned long long observed = server_state_.active_received;
            const bool changed = server_state_.cv.wait_for (
              lock, idle_timeout, [&server_state_, observed] () {
                  return server_state_.active_received != observed;
              });
            if (!changed)
                break;
        }
    }

    unsigned long long active_received = 0;
    {
        std::lock_guard<std::mutex> lock (server_state_.mutex);
        active_received = server_state_.active_received;
        if (latency_out_)
            *latency_out_ = server_state_.latency.snapshot ();
    }
    if (getenv ("PERF_DEBUG")) {
        std::cerr << "[perf-gateway] active received=" << active_received
                  << " duration_s=" << duration_s << std::endl;
    }

    if (throughput_out_)
        *throughput_out_ =
          static_cast<double> (active_received)
          / static_cast<double> (std::max (1, duration_s));
    return active_received > 0;
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
    g_server_state = &server_state;
    gateway_queue_probe_t *probe = NULL;
    auto print_fail = [&] () {
        if (probe) {
            print_queue_metrics (
              lib_name_, k_pattern, transport_, msg_size_, probe->snapshot ());
            return;
        }
        print_fail_result (lib_name_, k_pattern, transport_, msg_size_);
    };
    auto close_probe = [&] () {
        if (probe)
            probe->close ();
    };
    const std::string run_token = make_gateway_run_token ();
    const std::string service_name = "perf-gateway-" + run_token;
    const std::string server_routing_id = "perf-gateway-server-" + run_token;
    const std::string client_routing_id = "perf-gateway-client-" + run_token;

    void *server_gateway =
      zlink_gateway_new (ctx.get (), service_name.c_str (),
                         server_routing_id.c_str (),
                         &server_handler, NULL);
    void *client_gateway =
      zlink_gateway_new (ctx.get (), service_name.c_str (),
                         client_routing_id.c_str (),
                         &client_handler, NULL);
    if (!server_gateway || !client_gateway) {
        print_fail ();
        close_probe ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    gateway_queue_probe_t client_probe;
    probe = &client_probe;

    const int linger = 0;
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    (void) zlink_gateway_set_option (
      server_gateway, ZLINK_GATEWAY_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_gateway_set_option (
      server_gateway, ZLINK_GATEWAY_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_gateway_set_option (
      server_gateway, ZLINK_GATEWAY_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));
    (void) zlink_gateway_set_option (
      client_gateway, ZLINK_GATEWAY_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_gateway_set_option (
      client_gateway, ZLINK_GATEWAY_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_gateway_set_option (
      client_gateway, ZLINK_GATEWAY_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));

    if (!configure_tls_server (server_gateway, transport_)
        || !configure_tls_client (client_gateway, transport_)) {
        print_fail ();
        close_probe ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    if (!open_gateway_ready_monitor (client_gateway, &client_monitor)) {
        print_fail ();
        close_probe ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }
    client_probe.set_monitor (client_monitor.monitor);

    const int base_port =
      33000 + (current_process_id () % 1000) * 8;
    const std::string endpoint =
      bind_server_gateway (server_gateway, transport_, base_port);
    if (endpoint.empty ()) {
        print_fail ();
        close_probe ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    zlink_routing_id_t server_rid;
    std::memset (&server_rid, 0, sizeof (server_rid));
    if (zlink_gateway_routing_id (server_gateway, &server_rid) != 0) {
        print_fail ();
        close_probe ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    if (zlink_gateway_connect (client_gateway, endpoint.c_str (), &server_rid)
        != 0
        || !wait_gateway_ready (&client_monitor, 1, 5000)) {
        print_fail ();
        close_probe ();
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    server_state.run_id = static_cast<uint32_t> (current_process_id ());
    server_state.msg_size = msg_size_;
    server_state.probe = probe;

    std::vector<char> payload;
    if (!prime_gateway_route (
          client_gateway, &server_rid, &client_monitor, server_state, *probe,
          payload,
          msg_size_)) {
        const queue_stats_t queue_stats = probe->snapshot ();
        server_state.probe = NULL;
        probe->close ();
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }
    if (!run_warmup (
          client_gateway, &server_rid, server_state, *probe, payload,
          msg_size_)) {
        const queue_stats_t queue_stats = probe->snapshot ();
        server_state.probe = NULL;
        probe->close ();
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (SETTLE_TIME_MS));
    if (!wait_gateway_ready (&client_monitor, 1, 5000)) {
        const queue_stats_t queue_stats = probe->snapshot ();
        server_state.probe = NULL;
        probe->close ();
        print_result (lib_name_,
                      k_pattern,
                      transport_,
                      msg_size_,
                      0.0,
                      0.0,
                      0.0,
                      0.0,
                      queue_stats);
        cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok =
      run_active (
        client_gateway, &server_rid, &client_monitor, server_state, *probe,
        payload, msg_size_,
        &throughput,
        &latency);
    const queue_stats_t queue_stats = probe->snapshot ();
    server_state.probe = NULL;
    probe->close ();
    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  active_ok ? throughput : 0.0,
                  active_ok ? latency.mean_us : 0.0,
                  active_ok ? latency.p95_us : 0.0,
                  active_ok ? latency.p99_us : 0.0,
                  queue_stats);

    cleanup_gateway_case (&client_gateway, &server_gateway, &client_monitor);
    return active_ok ? 0 : 1;
}
} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t msg_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    return run_case (lib_name, transport, msg_size > 0 ? msg_size : 64);
}
