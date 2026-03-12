#include "../common/bench_common.hpp"
#include "../common/perf_single_metric_header.hpp"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
static const char *k_pattern = "GATEWAY";
static const char *k_server_routing_id = "perf-gateway-server";

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

struct gateway_queue_probe_t
{
    gateway_queue_probe_t (void *gateway_) :
        gateway (gateway_),
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

    void sample_send_if_due () { maybe_sample (true, false); }
    void sample_recv_if_due () { maybe_sample (false, false); }
    void force_sample () { maybe_sample (true, true); }

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

    void *gateway;
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

    static unsigned long long peer_score (const zlink_gateway_peer_info_t &info_)
    {
        return static_cast<unsigned long long> (info_.msgs_sent)
               + static_cast<unsigned long long> (info_.msgs_received);
    }

    bool read_best_peer (zlink_gateway_peer_info_t *info_out_) const
    {
        if (!gateway || !info_out_)
            return false;

        size_t count = 0;
        if (zlink_gateway_router_peers (gateway, NULL, &count) != 0 || count == 0)
            return false;

        std::vector<zlink_gateway_peer_info_t> peers (count);
        size_t filled = count;
        if (zlink_gateway_router_peers (gateway, &peers[0], &filled) != 0
            || filled == 0) {
            return false;
        }

        size_t best = 0;
        for (size_t i = 1; i < filled; ++i) {
            const zlink_gateway_peer_info_t &cand = peers[i];
            const zlink_gateway_peer_info_t &cur = peers[best];
            if (cand.connected_time > cur.connected_time
                || (cand.connected_time == cur.connected_time
                    && peer_score (cand) > peer_score (cur))) {
                best = i;
            }
        }

        *info_out_ = peers[best];
        return true;
    }

    void maybe_sample (bool send_path_, bool force_)
    {
        zlink_gateway_peer_info_t info;
        if (!read_best_peer (&info))
            return;

        const unsigned long long now = now_ns ();
        unsigned long long &last_sample_ns =
          send_path_ ? last_send_sample_ns : last_recv_sample_ns;
        if (!force_ && last_sample_ns > 0
            && now - last_sample_ns < sample_interval_ns) {
            return;
        }
        last_sample_ns = now;

        if (send_path_) {
            const unsigned long long pending =
              static_cast<unsigned long long> (info.snd_pending_msgs);
            if (!seen_send || pending > snd_pending_max)
                snd_pending_max = pending;
            seen_send = true;
            return;
        }

        const unsigned long long pending =
          static_cast<unsigned long long> (info.rcv_pending_msgs);
        if (!seen_recv || pending > rcv_pending_max)
            rcv_pending_max = pending;
        rcv_pending_end = pending;
        seen_recv = true;
    }
};

struct gateway_server_state_t
{
    gateway_server_state_t () : gateway (NULL), send_failures (0) {}

    void *gateway;
    std::atomic<int> send_failures;
};

struct gateway_client_state_t
{
    gateway_client_state_t () :
        run_id (0),
        msg_size (0),
        warmup_received (0),
        active_received (0),
        probe (NULL)
    {
    }

    uint32_t run_id;
    size_t msg_size;
    unsigned long long warmup_received;
    unsigned long long active_received;
    latency_stats_builder_t latency;
    gateway_queue_probe_t *probe;
    std::mutex mutex;
    std::condition_variable cv;
};

gateway_server_state_t *g_server_state = NULL;
gateway_client_state_t *g_client_state = NULL;

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

bool wait_for_gateway_connections (void *gateway_, int expected_, int timeout_ms_)
{
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_gateway_connection_count (gateway_) >= expected_)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return zlink_gateway_connection_count (gateway_) >= expected_;
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
                   std::vector<char> &payload_,
                   size_t msg_size_,
                   uint32_t run_id_,
                   perf_single_metric::phase_t phase_,
                   uint64_t seq_)
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
    if (zlink_gateway_send (gateway_, &part, 1, 0) != 0) {
        const int err = errno;
        zlink_msg_close (&part);
        errno = err;
        return false;
    }

    return true;
}

