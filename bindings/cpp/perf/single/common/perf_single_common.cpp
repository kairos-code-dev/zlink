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

bool set_sockopt_int (perf_socket_t &socket_,
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

void apply_single_hwm (perf_socket_t &socket_)
{
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    (void) set_sockopt_int (
      socket_, zlink::socket_options::sndhwm, sndhwm, "sndhwm");
    (void) set_sockopt_int (
      socket_, zlink::socket_options::rcvhwm, rcvhwm, "rcvhwm");
}

void apply_single_benchmark_socket_options (perf_socket_t &socket_,
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

std::string bind_and_resolve_endpoint (perf_socket_t &socket_,
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

bool connect_checked (perf_socket_t &socket_, const std::string &endpoint)
{
    return socket_.connect (endpoint) == 0;
}

bool setup_connected_pair (perf_socket_t &bind_socket_,
                           perf_socket_t &connect_socket_,
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

bool wait_socket_monitor_event (zlink::monitor_handle_t &monitor_,
                                uint64_t event_type_,
                                int64_t value_,
                                int timeout_ms_)
{
    for (;;) {
        const zlink::maybe_t<zlink_socket_monitor_event_t> event =
          monitor_.try_recv();
        if (!event)
            break;
        if (event->event != event_type_)
            continue;
        if (value_ >= 0 && static_cast<int64_t> (event->value) != value_)
            continue;
        return true;
    }

    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    if (poller.add (monitor_, zlink::poll_event::pollin, NULL) != 0)
        return false;

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        const auto remaining = deadline - std::chrono::steady_clock::now ();
        long wait_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                         remaining)
                         .count ();
        if (wait_ms < 1)
            wait_ms = 1;
        const int rc = poller.wait_all (events, wait_ms);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (rc == 0)
            continue;

        for (;;) {
            const zlink::maybe_t<zlink_socket_monitor_event_t> event =
              monitor_.try_recv();
            if (!event)
                break;
            if (event->event != event_type_)
                continue;
            if (value_ >= 0 && static_cast<int64_t> (event->value) != value_)
                continue;
            return true;
        }
    }
    return false;
}

bool wait_service_monitor_event (zlink::service_monitor_handle_t &monitor_,
                                 uint32_t event_type_,
                                 int64_t value_,
                                 int timeout_ms_)
{
    for (;;) {
        const zlink::maybe_t<zlink_service_monitor_event_t> event =
          monitor_.try_recv();
        if (!event)
            break;
        if (event->event_type != event_type_)
            continue;
        if (value_ >= 0 && static_cast<int64_t> (event->value) != value_)
            continue;
        return true;
    }

    zlink::poller_t poller;
    std::vector<zlink::poll_event_t> events;
    events.reserve (1);
    if (poller.add (monitor_, zlink::poll_event::pollin, NULL) != 0)
        return false;

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms_);
    while (std::chrono::steady_clock::now () < deadline) {
        const auto remaining = deadline - std::chrono::steady_clock::now ();
        long wait_ms = std::chrono::duration_cast<std::chrono::milliseconds> (
                         remaining)
                         .count ();
        if (wait_ms < 1)
            wait_ms = 1;
        const int rc = poller.wait_all (events, wait_ms);
        if (rc < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (rc == 0)
            continue;

        for (;;) {
            const zlink::maybe_t<zlink_service_monitor_event_t> event =
              monitor_.try_recv();
            if (!event)
                break;
            if (event->event_type != event_type_)
                continue;
            if (value_ >= 0 && static_cast<int64_t> (event->value) != value_)
                continue;
            return true;
        }
    }
    return false;
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
    std::cout << "FAIL," << lib_type << "," << pattern << "," << transport
              << "," << size << std::endl;
}

queue_probe_t::queue_probe_t (perf_socket_t *send_socket_,
                              perf_socket_t *recv_socket_)
    : _send_socket (send_socket_),
      _recv_socket (recv_socket_),
      _send_monitor (),
      _recv_monitor (),
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
    if (_send_socket)
        _send_monitor = zlink::monitor_handle_t (*_send_socket);
    if (_recv_socket)
        _recv_monitor = zlink::monitor_handle_t (*_recv_socket);
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

bool queue_probe_t::read_snapshot (zlink::monitor_handle_t *monitor_,
                                   zlink_monitor_snapshot_t *snapshot_)
{
    if (!monitor_ || !snapshot_ || !monitor_->valid ())
        return false;
    return monitor_->snapshot (*snapshot_) == 0;
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

    zlink_monitor_snapshot_t snapshot;
    if (!read_snapshot (&_send_monitor, &snapshot))
        return;
    if ((snapshot.detail_flags & ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS)
        == 0) {
        return;
    }

    const unsigned long long pending =
      static_cast<unsigned long long> (snapshot.snd_pending_msgs);
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

    zlink_monitor_snapshot_t snapshot;
    if (!read_snapshot (&_recv_monitor, &snapshot))
        return;
    if ((snapshot.detail_flags & ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS)
        == 0) {
        return;
    }

    const unsigned long long pending =
      static_cast<unsigned long long> (snapshot.rcv_pending_msgs);
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

callback_receiver_t::callback_receiver_t ()
    : _socket (NULL),
      _queue_probe (NULL),
      _queue (
        static_cast<size_t> (parse_positive_env (
          "PERF_SINGLE_CALLBACK_QUEUE_CAP", 262144))),
      _queue_head (0),
      _queue_tail (0),
      _queue_count (0),
      _stop_worker (false),
      _failed (false),
      _worker (),
      _current_token (0),
      _current_run_id (0),
      _current_msg_size (0),
      _current_phase (static_cast<int> (perf_single_metric::phase_warmup)),
      _current_active (false),
      _result_token (0),
      _received_count (0),
      _latency_builder (resolve_single_latency_sample_cap ())
{
    if (_queue.empty ())
        _queue.resize (1);
}

callback_receiver_t::~callback_receiver_t ()
{
    {
        std::lock_guard<std::mutex> lock (_queue_mutex);
        _stop_worker = true;
    }
    _queue_cv.notify_all ();
    if (_worker.joinable ())
        _worker.join ();
}

bool callback_receiver_t::attach (perf_socket_t &socket_,
                                  queue_probe_t *queue_probe_)
{
    _socket = &socket_;
    _queue_probe = queue_probe_;
    if (_socket->on_receive(&callback_receiver_t::recv_handler, this) != 0)
        return false;
    if (_worker.joinable ())
        return true;

    try {
        _worker = std::thread (&callback_receiver_t::worker_loop, this);
    } catch (...) {
        _failed.store (true, std::memory_order_release);
        return false;
    }
    return true;
}

bool callback_receiver_t::begin_phase (uint32_t run_id_,
                                       perf_single_metric::phase_t phase_,
                                       size_t msg_size_,
                                       bool active_)
{
    if (!_socket || !_worker.joinable ())
        return false;

    const unsigned long long token =
      _current_token.fetch_add (1, std::memory_order_acq_rel) + 1ULL;
    _current_run_id.store (run_id_, std::memory_order_release);
    _current_msg_size.store (msg_size_, std::memory_order_release);
    _current_phase.store (static_cast<int> (phase_), std::memory_order_release);
    _current_active.store (active_, std::memory_order_release);

    std::lock_guard<std::mutex> lock (_result_mutex);
    _result_token = token;
    _received_count = 0;
    _latency_builder = latency_stats_builder_t (resolve_single_latency_sample_cap ());
    return true;
}

bool callback_receiver_t::finish_phase (unsigned long long expected_count_,
                                        int recv_timeout_ms_,
                                        unsigned long long *received_out_,
                                        latency_stats_t *latency_out_)
{
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (recv_timeout_ms_ > 0 ? recv_timeout_ms_ : 200);

    std::unique_lock<std::mutex> lock (_result_mutex);
    while (_received_count < expected_count_
           && !_failed.load (std::memory_order_acquire)) {
        if (_result_cv.wait_until (lock, deadline) == std::cv_status::timeout)
            break;
    }

    if (_received_count < expected_count_
        || _failed.load (std::memory_order_acquire)) {
        return false;
    }

    if (received_out_)
        *received_out_ = _received_count;
    if (latency_out_)
        *latency_out_ = _latency_builder.snapshot ();
    return true;
}

bool callback_receiver_t::failed () const
{
    return _failed.load (std::memory_order_acquire);
}

void callback_receiver_t::recv_handler (const zlink_routing_id_t *,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        void *userdata_)
{
    callback_receiver_t *self =
      static_cast<callback_receiver_t *> (userdata_);
    if (!self || !parts_ || part_count_ == 0) {
        if (parts_)
            zlink::detail::close_message_array (parts_, part_count_);
        return;
    }

    event_t event;
    event.token = self->_current_token.load (std::memory_order_acquire);
    event.run_id = self->_current_run_id.load (std::memory_order_acquire);
    event.msg_size = self->_current_msg_size.load (std::memory_order_acquire);
    event.phase = static_cast<perf_single_metric::phase_t> (
      self->_current_phase.load (std::memory_order_acquire));
    event.active = self->_current_active.load (std::memory_order_acquire);

    if (part_count_ == 1) {
        zlink::message_t part;
        if (part.adopt (&parts_[0]) == 0) {
            event.header_ok = perf_single_metric::decode_payload_header (
              part.data (), part.size (), &event.header);
        }
    }

    if (event.active && self->_queue_probe)
        self->_queue_probe->sample_recv_if_due ();

    if (!self->push_event (event))
        self->_failed.store (true, std::memory_order_release);

    zlink::detail::close_message_array (parts_, part_count_);
}

bool callback_receiver_t::push_event (const event_t &event_)
{
    std::lock_guard<std::mutex> lock (_queue_mutex);
    if (_stop_worker || _queue_count >= _queue.size ())
        return false;

    _queue[_queue_tail] = event_;
    _queue_tail = (_queue_tail + 1) % _queue.size ();
    ++_queue_count;
    _queue_cv.notify_one ();
    return true;
}

void callback_receiver_t::worker_loop ()
{
    for (;;) {
        event_t event;
        {
            std::unique_lock<std::mutex> lock (_queue_mutex);
            while (_queue_count == 0 && !_stop_worker)
                _queue_cv.wait (lock);
            if (_queue_count == 0 && _stop_worker)
                break;

            event = _queue[_queue_head];
            _queue_head = (_queue_head + 1) % _queue.size ();
            --_queue_count;
        }

        std::lock_guard<std::mutex> lock (_result_mutex);
        if (event.token != _result_token)
            continue;
        if (!event.header_ok
            || !perf_single_metric::is_expected (
              event.header, event.run_id, event.phase, event.msg_size)) {
            continue;
        }

        ++_received_count;
        if (event.active) {
            const uint64_t now = perf_single_metric::now_us ();
            const double latency_us =
              now >= event.header.sent_ts_us
                ? static_cast<double> (now - event.header.sent_ts_us)
                : 0.0;
            _latency_builder.add (latency_us);
        }
        _result_cv.notify_all ();
    }
}

bool run_callback_phase (callback_receiver_t &receiver_,
                         phase_send_fn_t send_fn_,
                         void *send_userdata_,
                         std::vector<char> &payload_,
                         size_t msg_size_,
                         uint32_t run_id_,
                         uint64_t &seq_,
                         perf_single_metric::phase_t phase_,
                         int warmup_count_,
                         int duration_s_,
                         int recv_timeout_ms_,
                         unsigned long long *received_out_,
                         latency_stats_t *latency_out_)
{
    if (!send_fn_ || !receiver_.begin_phase (
                       run_id_, phase_, msg_size_,
                       phase_ == perf_single_metric::phase_active)) {
        return false;
    }

    const bool active = phase_ == perf_single_metric::phase_active;
    unsigned long long sent_count = 0;
    bool send_failed = false;

    if (active) {
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::seconds (duration_s_ > 0 ? duration_s_ : 1);
        while (std::chrono::steady_clock::now () < deadline) {
            if (!perf_single_metric::stamp_payload (payload_.data (),
                                                    payload_.size (),
                                                    run_id_,
                                                    phase_,
                                                    msg_size_,
                                                    seq_++,
                                                    perf_single_metric::now_us ())
                || !send_fn_ (
                  send_userdata_, payload_.data (), payload_.size ())) {
                send_failed = true;
                break;
            }
            ++sent_count;
        }
    } else {
        for (int i = 0; i < warmup_count_; ++i) {
            if (!perf_single_metric::stamp_payload (payload_.data (),
                                                    payload_.size (),
                                                    run_id_,
                                                    phase_,
                                                    msg_size_,
                                                    seq_++,
                                                    perf_single_metric::now_us ())
                || !send_fn_ (
                  send_userdata_, payload_.data (), payload_.size ())) {
                send_failed = true;
                break;
            }
            ++sent_count;
        }
    }

    if (send_failed || sent_count == 0)
        return false;

    if (!receiver_.finish_phase (
          sent_count, recv_timeout_ms_, received_out_, latency_out_)) {
        return false;
    }

    if (active && latency_out_ && latency_out_->mean_us <= 0.0)
        return false;
    return !receiver_.failed ();
}

subscribe_callback_receiver_t::subscribe_callback_receiver_t ()
    : _queue_probe (NULL),
      _queue (
        static_cast<size_t> (parse_positive_env (
          "PERF_SINGLE_CALLBACK_QUEUE_CAP", 262144))),
      _queue_head (0),
      _queue_tail (0),
      _queue_count (0),
      _stop_worker (false),
      _failed (false),
      _worker (),
      _current_token (0),
      _current_run_id (0),
      _current_msg_size (0),
      _current_phase (static_cast<int> (perf_single_metric::phase_warmup)),
      _current_active (false),
      _expected_topic (),
      _result_token (0),
      _received_count (0),
      _latency_builder (resolve_single_latency_sample_cap ())
{
    if (_queue.empty ())
        _queue.resize (1);
}

subscribe_callback_receiver_t::~subscribe_callback_receiver_t ()
{
    {
        std::lock_guard<std::mutex> lock (_queue_mutex);
        _stop_worker = true;
    }
    _queue_cv.notify_all ();
    if (_worker.joinable ())
        _worker.join ();
}

bool subscribe_callback_receiver_t::attach_socket (perf_socket_t &socket_,
                                                   queue_probe_t *queue_probe_)
{
    _queue_probe = queue_probe_;
    if (socket_.on_subscribe(
          &subscribe_callback_receiver_t::subscribe_handler, this)
        != 0) {
        return false;
    }

    if (_worker.joinable ())
        return true;

    try {
        _worker = std::thread (&subscribe_callback_receiver_t::worker_loop, this);
    } catch (...) {
        _failed.store (true, std::memory_order_release);
        return false;
    }
    return true;
}

bool subscribe_callback_receiver_t::attach_spot (zlink::service::spot_t &spot_,
                                                 queue_probe_t *queue_probe_)
{
    _queue_probe = queue_probe_;
    if (spot_.on_subscribe(
          &subscribe_callback_receiver_t::subscribe_handler, this)
        != 0) {
        return false;
    }

    if (_worker.joinable ())
        return true;

    try {
        _worker = std::thread (&subscribe_callback_receiver_t::worker_loop, this);
    } catch (...) {
        _failed.store (true, std::memory_order_release);
        return false;
    }
    return true;
}

bool subscribe_callback_receiver_t::begin_phase (uint32_t run_id_,
                                                 perf_single_metric::phase_t phase_,
                                                 size_t msg_size_,
                                                 bool active_,
                                                 const std::string &topic_)
{
    if (!_worker.joinable ())
        return false;

    const unsigned long long token =
      _current_token.fetch_add (1, std::memory_order_acq_rel) + 1ULL;
    _current_run_id.store (run_id_, std::memory_order_release);
    _current_msg_size.store (msg_size_, std::memory_order_release);
    _current_phase.store (static_cast<int> (phase_), std::memory_order_release);
    _current_active.store (active_, std::memory_order_release);

    std::lock_guard<std::mutex> lock (_result_mutex);
    _expected_topic = topic_;
    _result_token = token;
    _received_count = 0;
    _latency_builder = latency_stats_builder_t (resolve_single_latency_sample_cap ());
    return true;
}

bool subscribe_callback_receiver_t::finish_phase (unsigned long long expected_count_,
                                                  int recv_timeout_ms_,
                                                  unsigned long long *received_out_,
                                                  latency_stats_t *latency_out_)
{
    std::unique_lock<std::mutex> lock (_result_mutex);
    const auto wait_span =
      std::chrono::milliseconds (recv_timeout_ms_ > 0 ? recv_timeout_ms_ : 200);

    if (expected_count_ == 0) {
        unsigned long long last_count = _received_count;
        auto deadline = std::chrono::steady_clock::now () + wait_span;

        while (!_failed.load (std::memory_order_acquire)) {
            if (_received_count != last_count) {
                last_count = _received_count;
                deadline = std::chrono::steady_clock::now () + wait_span;
                continue;
            }
            if (std::chrono::steady_clock::now () >= deadline)
                break;
            (void) _result_cv.wait_until (lock, deadline);
        }
    } else {
        const auto deadline = std::chrono::steady_clock::now () + wait_span;
        while (_received_count < expected_count_
               && !_failed.load (std::memory_order_acquire)) {
            if (_result_cv.wait_until (lock, deadline) == std::cv_status::timeout)
                break;
        }
        if (_received_count < expected_count_)
            return false;
    }

    if (_failed.load (std::memory_order_acquire) || _received_count == 0) {
        return false;
    }

    if (received_out_)
        *received_out_ = _received_count;
    if (latency_out_)
        *latency_out_ = _latency_builder.snapshot ();
    return true;
}

bool subscribe_callback_receiver_t::failed () const
{
    return _failed.load (std::memory_order_acquire);
}

void subscribe_callback_receiver_t::subscribe_handler (
  const zlink_routing_id_t *,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    subscribe_callback_receiver_t *self =
      static_cast<subscribe_callback_receiver_t *> (userdata_);
    if (!self || !parts_ || part_count_ == 0) {
        if (parts_)
            zlink::detail::close_message_array (parts_, part_count_);
        return;
    }

    event_t event;
    event.token = self->_current_token.load (std::memory_order_acquire);
    event.run_id = self->_current_run_id.load (std::memory_order_acquire);
    event.msg_size = self->_current_msg_size.load (std::memory_order_acquire);
    event.phase = static_cast<perf_single_metric::phase_t> (
      self->_current_phase.load (std::memory_order_acquire));
    event.active = self->_current_active.load (std::memory_order_acquire);
    if (topic_ && topic_len_ > 0)
        event.topic.assign (topic_, topic_len_);

    if (part_count_ == 1) {
        zlink::message_t part;
        if (part.adopt (&parts_[0]) == 0) {
            event.header_ok = perf_single_metric::decode_payload_header (
              part.data (), part.size (), &event.header);
        }
    }

    if (event.active && self->_queue_probe)
        self->_queue_probe->sample_recv_if_due ();

    if (!self->push_event (event))
        self->_failed.store (true, std::memory_order_release);

    zlink::detail::close_message_array (parts_, part_count_);
}

bool subscribe_callback_receiver_t::push_event (const event_t &event_)
{
    std::lock_guard<std::mutex> lock (_queue_mutex);
    if (_stop_worker || _queue_count >= _queue.size ())
        return false;

    _queue[_queue_tail] = event_;
    _queue_tail = (_queue_tail + 1) % _queue.size ();
    ++_queue_count;
    _queue_cv.notify_one ();
    return true;
}

void subscribe_callback_receiver_t::worker_loop ()
{
    for (;;) {
        event_t event;
        {
            std::unique_lock<std::mutex> lock (_queue_mutex);
            while (_queue_count == 0 && !_stop_worker)
                _queue_cv.wait (lock);
            if (_queue_count == 0 && _stop_worker)
                break;

            event = _queue[_queue_head];
            _queue_head = (_queue_head + 1) % _queue.size ();
            --_queue_count;
        }

        std::lock_guard<std::mutex> lock (_result_mutex);
        if (event.token != _result_token || event.topic != _expected_topic)
            continue;
        if (!event.header_ok
            || !perf_single_metric::is_expected (
              event.header, event.run_id, event.phase, event.msg_size)) {
            continue;
        }

        ++_received_count;
        if (event.active) {
            const uint64_t now = perf_single_metric::now_us ();
            const double latency_us =
              now >= event.header.sent_ts_us
                ? static_cast<double> (now - event.header.sent_ts_us)
                : 0.0;
            _latency_builder.add (latency_us);
        }
        _result_cv.notify_all ();
    }
}

bool run_subscribe_callback_phase (subscribe_callback_receiver_t &receiver_,
                                   phase_send_fn_t send_fn_,
                                   void *send_userdata_,
                                   std::vector<char> &payload_,
                                   size_t msg_size_,
                                   uint32_t run_id_,
                                   uint64_t &seq_,
                                   perf_single_metric::phase_t phase_,
                                   int warmup_count_,
                                   int duration_s_,
                                   int recv_timeout_ms_,
                                   const std::string &topic_,
                                   unsigned long long *received_out_,
                                   latency_stats_t *latency_out_)
{
    const bool debug_enabled = std::getenv ("PERF_DEBUG") != NULL;
    if (!send_fn_
        || !receiver_.begin_phase (run_id_, phase_, msg_size_,
                                   phase_ == perf_single_metric::phase_active,
                                   topic_)) {
        if (debug_enabled)
            std::cerr << "subscribe_phase: begin_phase failed" << std::endl;
        return false;
    }

    const bool active = phase_ == perf_single_metric::phase_active;
    unsigned long long sent_count = 0;
    bool send_failed = false;

    if (active) {
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::seconds (duration_s_ > 0 ? duration_s_ : 1);
        while (std::chrono::steady_clock::now () < deadline) {
            if (!perf_single_metric::stamp_payload (payload_.data (),
                                                    payload_.size (),
                                                    run_id_,
                                                    phase_,
                                                    msg_size_,
                                                    seq_++,
                                                    perf_single_metric::now_us ())
                || !send_fn_ (
                  send_userdata_, payload_.data (), payload_.size ())) {
                send_failed = true;
                break;
            }
            ++sent_count;
        }
    } else {
        for (int i = 0; i < warmup_count_; ++i) {
            if (!perf_single_metric::stamp_payload (payload_.data (),
                                                    payload_.size (),
                                                    run_id_,
                                                    phase_,
                                                    msg_size_,
                                                    seq_++,
                                                    perf_single_metric::now_us ())
                || !send_fn_ (
                  send_userdata_, payload_.data (), payload_.size ())) {
                send_failed = true;
                break;
            }
            ++sent_count;
        }
    }

    if (send_failed || sent_count == 0)
    {
        if (debug_enabled)
            std::cerr << "subscribe_phase: send failed sent_count=" << sent_count
                      << std::endl;
        return false;
    }

    if (!receiver_.finish_phase (
          active ? 0ULL : sent_count, recv_timeout_ms_, received_out_, latency_out_)) {
        if (debug_enabled)
            std::cerr << "subscribe_phase: finish failed sent_count=" << sent_count
                      << " receiver_failed=" << receiver_.failed () << std::endl;
        return false;
    }

    if (active && latency_out_ && latency_out_->mean_us <= 0.0) {
        if (debug_enabled)
            std::cerr << "subscribe_phase: latency snapshot empty" << std::endl;
        return false;
    }
    return !receiver_.failed ();
}

} // namespace single
} // namespace perf
