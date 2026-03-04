#include "perf_multi_common.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {

std::atomic<bool> g_stop_requested (false);
std::atomic<bool> g_callback_failed (false);
zlink::socket_t *g_server_socket = NULL;

bool is_stream_event_payload (const unsigned char *data, size_t size)
{
    return data && size == 1 && (data[0] == 0x00 || data[0] == 0x01);
}

bool is_stop_token_payload (const unsigned char *payload, size_t payload_size)
{
    if (!payload)
        return false;

    const size_t token_len = std::strlen (perf_multi::k_stop_token);
    if (payload_size == token_len
        && std::memcmp (payload, perf_multi::k_stop_token, token_len) == 0) {
        return true;
    }

    if (payload_size == token_len + 4) {
        const uint32_t declared =
          (static_cast<uint32_t> (payload[0]) << 24)
          | (static_cast<uint32_t> (payload[1]) << 16)
          | (static_cast<uint32_t> (payload[2]) << 8)
          | static_cast<uint32_t> (payload[3]);
        if (declared == token_len
            && std::memcmp (payload + 4, perf_multi::k_stop_token, token_len)
                 == 0) {
            return true;
        }
    }

    return false;
}

bool decode_stream_routing_id (const zlink_routing_id_t *rid_, uint32_t *out_)
{
    if (!rid_ || !out_ || rid_->size != 4)
        return false;

    *out_ = (static_cast<uint32_t> (rid_->data[0]) << 24)
            | (static_cast<uint32_t> (rid_->data[1]) << 16)
            | (static_cast<uint32_t> (rid_->data[2]) << 8)
            | static_cast<uint32_t> (rid_->data[3]);
    return true;
}

bool send_stream_once (const zlink_routing_id_t *rid,
                       const unsigned char *payload,
                       size_t payload_size)
{
    if (!g_server_socket || !rid || rid->size == 0)
        return false;

    uint32_t routing_id = 0;
    if (!decode_stream_routing_id (rid, &routing_id))
        return false;

    int retry_limit = perf_multi::env_int ("PERF_MULTI_STREAM_SEND_RETRIES", 128);
    if (retry_limit < 1)
        retry_limit = 1;

    for (int attempt = 0;
         attempt < retry_limit
         && !g_stop_requested.load (std::memory_order_acquire);
         ++attempt) {
        const int rc = g_server_socket->stream_send (routing_id, payload, payload_size);
        if (rc == static_cast<int> (payload_size))
            return true;

        if (rc >= 0)
            return false;

        const int err = errno;
        if (err == EINTR)
            continue;
        if (err == EAGAIN || err == ETIMEDOUT) {
            std::this_thread::yield ();
            continue;
        }
        if (err == ECONNRESET || err == EHOSTUNREACH || err == ENOTCONN
            || err == EPIPE) {
            return true;
        }
        return false;
    }

    return true;
}

double read_process_mem_mb ()
{
#if defined(__APPLE__)
    return 0.0;
#else
    double mem_mb = 0.0;
    FILE *fp = std::fopen ("/proc/self/status", "r");
    if (!fp)
        return 0.0;

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
    return mem_mb;
#endif
}

void print_server_resource_metrics (const std::string &pattern,
                                    const std::string &transport,
                                    size_t size,
                                    const std::clock_t cpu_t0,
                                    const std::chrono::steady_clock::time_point &wall_t0)
{
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

    std::cout << "RESULT,current," << pattern << "," << transport << "," << size
              << ",server_mem_mb," << std::fixed << std::setprecision (2)
              << read_process_mem_mb () << "\n";
}

int on_stream_packets_len32be (const zlink_routing_id_t *rid,
                               zlink_msg_t *msgs,
                               size_t msg_count)
{
    if (!rid || !msgs || msg_count == 0 || !g_server_socket)
        return 0;

    for (size_t i = 0; i < msg_count; ++i) {
        zlink_msg_t *msg = &msgs[i];
        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t payload_size = zlink_msg_size (msg);
        if (is_stream_event_payload (payload, payload_size)) {
            (void) zlink_msg_close (msg);
            continue;
        }

        if (is_stop_token_payload (payload, payload_size)) {
            g_stop_requested.store (true, std::memory_order_release);
            (void) zlink_msg_close (msg);
            continue;
        }

        if (!send_stream_once (rid, payload, payload_size)) {
            g_callback_failed.store (true, std::memory_order_release);
            (void) zlink_msg_close (msg);
            for (size_t j = i + 1; j < msg_count; ++j)
                (void) zlink_msg_close (&msgs[j]);
            return 1;
        }

        (void) zlink_msg_close (msg);
    }

    return 0;
}

int run_stream_len32be_server (const std::string &pattern,
                               const std::string &transport,
                               size_t size)
{
    const int snd_timeout_ms = perf_multi::env_int ("PERF_MULTI_SNDTIMEO_MS", 5000);
    const int rcv_timeout_ms = perf_multi::env_int ("PERF_MULTI_RCVTIMEO_MS", 5000);
    const int connect_ready_timeout_ms =
      perf_multi::env_int ("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000);
    const int hwm = perf_multi::env_int ("PERF_MULTI_HWM", 100000);

    const std::clock_t cpu_t0 = std::clock ();
    const std::chrono::steady_clock::time_point wall_t0 =
      std::chrono::steady_clock::now ();

    zlink::context_t ctx;
    perf_multi::apply_ctx_options (ctx);
    zlink::socket_t server (ctx, zlink::socket_type::stream);
    if (server.set (zlink::socket_option::sndtimeo, snd_timeout_ms) != 0)
        return 2;
    if (server.set (zlink::socket_option::rcvtimeo, rcv_timeout_ms) != 0)
        return 2;
    if (server.set (zlink::socket_option::sndhwm, hwm) != 0)
        return 2;
    if (server.set (zlink::socket_option::rcvhwm, hwm) != 0)
        return 2;

    zlink::socket_t monitor = server.monitor_open (
      zlink::monitor_event::connection_ready
      | zlink::monitor_event::accepted | zlink::monitor_event::connected);

    const std::string endpoint =
      perf_multi::make_multi_endpoint (transport, "multi-stream-len32be-server");
    if (server.bind (endpoint) != 0)
        return 2;

    g_stop_requested.store (false, std::memory_order_release);
    g_callback_failed.store (false, std::memory_order_release);
    g_server_socket = &server;

    if (g_server_socket->stream_attach (&on_stream_packets_len32be,
                                        zlink::stream_dispatch_mode::len32be)
        != 0) {
        g_server_socket = NULL;
        return 2;
    }

    std::cout << "READY," << endpoint << std::endl;
    if (!perf_multi::wait_monitor_ready (monitor, connect_ready_timeout_ms, true)) {
        (void) server.stream_detach ();
        g_server_socket = NULL;
        return 2;
    }

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        if (g_callback_failed.load (std::memory_order_acquire)) {
            (void) server.stream_detach ();
            g_server_socket = NULL;
            return 2;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    (void) server.stream_detach ();
    g_server_socket = NULL;
    print_server_resource_metrics (pattern, transport, size, cpu_t0, wall_t0);
    return 0;
}

} // namespace

int perf_multi_stream_len32be_server (const std::string &transport, size_t size)
{
    return run_stream_len32be_server ("MULTI_STREAM_LEN32BE", transport, size);
}