void server_handler (const zlink_routing_id_t *source_rid_,
                     zlink_msg_t *parts_,
                     size_t part_count_)
{
    if (!g_server_state || !g_server_state->gateway || !source_rid_
        || part_count_ == 0) {
        close_parts (parts_, part_count_);
        return;
    }

    if (zlink_gateway_send_rid (
          g_server_state->gateway, source_rid_, parts_, part_count_, 0)
        != 0) {
        g_server_state->send_failures.fetch_add (1, std::memory_order_release);
        close_parts (parts_, part_count_);
    }
}

void client_handler (const zlink_routing_id_t *,
                     zlink_msg_t *parts_,
                     size_t part_count_)
{
    gateway_client_state_t *state = g_client_state;
    if (!state || part_count_ == 0) {
        close_parts (parts_, part_count_);
        return;
    }

    perf_single_metric::header_t header;
    const bool header_ok =
      perf_single_metric::decode_payload_header (
        zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &header)
      && header.run_id == state->run_id && header.msg_size == state->msg_size;

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (header_ok
            && header.phase == static_cast<uint32_t> (
                                perf_single_metric::phase_warmup)) {
            ++state->warmup_received;
        } else if (header_ok
                   && header.phase == static_cast<uint32_t> (
                                        perf_single_metric::phase_active)) {
            ++state->active_received;
            const uint64_t now_us = perf_single_metric::now_us ();
            const double latency_us =
              now_us >= header.sent_ts_us
                ? static_cast<double> (now_us - header.sent_ts_us)
                : 0.0;
            state->latency.add (latency_us);
        }
        if (state->probe)
            state->probe->sample_recv_if_due ();
    }

    close_parts (parts_, part_count_);
    state->cv.notify_all ();
}

bool run_warmup (void *gateway_,
                 gateway_client_state_t &client_state_,
                 gateway_queue_probe_t &probe_,
                 std::vector<char> &payload_,
                 size_t msg_size_)
{
    const int warmup_count = resolve_bench_count ("PERF_WARMUP_COUNT", 200);
    const int wait_timeout_ms = std::max (1000, resolve_single_recv_timeout_ms () * 10);
    for (int i = 0; i < warmup_count; ++i) {
        probe_.sample_send_if_due ();
        if (!send_payload (gateway_,
                           payload_,
                           msg_size_,
                           client_state_.run_id,
                           perf_single_metric::phase_warmup,
                           static_cast<uint64_t> (i + 1))) {
            return false;
        }

        if (!wait_for_counter (client_state_.cv,
                               client_state_.mutex,
                               &client_state_.warmup_received,
                               static_cast<unsigned long long> (i + 1),
                               wait_timeout_ms)) {
            return false;
        }
    }
    return true;
}

