#ifndef BINDINGS_CPP_PERF_MULTI_COMMON_HPP
#define BINDINGS_CPP_PERF_MULTI_COMMON_HPP

#include <zlink.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace perf_multi {

static const char *k_stop_token = "__zlink_perf_stop__";

inline std::string to_lower (std::string value)
{
    std::transform (
      value.begin (), value.end (), value.begin (), [](unsigned char c) {
          return static_cast<char> (std::tolower (c));
      });
    return value;
}

inline std::string to_upper (std::string value)
{
    std::transform (
      value.begin (), value.end (), value.begin (), [](unsigned char c) {
          return static_cast<char> (std::toupper (c));
      });
    return value;
}

inline int env_int (const char *name, int def)
{
    if (!name || !*name)
        return def;

    const char *v = std::getenv (name);
    if (!v)
        return def;

    const int parsed = std::atoi (v);
    return parsed > 0 ? parsed : def;
}

inline int get_port ()
{
    int s = ::socket (AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return 0;

    sockaddr_in addr;
    std::memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind (s, reinterpret_cast<sockaddr *> (&addr), sizeof (addr)) != 0) {
        ::close (s);
        return 0;
    }

    socklen_t len = sizeof (addr);
    if (::getsockname (s, reinterpret_cast<sockaddr *> (&addr), &len) != 0) {
        ::close (s);
        return 0;
    }

    const int port = ntohs (addr.sin_port);
    ::close (s);
    return port;
}

inline std::string make_multi_endpoint (const std::string &transport,
                                        const std::string &name)
{
    const int bind_port = env_int ("PERF_MULTI_SERVER_BIND_PORT", 0);
    if (bind_port > 0)
        return transport + "://127.0.0.1:" + std::to_string (bind_port);

    if (transport == "inproc") {
        return "inproc://bench-" + name + "-"
               + std::to_string (
                 std::chrono::duration_cast<std::chrono::milliseconds> (
                   std::chrono::steady_clock::now ().time_since_epoch ())
                   .count ());
    }
    if (transport == "ipc") {
        return "ipc:///tmp/zlink-bindings-cpp-multi-" + name + "-"
               + std::to_string (get_port ()) + ".sock";
    }

    return transport + "://127.0.0.1:" + std::to_string (get_port ());
}

inline bool is_stop_payload (const char *buf, int len)
{
    const int token_len = static_cast<int> (std::strlen (k_stop_token));
    return len == token_len && len > 0 && std::memcmp (buf, k_stop_token, token_len) == 0;
}

inline bool wait_monitor_ready (zlink::socket_t &monitor_socket,
                                int timeout_ms,
                                bool accept_fallback)
{
    zlink::poller_t poller;
    poller.add (monitor_socket, zlink::poll_event::pollin);

    const int effective_timeout = timeout_ms > 0 ? timeout_ms : 5000;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (effective_timeout);

    while (std::chrono::steady_clock::now () < deadline) {
        const long remain_ms = static_cast<long> (
          std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - std::chrono::steady_clock::now ())
            .count ());
        if (remain_ms <= 0)
            break;

        std::vector<zlink::poll_event_t> events;
        if (poller.wait (events, remain_ms) <= 0)
            continue;

        zlink_monitor_event_t evt;
        std::memset (&evt, 0, sizeof (evt));
        if (zlink_monitor_recv (monitor_socket.handle (), &evt, 0) != 0)
            continue;

        if (evt.event == static_cast<uint64_t> (ZLINK_EVENT_CONNECTION_READY))
            return true;

        if (accept_fallback
            && (evt.event == static_cast<uint64_t> (ZLINK_EVENT_ACCEPTED)
                || evt.event == static_cast<uint64_t> (ZLINK_EVENT_CONNECTED))) {
            return true;
        }
    }

    return false;
}

inline bool is_echo_pattern (const std::string &pattern)
{
    return pattern == "MULTI_DEALER_ROUTER"
           || pattern == "MULTI_ROUTER_ROUTER"
           || pattern == "MULTI_STREAM"
           || pattern == "MULTI_STREAM_CALLBACK"
           || pattern == "MULTI_STREAM_LEN32BE";
}

