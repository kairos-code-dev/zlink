#include "perf_single_common.hpp"

#include <cerrno>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <thread>

namespace perf {
namespace single {

latency_stats_builder_t::latency_stats_builder_t (size_t sample_cap_)
    : _sample_cap (sample_cap_ > 0 ? sample_cap_ : 1),
      _count (0),
      _sum_us (0.0),
      _rng_state (0x9e3779b97f4a7c15ULL)
{
    _samples.reserve (_sample_cap);
}

void latency_stats_builder_t::add (double latency_us_)
{
    const double sample = latency_us_ >= 0.0 ? latency_us_ : 0.0;
    ++_count;
    _sum_us += sample;

    if (_samples.size () < _sample_cap) {
        _samples.push_back (sample);
        return;
    }

    const unsigned long long slot = next_rand_u64 () % _count;
    if (slot < static_cast<unsigned long long> (_sample_cap))
        _samples[static_cast<size_t> (slot)] = sample;
}

unsigned long long latency_stats_builder_t::count () const
{
    return _count;
}

latency_stats_t latency_stats_builder_t::snapshot ()
{
    latency_stats_t out;
    if (_count == 0)
        return out;

    out.mean_us = _sum_us / static_cast<double> (_count);
    if (_samples.empty ()) {
        out.p95_us = out.mean_us;
        out.p99_us = out.mean_us;
        return out;
    }

    std::sort (_samples.begin (), _samples.end ());
    out.p95_us = percentile_from_sorted (_samples, 0.95);
    out.p99_us = percentile_from_sorted (_samples, 0.99);
    if (out.p95_us < out.mean_us)
        out.p95_us = out.mean_us;
    if (out.p99_us < out.p95_us)
        out.p99_us = out.p95_us;
    return out;
}

double latency_stats_builder_t::percentile_from_sorted (
  const std::vector<double> &sorted_,
  double q_)
{
    if (sorted_.empty ())
        return 0.0;
    if (q_ <= 0.0)
        return sorted_.front ();
    if (q_ >= 1.0)
        return sorted_.back ();

    const double pos = (sorted_.size () - 1) * q_;
    const size_t lo = static_cast<size_t> (pos);
    const size_t hi = lo + 1 < sorted_.size () ? lo + 1 : lo;
    const double frac = pos - static_cast<double> (lo);
    return sorted_[lo] + (sorted_[hi] - sorted_[lo]) * frac;
}

unsigned long long latency_stats_builder_t::next_rand_u64 ()
{
    if (_rng_state == 0)
        _rng_state = 0x9e3779b97f4a7c15ULL;
    unsigned long long x = _rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    _rng_state = x;
    return x;
}

ctx_guard_t::ctx_guard_t () : _ctx ()
{
    if (_ctx.handle ())
        apply_ctx_options (_ctx);
}

ctx_guard_t::~ctx_guard_t ()
{
    if (_ctx.handle ())
        (void) _ctx.shutdown ();
}

socket_guard_t::socket_guard_t () : _sock () {}

socket_guard_t::socket_guard_t (ctx_guard_t &ctx_, zlink::socket_type type_)
    : _sock (ctx_.ctx (), type_)
{
}

int parse_positive_env (const char *name_, int default_value_)
{
    if (!name_)
        return default_value_;

    const char *env = std::getenv (name_);
    if (!env || !*env)
        return default_value_;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol (env, &end, 10);
    if (errno != 0 || end == env || parsed <= 0)
        return default_value_;

    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int> (parsed);
}

int resolve_single_duration_seconds ()
{
    return parse_positive_env ("PERF_SINGLE_DURATION_SECONDS", 5);
}

size_t resolve_single_latency_sample_cap ()
{
    const int cap = parse_positive_env ("PERF_SINGLE_LATENCY_SAMPLE_CAP", 200000);
    return cap > 0 ? static_cast<size_t> (cap) : static_cast<size_t> (200000);
}

int resolve_single_send_timeout_ms ()
{
    return parse_positive_env ("PERF_SINGLE_SNDTIMEO_MS", 200);
}

int resolve_single_recv_timeout_ms ()
{
    return parse_positive_env ("PERF_SINGLE_RCVTIMEO_MS", 200);
}

int resolve_single_pubsub_recv_timeout_ms ()
{
    return parse_positive_env ("PERF_SINGLE_PUBSUB_RCVTIMEO_MS",
                               resolve_single_recv_timeout_ms ());
}

int resolve_single_socket_hwm (bool send_)
{
    const int base_hwm = parse_positive_env ("PERF_SINGLE_HWM", 1000);
    return send_ ? parse_positive_env ("PERF_SINGLE_SNDHWM", base_hwm)
                 : parse_positive_env ("PERF_SINGLE_RCVHWM", base_hwm);
}

int resolve_single_queue_sample_ms ()
{
    return parse_positive_env ("PERF_SINGLE_QUEUE_SAMPLE_MS", 100);
}

int resolve_single_queue_sample_every_msgs ()
{
    return parse_positive_env ("PERF_SINGLE_QUEUE_SAMPLE_EVERY_MSGS", 64);
}

int resolve_bench_count (const char *env_name, int default_value)
{
    return parse_positive_env (env_name, default_value);
}

bool bench_debug_enabled ()
{
    static const bool enabled = std::getenv ("PERF_DEBUG") != NULL;
    return enabled;
}

void apply_ctx_options (zlink::context_t &ctx_)
{
    const int io_threads = parse_positive_env ("PERF_IO_THREADS", 0);
    if (io_threads > 0)
        (void) ctx_.set (zlink::context_option::io_threads, io_threads);

    int max_sockets = parse_positive_env ("PERF_MAX_SOCKETS", 0);
    if (max_sockets <= 0) {
        const int clients = parse_positive_env ("PERF_CLIENTS", 0);
        if (clients > 0) {
            const long required = static_cast<long> (clients) + 4096L;
            max_sockets = required > INT_MAX ? INT_MAX : static_cast<int> (required);
        }
    }
    if (max_sockets > 0)
        (void) ctx_.set (zlink::context_option::max_sockets, max_sockets);
}

bool set_sockopt_int (zlink::socket_t &socket_,
                      zlink::socket_option_key_t<int> option_,
                      int value_,
                      const char *name_)
{
    const int rc = socket_.set (option_, value_);
    if (rc != 0 && bench_debug_enabled ()) {
        std::cerr << "setsockopt(" << (name_ ? name_ : "?")
                  << ") failed: " << zlink::last_error ().what () << std::endl;
    }
    return rc == 0;
}

void apply_single_hwm (zlink::socket_t &socket_)
{
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    (void) set_sockopt_int (
      socket_, zlink::socket_options::sndhwm, sndhwm, "sndhwm");
    (void) set_sockopt_int (
      socket_, zlink::socket_options::rcvhwm, rcvhwm, "rcvhwm");
}

void apply_single_benchmark_socket_options (zlink::socket_t &socket_,
                                            const std::string &transport_)
{
    if (transport_ == "pgm" || transport_ == "epgm")
        return;

    const int linger_ms = 0;
    const int sndtimeo_ms = resolve_single_send_timeout_ms ();
    const int rcvtimeo_ms = resolve_single_recv_timeout_ms ();
    (void) set_sockopt_int (
      socket_, zlink::socket_options::linger, linger_ms, "linger");
    (void) set_sockopt_int (
      socket_, zlink::socket_options::sndtimeo, sndtimeo_ms, "sndtimeo");
    (void) set_sockopt_int (
      socket_, zlink::socket_options::rcvtimeo, rcvtimeo_ms, "rcvtimeo");
}

std::string make_endpoint (const std::string &transport,
                           const std::string &id)
{
    if (transport == "pgm" || transport == "epgm") {
#if !defined(_WIN32)
        struct ifaddrs *ifaddr = NULL;
        if (getifaddrs (&ifaddr) == 0) {
            for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr)
                    continue;
                if (!(ifa->ifa_flags & IFF_UP))
                    continue;
                if (!(ifa->ifa_flags & IFF_MULTICAST))
                    continue;
                if (ifa->ifa_flags & IFF_LOOPBACK)
                    continue;
                if (ifa->ifa_addr->sa_family != AF_INET)
                    continue;

                char addr[INET_ADDRSTRLEN];
                const struct sockaddr_in *sa =
                  reinterpret_cast<const struct sockaddr_in *> (ifa->ifa_addr);
                if (inet_ntop (AF_INET, &sa->sin_addr, addr, sizeof (addr))) {
                    std::string endpoint =
                      transport + "://" + addr + ";239.192.1.1:5555";
                    freeifaddrs (ifaddr);
                    return endpoint;
                }
            }
            freeifaddrs (ifaddr);
        }
#endif
        return std::string ();
    }