bool run_active (void *gateway_,
                 gateway_client_state_t &client_state_,
                 gateway_queue_probe_t &probe_,
                 std::vector<char> &payload_,
                 size_t msg_size_,
                 double *throughput_out_,
                 latency_stats_t *latency_out_)
{
    const int duration_s = resolve_single_duration_seconds ();
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);
    const auto active_start = std::chrono::steady_clock::now ();
    unsigned long long target = 0;
    uint64_t seq = 1;

    while (std::chrono::steady_clock::now () < deadline) {
        probe_.sample_send_if_due ();
        if (!send_payload (gateway_,
                           payload_,
                           msg_size_,
                           client_state_.run_id,
                           perf_single_metric::phase_active,
                           seq++)) {
            return false;
        }
        ++target;
        if (!wait_for_counter (client_state_.cv,
                               client_state_.mutex,
                               &client_state_.active_received,
                               target,
                               std::max (
                                 1000, resolve_single_recv_timeout_ms () * 10))) {
            break;
        }
    }

    const double elapsed_s = std::max (
      0.001,
      std::chrono::duration_cast<std::chrono::duration<double> > (
        std::chrono::steady_clock::now () - active_start)
        .count ());

    if (throughput_out_)
        *throughput_out_ =
          static_cast<double> (client_state_.active_received) / elapsed_s;
    if (latency_out_)
        *latency_out_ = client_state_.latency.snapshot ();
    return client_state_.active_received > 0;
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
    gateway_client_state_t client_state;
    g_server_state = &server_state;
    g_client_state = &client_state;

    void *server_gateway =
      zlink_gateway_new (ctx.get (), "perf-gateway", k_server_routing_id,
                         &server_handler);
    void *client_gateway =
      zlink_gateway_new (ctx.get (), "perf-gateway", "perf-gateway-client",
                         &client_handler);
    if (!server_gateway || !client_gateway) {
        if (server_gateway)
            zlink_gateway_destroy (&server_gateway);
        if (client_gateway)
            zlink_gateway_destroy (&client_gateway);
        g_server_state = NULL;
        g_client_state = NULL;
        return 1;
    }

    const int linger = 0;
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    const int sndtimeo_ms = resolve_single_send_timeout_ms ();
    const int rcvtimeo_ms = resolve_single_recv_timeout_ms ();
    (void) zlink_gateway_set_option (
      server_gateway, ZLINK_GATEWAY_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_gateway_set_option (
      server_gateway, ZLINK_GATEWAY_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_gateway_set_option (
      server_gateway, ZLINK_GATEWAY_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));
    (void) zlink_gateway_set_option (
      server_gateway, ZLINK_GATEWAY_OPT_SNDTIMEO, &sndtimeo_ms,
      sizeof (sndtimeo_ms));
    (void) zlink_gateway_set_option (
      client_gateway, ZLINK_GATEWAY_OPT_LINGER, &linger, sizeof (linger));
    (void) zlink_gateway_set_option (
      client_gateway, ZLINK_GATEWAY_OPT_SNDHWM, &sndhwm, sizeof (sndhwm));
    (void) zlink_gateway_set_option (
      client_gateway, ZLINK_GATEWAY_OPT_RCVHWM, &rcvhwm, sizeof (rcvhwm));
    (void) zlink_gateway_set_option (
      client_gateway, ZLINK_GATEWAY_OPT_SNDTIMEO, &sndtimeo_ms,
      sizeof (sndtimeo_ms));
    (void) rcvtimeo_ms;

    if (!configure_tls_server (server_gateway, transport_)
        || !configure_tls_client (client_gateway, transport_)) {
        zlink_gateway_destroy (&client_gateway);
        zlink_gateway_destroy (&server_gateway);
        g_server_state = NULL;
        g_client_state = NULL;
        return 1;
    }

    const int base_port =
      33000 + (current_process_id () % 1000) * 8;
    const std::string endpoint =
      bind_server_gateway (server_gateway, transport_, base_port);
    if (endpoint.empty ()) {
        zlink_gateway_destroy (&client_gateway);
        zlink_gateway_destroy (&server_gateway);
        g_server_state = NULL;
        g_client_state = NULL;
        return 1;
    }

    zlink_routing_id_t server_rid;
    std::memset (&server_rid, 0, sizeof (server_rid));
    const size_t rid_size = std::strlen (k_server_routing_id);
    server_rid.size = static_cast<uint8_t> (
      rid_size < sizeof (server_rid.data) ? rid_size : sizeof (server_rid.data));
    std::memcpy (server_rid.data, k_server_routing_id, server_rid.size);

    if (zlink_gateway_connect (client_gateway, endpoint.c_str (), &server_rid)
        != 0
        || !wait_for_gateway_connections (client_gateway, 1, 5000)) {
        zlink_gateway_destroy (&client_gateway);
        zlink_gateway_destroy (&server_gateway);
        g_server_state = NULL;
        g_client_state = NULL;
        return 1;
    }

    std::this_thread::sleep_for (std::chrono::milliseconds (SETTLE_TIME_MS));

    server_state.gateway = server_gateway;
    client_state.run_id = static_cast<uint32_t> (current_process_id ());
    client_state.msg_size = msg_size_;
    gateway_queue_probe_t probe (client_gateway);
    client_state.probe = &probe;

    std::vector<char> payload;
    if (!run_warmup (client_gateway, client_state, probe, payload, msg_size_)) {
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
        zlink_gateway_destroy (&client_gateway);
        zlink_gateway_destroy (&server_gateway);
        g_server_state = NULL;
        g_client_state = NULL;
        return 1;
    }

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok =
      run_active (
        client_gateway, client_state, probe, payload, msg_size_, &throughput,
        &latency);
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

    const bool success =
      active_ok
      && server_state.send_failures.load (std::memory_order_acquire) == 0;

    zlink_gateway_destroy (&client_gateway);
    zlink_gateway_destroy (&server_gateway);
    g_server_state = NULL;
    g_client_state = NULL;
    return success ? 0 : 1;
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