inline void print_result (const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          double throughput,
                          double latency)
{
    const double multiplier = is_echo_pattern (pattern) ? 2.0 : 1.0;
    const double bandwidth =
      (throughput * static_cast<double> (size) * multiplier) / 1000000.0;

    std::cout << "RESULT,current," << pattern << "," << transport << "," << size
              << ",throughput," << throughput << "\n";
    std::cout << "RESULT,current," << pattern << "," << transport << "," << size
              << ",bandwidth," << bandwidth << "\n";
    std::cout << "RESULT,current," << pattern << "," << transport << "," << size
              << ",latency," << latency << "\n";
}

inline void print_unsupported (const std::string &pattern,
                               const std::string &transport)
{
    std::cout << "UNSUPPORTED,cpp," << pattern << "," << transport << "\n";
}

inline int run_generic_server (const std::string &pattern,
                               const std::string &transport,
                               size_t size,
                               bool router_mode)
{
    const std::string endpoint =
      make_multi_endpoint (transport, "multi-" + to_lower (pattern));
    const int rcv_timeout_ms = env_int ("PERF_MULTI_RCVTIMEO_MS", 5000);
    const int connect_ready_timeout_ms =
      env_int ("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);

    const std::clock_t cpu_t0 = std::clock ();
    const std::chrono::steady_clock::time_point wall_t0 =
      std::chrono::steady_clock::now ();

    zlink::context_t ctx;
    std::vector<char> payload_buf (
      size > std::strlen (k_stop_token) ? size : std::strlen (k_stop_token));
    if (payload_buf.empty ())
        payload_buf.push_back ('\0');

    if (router_mode) {
        zlink::socket_t server (ctx, zlink::socket_type::router);
        if (server.set (zlink::socket_option::rcvtimeo, rcv_timeout_ms) != 0)
            return 2;

        zlink::socket_t monitor = server.monitor_open (
          zlink::monitor_event::connection_ready
          | zlink::monitor_event::accepted | zlink::monitor_event::connected);

        if (server.bind (endpoint) != 0)
            return 2;

        std::cout << "READY," << endpoint << std::endl;
        if (!wait_monitor_ready (monitor, connect_ready_timeout_ms, true))
            return 2;

        std::vector<char> rid (256);
        while (true) {
            const int rid_len = server.recv (rid.data (), rid.size ());
            if (rid_len <= 0)
                continue;

            const int n = server.recv (payload_buf.data (), payload_buf.size ());
            if (n <= 0)
                continue;

            if (is_stop_payload (payload_buf.data (), n))
                break;

            if (server.send (
                  rid.data (), static_cast<size_t> (rid_len),
                  zlink::send_flag::sndmore)
                < 0) {
                return 2;
            }
            if (server.send (payload_buf.data (), static_cast<size_t> (n)) < 0)
                return 2;
        }
    } else {
        zlink::socket_t server (ctx, zlink::socket_type::dealer);
        if (server.set (zlink::socket_option::rcvtimeo, rcv_timeout_ms) != 0)
            return 2;

        zlink::socket_t monitor = server.monitor_open (
          zlink::monitor_event::connection_ready
          | zlink::monitor_event::accepted | zlink::monitor_event::connected);

        if (server.bind (endpoint) != 0)
            return 2;

        std::cout << "READY," << endpoint << std::endl;
        if (!wait_monitor_ready (monitor, connect_ready_timeout_ms, true))
            return 2;

        while (true) {
            const int n = server.recv (payload_buf.data (), payload_buf.size ());
            if (n <= 0)
                continue;
            if (is_stop_payload (payload_buf.data (), n))
                break;
            if (server.send (payload_buf.data (), static_cast<size_t> (n)) < 0)
                return 2;
        }
    }

    const std::chrono::duration<double> wall_dt =
      std::chrono::steady_clock::now () - wall_t0;
    const double wall_sec = wall_dt.count () > 0.0 ? wall_dt.count () : 1e-9;
    const double cpu_sec = static_cast<double> (std::clock () - cpu_t0)
                           / static_cast<double> (CLOCKS_PER_SEC);
    const unsigned int hw_threads = std::thread::hardware_concurrency ();
    const double ncpu = static_cast<double> (hw_threads > 0 ? hw_threads : 1u);
    const double cpu_pct =
      (cpu_sec / (wall_sec * (ncpu > 0.0 ? ncpu : 1.0))) * 100.0;

    std::cout << "RESULT,current," << pattern << "," << transport << "," << size
              << ",server_cpu_pct," << std::fixed << std::setprecision (2)
              << cpu_pct << "\n";

    double mem_mb = 0.0;
#if !defined(__APPLE__)
    FILE *fp = std::fopen ("/proc/self/status", "r");
    if (fp) {
        char line[256];
        while (std::fgets (line, sizeof (line), fp)) {
            if (std::strncmp (line, "VmRSS:", 6) == 0) {
                long kb = 0;
                if (std::sscanf (line + 6, "%ld", &kb) == 1)
                    mem_mb = static_cast<double> (kb) / 1024.0;
                break;
            }
        }
        std::fclose (fp);
    }
#endif

    std::cout << "RESULT,current," << pattern << "," << transport << "," << size
              << ",server_mem_mb," << std::fixed << std::setprecision (2)
              << mem_mb << "\n";
    return 0;
}

inline int run_generic_client (const std::string &pattern,
                               const std::string &transport,
                               size_t size,
                               const std::string &endpoint)
{
    const int warmup_seconds = env_int ("PERF_MULTI_WARMUP_SECONDS", 3);
    const int duration_seconds = env_int ("PERF_MULTI_DURATION_SECONDS", 5);
    const int lat_count = env_int ("PERF_LAT_COUNT", 200);
    const int snd_timeout_ms = env_int ("PERF_MULTI_SNDTIMEO_MS", 5000);
    const int rcv_timeout_ms = env_int ("PERF_MULTI_RCVTIMEO_MS", 5000);
    const int connect_ready_timeout_ms =
      env_int ("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);

    zlink::context_t ctx;
    zlink::socket_t client (ctx, zlink::socket_type::dealer);
    if (client.set (zlink::socket_option::sndtimeo, snd_timeout_ms) != 0)
        return 2;
    if (client.set (zlink::socket_option::rcvtimeo, rcv_timeout_ms) != 0)
        return 2;

    zlink::socket_t monitor = client.monitor_open (
      zlink::monitor_event::connection_ready | zlink::monitor_event::connected);

    if (client.connect (endpoint) != 0)
        return 2;
    if (!wait_monitor_ready (monitor, connect_ready_timeout_ms, false))
        return 2;

    std::vector<char> payload (size > 0 ? size : 1, 'a');
    std::vector<char> recv_buf (
      size > std::strlen (k_stop_token) ? size : std::strlen (k_stop_token),
      '\0');

    const std::chrono::steady_clock::time_point warmup_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::seconds (warmup_seconds > 0 ? warmup_seconds : 0);
    while (std::chrono::steady_clock::now () < warmup_deadline) {
        if (client.send (payload.data (), payload.size ()) < 0)
            return 2;
        if (client.recv (recv_buf.data (), recv_buf.size ()) < 0)
            return 2;
    }

    const int lat_runs = lat_count > 0 ? lat_count : 1;
    const std::chrono::steady_clock::time_point lat_t0 =
      std::chrono::steady_clock::now ();
    for (int i = 0; i < lat_runs; ++i) {
        if (client.send (payload.data (), payload.size ()) < 0)
            return 2;
        if (client.recv (recv_buf.data (), recv_buf.size ()) < 0)
            return 2;
    }

    const double lat_us =
      static_cast<double> (
        std::chrono::duration_cast<std::chrono::microseconds> (
          std::chrono::steady_clock::now () - lat_t0)
          .count ())
      / static_cast<double> (lat_runs * 2);

    size_t ops = 0;
    const std::chrono::steady_clock::time_point bench_t0 =
      std::chrono::steady_clock::now ();
    const std::chrono::steady_clock::time_point bench_deadline =
      bench_t0 + std::chrono::seconds (duration_seconds > 0 ? duration_seconds : 1);

    while (std::chrono::steady_clock::now () < bench_deadline) {
        if (client.send (payload.data (), payload.size ()) < 0)
            return 2;
        if (client.recv (recv_buf.data (), recv_buf.size ()) < 0)
            return 2;
        ++ops;
    }

    const double elapsed_sec =
      static_cast<double> (
        std::chrono::duration_cast<std::chrono::nanoseconds> (
          std::chrono::steady_clock::now () - bench_t0)
          .count ())
      / 1e9;
    const double throughput =
      elapsed_sec > 0.0 ? static_cast<double> (ops) / elapsed_sec : 0.0;

    (void) client.send (k_stop_token, std::strlen (k_stop_token));
    print_result (pattern, transport, size, throughput, lat_us);
    return 0;
}

} // namespace perf_multi

#endif