    if (transport == "inproc")
        return std::string ("inproc://") + id;
    if (transport == "ipc")
        return "ipc://*";
    if (transport == "ws")
        return "ws://127.0.0.1:*";
    if (transport == "wss")
        return "wss://127.0.0.1:*";
    if (transport == "tls")
        return "tls://127.0.0.1:*";
    return "tcp://127.0.0.1:*";
}

std::string make_fixed_endpoint (const std::string &transport, int port)
{
    const std::string host = "127.0.0.1";
    const std::string port_str = std::to_string (port);
    if (transport == "ws")
        return "ws://" + host + ":" + port_str;
    if (transport == "wss")
        return "wss://" + host + ":" + port_str;
    if (transport == "tls")
        return "tls://" + host + ":" + port_str;
    return "tcp://" + host + ":" + port_str;
}

std::string bind_and_resolve_endpoint (zlink::socket_t &socket_,
                                       const std::string &transport,
                                       const std::string &id)
{
    std::string endpoint = make_endpoint (transport, id);
    if (endpoint.empty ())
        return std::string ();
    if (socket_.bind (endpoint) != 0)
        return std::string ();

    if (transport != "inproc") {
        std::string last_endpoint;
        if (socket_.get (zlink::socket_options::last_endpoint, last_endpoint) != 0)
            return std::string ();
        endpoint = last_endpoint;

        const std::string any_v4 = "://0.0.0.0:";
        const std::string any_v6 = "://[::]:";
        size_t pos = endpoint.find (any_v4);
        if (pos != std::string::npos) {
            endpoint.replace (pos, any_v4.size (), "://127.0.0.1:");
        } else {
            pos = endpoint.find (any_v6);
            if (pos != std::string::npos)
                endpoint.replace (pos, any_v6.size (), "://127.0.0.1:");
        }
    }

    return endpoint;
}

