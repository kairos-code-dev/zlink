#include "perf_single_common.hpp"

#include <cerrno>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>

namespace perf {
namespace single {

namespace {

bool perf_debug_enabled_local ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

} // namespace

// latency_stats_builder_t methods removed: now a typedef to
// the unified header-only perf::latency_sampler_t in
// common/perf_latency_sampler.hpp.

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

int resolve_single_pubsub_ready_settle_ms ()
{
    return parse_positive_env ("PERF_SINGLE_PUBSUB_READY_SETTLE_MS", 1000);
}

int resolve_single_socket_hwm (bool send_)
{
    const int base_hwm = parse_positive_env ("PERF_SINGLE_HWM", 1000);
    return send_ ? parse_positive_env ("PERF_SINGLE_SNDHWM", base_hwm)
                 : parse_positive_env ("PERF_SINGLE_RCVHWM", base_hwm);
}

bool bench_debug_enabled ()
{
    static const bool enabled = std::getenv ("PERF_DEBUG") != NULL;
    return enabled;
}

void apply_ctx_options (zlink::context_t &ctx_)
{
    zlink::context_options_t options = ctx_.options ();
    const int io_threads = parse_positive_env ("PERF_IO_THREADS", 0);
    if (io_threads > 0)
        (void) options.ioThreads (io_threads);

    int max_sockets = parse_positive_env ("PERF_MAX_SOCKETS", 0);
    if (max_sockets <= 0) {
        const int clients = parse_positive_env ("PERF_CLIENTS", 0);
        if (clients > 0) {
            const long required = static_cast<long> (clients) + 4096L;
            max_sockets = required > INT_MAX ? INT_MAX : static_cast<int> (required);
        }
    }
    if (max_sockets > 0)
        (void) options.maxSockets (max_sockets);
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

    zlink::monitor_handle_t bind_monitor = zlink::monitor_handle_t::open (
      bind_socket_, zlink::monitor_event::connection_ready);
    zlink::monitor_handle_t connect_monitor = zlink::monitor_handle_t::open (
      connect_socket_, zlink::monitor_event::connection_ready);
    if (!bind_monitor.valid () || !connect_monitor.valid ())
        return false;

    const std::string endpoint =
      bind_and_resolve_endpoint (bind_socket_, transport_, id_);
    if (endpoint.empty ())
        return false;
    if (connect_socket_.connect (endpoint) != 0)
        return false;

    apply_single_benchmark_socket_options (bind_socket_, transport_);
    apply_single_benchmark_socket_options (connect_socket_, transport_);
    if (!wait_socket_monitor_event (
          bind_monitor,
          static_cast<uint64_t> (zlink::monitor_event::connection_ready),
          -1,
          10000)
        || !wait_socket_monitor_event (
          connect_monitor,
          static_cast<uint64_t> (zlink::monitor_event::connection_ready),
          -1,
          10000)) {
        return false;
    }
    return true;
}

} // namespace single
} // namespace perf