bool transport_available (const std::string &transport)
{
    if (transport == "pgm" || transport == "epgm")
        return false;
    if (transport == "ipc")
        return zlink::has ("ipc");
    if (transport == "tls")
        return zlink::has ("tls");
    if (transport == "ws")
        return zlink::has ("ws");
    if (transport == "wss")
        return zlink::has ("wss");
    return true;
}

void settle ()
{
    std::this_thread::sleep_for (std::chrono::milliseconds (SETTLE_TIME_MS));
}

bool connect_checked (zlink::socket_t &socket_, const std::string &endpoint)
{
    return socket_.connect (endpoint) == 0;
}

bool setup_connected_pair (zlink::socket_t &bind_socket_,
                           zlink::socket_t &connect_socket_,
                           const std::string &transport_,
                           const std::string &id_)
{
    if (!setup_tls_server (bind_socket_, transport_)
        || !setup_tls_client (connect_socket_, transport_)) {
        return false;
    }

    apply_single_hwm (bind_socket_);
    apply_single_hwm (connect_socket_);

    const std::string endpoint =
      bind_and_resolve_endpoint (bind_socket_, transport_, id_);
    if (endpoint.empty ())
        return false;
    if (!connect_checked (connect_socket_, endpoint))
        return false;

    apply_single_benchmark_socket_options (bind_socket_, transport_);
    apply_single_benchmark_socket_options (connect_socket_, transport_);
    settle ();
    return true;
}

void print_result (const std::string &lib_type,
                   const std::string &pattern,
                   const std::string &transport,
                   size_t size,
                   double throughput,
                   double latency,
                   double latency_p95,
                   double latency_p99)
{
    const double latency_ms = latency / 1000.0;
    const double latency_p95_ms = latency_p95 / 1000.0;
    const double latency_p99_ms = latency_p99 / 1000.0;
    const double bandwidth_mb_s =
      (throughput * static_cast<double> (size)) / 1000000.0;

    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",throughput," << std::fixed
              << std::setprecision (2) << throughput << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",bandwidth," << std::fixed
              << std::setprecision (2) << bandwidth_mb_s << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency," << std::fixed
              << std::setprecision (2) << latency_ms << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency_p95," << std::fixed
              << std::setprecision (2) << latency_p95_ms << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency_p99," << std::fixed
              << std::setprecision (2) << latency_p99_ms << std::endl;
}

void print_queue_metrics (const std::string &lib_type,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          const queue_stats_t &queue_stats)
{
    if (queue_stats.has_snd_pending) {
        std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
                  << "," << size << ",snd_pending_max," << std::fixed
                  << std::setprecision (2) << queue_stats.snd_pending_max
                  << std::endl;
    }

    if (queue_stats.has_rcv_pending) {
        std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
                  << "," << size << ",rcv_pending_max," << std::fixed
                  << std::setprecision (2) << queue_stats.rcv_pending_max
                  << std::endl;
        std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
                  << "," << size << ",rcv_pending_end," << std::fixed
                  << std::setprecision (2) << queue_stats.rcv_pending_end
                  << std::endl;
    }
}

void print_result (const std::string &lib_type,
                   const std::string &pattern,
                   const std::string &transport,
                   size_t size,
                   double throughput,
                   double latency,
                   double latency_p95,
                   double latency_p99,
                   const queue_stats_t &queue_stats)
{
    print_result (lib_type,
                  pattern,
                  transport,
                  size,
                  throughput,
                  latency,
                  latency_p95,
                  latency_p99);
    print_queue_metrics (lib_type, pattern, transport, size, queue_stats);
}

void print_fail_result (const std::string &lib_type,
                        const std::string &pattern,
                        const std::string &transport,
                        size_t size)
{
    (void) lib_type;
    (void) pattern;
    (void) transport;
    (void) size;
}

queue_probe_t::queue_probe_t (zlink::socket_t *send_socket_,
                              zlink::socket_t *recv_socket_)
    : _send_socket (send_socket_),
      _recv_socket (recv_socket_),
      _sample_interval_ns (resolve_sample_interval_ns ()),
      _sample_every_msgs (resolve_sample_every_msgs ()),
      _send_last_sample_ns (0),
      _recv_last_sample_ns (0),
      _send_msgs_since_sample (0),
      _recv_msgs_since_sample (0),
      _snd_pending_max (0),
      _rcv_pending_max (0),
      _rcv_pending_end (0),
      _snd_seen (false),
      _rcv_seen (false)
{
}

unsigned long long queue_probe_t::resolve_sample_interval_ns ()
{
    const int sample_ms = resolve_single_queue_sample_ms ();
    const unsigned long long clamped_ms =
      static_cast<unsigned long long> (sample_ms > 0 ? sample_ms : 100);
    return clamped_ms * 1000000ULL;
}

unsigned int queue_probe_t::resolve_sample_every_msgs ()
{
    const int value = resolve_single_queue_sample_every_msgs ();
    return static_cast<unsigned int> (value > 0 ? value : 64);
}

unsigned long long queue_probe_t::now_ns ()
{
    return static_cast<unsigned long long> (
      std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now ().time_since_epoch ())
        .count ());
}

unsigned long long queue_probe_t::peer_activity_score (
  const zlink_peer_info_t &info_)
{
    return static_cast<unsigned long long> (info_.msgs_sent)
           + static_cast<unsigned long long> (info_.msgs_received);
}

bool queue_probe_t::read_first_peer_info (zlink::socket_t *socket_,
                                          zlink_peer_info_t *info_)
{
    if (!socket_ || !info_)
        return false;

    size_t peer_count = 0;
    if (socket_->peers (NULL, &peer_count) != 0 || peer_count == 0)
        return false;

    std::vector<zlink_peer_info_t> peers (peer_count);
    size_t to_copy = peer_count;
    if (socket_->peers (&peers[0], &to_copy) != 0 || to_copy == 0)
        return false;

    size_t best = 0;
    for (size_t i = 1; i < to_copy; ++i) {
        const zlink_peer_info_t &cand = peers[i];
        const zlink_peer_info_t &cur = peers[best];
        if (cand.connected_time > cur.connected_time) {
            best = i;
            continue;
        }
        if (cand.connected_time == cur.connected_time
            && peer_activity_score (cand) > peer_activity_score (cur)) {
            best = i;
        }
    }

    *info_ = peers[best];
    return true;
}

void queue_probe_t::sample_send_if_due ()
{
    maybe_sample_send (false);
}

void queue_probe_t::sample_recv_if_due ()
{
    maybe_sample_recv (false);
}

void queue_probe_t::force_sample_send ()
{
    maybe_sample_send (true);
}

void queue_probe_t::force_sample_recv ()
{
    maybe_sample_recv (true);
}

void queue_probe_t::maybe_sample_send (bool force_)
{
    if (!_send_socket)
        return;

    if (force_) {
        _send_msgs_since_sample = 0;
    } else if (_sample_every_msgs > 1) {
        ++_send_msgs_since_sample;
        if (_send_msgs_since_sample < _sample_every_msgs)
            return;
        _send_msgs_since_sample = 0;
    }

    const unsigned long long now = now_ns ();
    if (!force_ && _send_last_sample_ns > 0
        && now - _send_last_sample_ns < _sample_interval_ns) {
        return;
    }
    _send_last_sample_ns = now;

    zlink_peer_info_t info;
    if (!read_first_peer_info (_send_socket, &info))
        return;

    const unsigned long long pending =
      static_cast<unsigned long long> (info.snd_pending_msgs);
    if (!_snd_seen || pending > _snd_pending_max)
        _snd_pending_max = pending;
    _snd_seen = true;
}

void queue_probe_t::maybe_sample_recv (bool force_)
{
    if (!_recv_socket)
        return;

    if (force_) {
        _recv_msgs_since_sample = 0;
    } else if (_sample_every_msgs > 1) {
        ++_recv_msgs_since_sample;
        if (_recv_msgs_since_sample < _sample_every_msgs)
            return;
        _recv_msgs_since_sample = 0;
    }

    const unsigned long long now = now_ns ();
    if (!force_ && _recv_last_sample_ns > 0
        && now - _recv_last_sample_ns < _sample_interval_ns) {
        return;
    }
    _recv_last_sample_ns = now;

    zlink_peer_info_t info;
    if (!read_first_peer_info (_recv_socket, &info))
        return;

    const unsigned long long pending =
      static_cast<unsigned long long> (info.rcv_pending_msgs);
    if (!_rcv_seen || pending > _rcv_pending_max)
        _rcv_pending_max = pending;
    _rcv_pending_end = pending;
    _rcv_seen = true;
}

queue_stats_t queue_probe_t::snapshot () const
{
    queue_stats_t out;
    if (_snd_seen) {
        out.has_snd_pending = true;
        out.snd_pending_max = static_cast<double> (_snd_pending_max);
    }
    if (_rcv_seen) {
        out.has_rcv_pending = true;
        out.rcv_pending_max = static_cast<double> (_rcv_pending_max);
        out.rcv_pending_end = static_cast<double> (_rcv_pending_end);
    }
    return out;
}

queue_stats_t sample_queue_stats (queue_probe_t *queue_probe_)
{
    if (!queue_probe_)
        return queue_stats_t ();
    queue_probe_->force_sample_send ();
    queue_probe_->force_sample_recv ();
    return queue_probe_->snapshot ();
}

void print_fail_result (const std::string &lib_type,
                        const std::string &pattern,
                        const std::string &transport,
                        size_t size,
                        queue_probe_t *queue_probe_)
{
    if (!queue_probe_)
        return;
    const queue_stats_t queue_stats = sample_queue_stats (queue_probe_);
    print_queue_metrics (lib_type, pattern, transport, size, queue_stats);
}

} // namespace single
} // namespace perf
