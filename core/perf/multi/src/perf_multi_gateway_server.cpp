#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "GATEWAY";
static const char *k_service_name = "perf-gateway";
static const char *k_server_routing_id = "perf-gateway-server";

static std::atomic<bool> g_stop_requested(false);
static std::atomic<bool> g_queue_probe_pending(false);
static std::atomic<size_t> g_queue_probe_size(0);

struct pending_gateway_echo_t
{
    pending_gateway_echo_t()
    {
        std::memset(&routing_id, 0, sizeof(routing_id));
    }

    zlink_routing_id_t routing_id;
    std::vector<char> payload;
};

struct gateway_server_state_t
{
    gateway_server_state_t() :
        gateway(NULL),
        recv_count(0),
        send_ok_count(0),
        send_failures(0)
    {
    }

    void *gateway;
    std::mutex mutex;
    std::deque<pending_gateway_echo_t> pending;
    std::atomic<unsigned long long> recv_count;
    std::atomic<unsigned long long> send_ok_count;
    std::atomic<int> send_failures;
};

gateway_server_state_t *g_server_state = NULL;

void close_parts(zlink_msg_t *parts, size_t part_count)
{
    if (!parts)
        return;
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close(&parts[i]);
}

bool is_supported_transport(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

void on_signal(int)
{
    g_stop_requested.store(true, std::memory_order_release);
}

void install_signal_handlers()
{
    std::signal(SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal(SIGTERM, on_signal);
#endif
}

void request_queue_probe(size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store(msg_size, std::memory_order_release);
    g_queue_probe_pending.store(true, std::memory_order_release);
}

std::string replace_any_host_with_localhost(const std::string &endpoint)
{
    std::string normalized = endpoint;
    const std::string any_v4 = "://0.0.0.0:";
    const std::string any_v6 = "://[::]:";
    size_t pos = normalized.find(any_v4);
    if (pos != std::string::npos)
        normalized.replace(pos, any_v4.size(), "://127.0.0.1:");
    pos = normalized.find(any_v6);
    if (pos != std::string::npos)
        normalized.replace(pos, any_v6.size(), "://127.0.0.1:");
    return normalized;
}

bool configure_gateway_tls_server(void *gateway, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    static const std::string cert_path =
      write_temp_cert(test_certs::server_cert_pem, "multi_gateway_srv_cert");
    static const std::string key_path =
      write_temp_cert(test_certs::server_key_pem, "multi_gateway_srv_key");
    return zlink_gateway_set_tls_server(gateway, cert_path.c_str(),
                                        key_path.c_str())
           == 0;
}

bool apply_gateway_options(void *gateway,
                           const multi_bench_settings_t &settings)
{
    const int linger_ms = 0;
    const int sndhwm = bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm);
    const int rcvhwm = bench_hwm_from_env("PERF_MULTI_RCVHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200);

    return zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_LINGER,
                                    &linger_ms, sizeof(linger_ms))
             == 0
           && zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_SNDHWM,
                                       &sndhwm, sizeof(sndhwm))
                == 0
           && zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_RCVHWM,
                                       &rcvhwm, sizeof(rcvhwm))
                == 0
           && zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_SNDTIMEO,
                                       &sndtimeo_ms, sizeof(sndtimeo_ms))
                == 0;
}

std::string bind_gateway_endpoint(void *gateway,
                                  const std::string &transport,
                                  const std::string &token)
{
    const int bind_port =
      resolve_multi_int_env("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    std::string endpoint = bind_port > 0
                             ? make_fixed_endpoint(transport, bind_port)
                             : make_endpoint(transport, token);
    if (endpoint.empty())
        return std::string();

    if (zlink_gateway_bind(gateway, endpoint.c_str()) != 0) {
        std::cerr << "gateway bind failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return std::string();
    }

    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t last_endpoint_size = sizeof(last_endpoint);
    if (zlink_gateway_last_endpoint(gateway, last_endpoint, &last_endpoint_size)
        == 0) {
        endpoint.assign(last_endpoint);
    }

    return replace_any_host_with_localhost(endpoint);
}

bool wait_for_gateway_peers(void *gateway, size_t target_count, int timeout_ms)
{
    const int expected = static_cast<int>(std::max<size_t>(1, target_count));
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        if (zlink_gateway_connection_count(gateway) >= expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return zlink_gateway_connection_count(gateway) >= expected;
}

unsigned long long gateway_peer_score(const zlink_gateway_peer_info_t &info)
{
    return static_cast<unsigned long long>(info.msgs_sent)
           + static_cast<unsigned long long>(info.msgs_received);
}

server_queue_stats_t sample_gateway_queue_stats(void *gateway,
                                                size_t pending_depth)
{
    server_queue_stats_t stats;
    if (!gateway)
        return stats;

    size_t peer_count = 0;
    if (zlink_gateway_router_peers(gateway, NULL, &peer_count) != 0
        || peer_count == 0) {
        stats.snd_pending_max = static_cast<double>(pending_depth);
        return stats;
    }

    std::vector<zlink_gateway_peer_info_t> peers(peer_count);
    size_t filled = peer_count;
    if (zlink_gateway_router_peers(gateway, &peers[0], &filled) != 0
        || filled == 0) {
        stats.snd_pending_max = static_cast<double>(pending_depth);
        return stats;
    }

    size_t best = 0;
    for (size_t i = 1; i < filled; ++i) {
        if (peers[i].connected_time > peers[best].connected_time
            || (peers[i].connected_time == peers[best].connected_time
                && gateway_peer_score(peers[i])
                     > gateway_peer_score(peers[best]))) {
            best = i;
        }
    }

    stats.snd_pending_max = static_cast<double>(
      std::max<unsigned long long>(
        peers[best].snd_pending_msgs,
        static_cast<unsigned long long>(pending_depth)));
    stats.rcv_pending_max =
      static_cast<double>(peers[best].rcv_pending_msgs);
    stats.rcv_pending_end =
      static_cast<double>(peers[best].rcv_pending_msgs);
    return stats;
}

void emit_requested_queue_probe(const std::string &lib_name,
                                const std::string &transport)
{
    if (!g_queue_probe_pending.exchange(false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load(std::memory_order_acquire);
    gateway_server_state_t *state = g_server_state;
    if (msg_size == 0 || !state || !state->gateway)
        return;

    size_t pending_depth = 0;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        pending_depth = state->pending.size();
    }

    const server_queue_stats_t queue_stats =
      sample_gateway_queue_stats(state->gateway, pending_depth);
    print_server_queue_metrics(lib_name, k_pattern, transport, msg_size,
                               queue_stats);
}

enum send_status_t
{
    send_status_ok = 0,
    send_status_blocked = 1,
    send_status_fatal = 2
};

bool move_part(zlink_msg_t *dest, zlink_msg_t *src)
{
    if (!dest || !src)
        return false;

    if (zlink_msg_init(dest) != 0)
        return false;

    if (zlink_msg_move(dest, src) == 0)
        return true;

    (void) zlink_msg_close(dest);
    return false;
}

send_status_t try_send_gateway_reply(void *gateway,
                                     const zlink_routing_id_t *routing_id,
                                     const void *data,
                                     size_t size)
{
    zlink_msg_t part;
    if (zlink_msg_init_data(
          &part,
          size > 0 ? const_cast<void *>(data) : NULL,
          size,
          NULL,
          NULL)
        != 0) {
        return send_status_fatal;
    }

    const int rc =
      zlink_gateway_send_rid(gateway, routing_id, &part, 1, ZLINK_DONTWAIT);
    if (rc == 0)
        return send_status_ok;
    const int saved_errno = errno;
    (void) zlink_msg_close(&part);
    if (saved_errno == EAGAIN || saved_errno == EHOSTUNREACH
        || saved_errno == ENOTCONN) {
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
}

void enqueue_pending_reply(gateway_server_state_t *state,
                           const zlink_routing_id_t *routing_id,
                           const void *data,
                           size_t size)
{
    if (!state || !routing_id)
        return;

    pending_gateway_echo_t entry;
    entry.routing_id = *routing_id;
    entry.payload.assign(static_cast<const char *>(data),
                         static_cast<const char *>(data) + size);

    std::lock_guard<std::mutex> lock(state->mutex);
    state->pending.push_back(entry);
}

void gateway_server_handler(const zlink_routing_id_t *source_rid,
                            zlink_msg_t *parts,
                            size_t part_count)
{
    gateway_server_state_t *state = g_server_state;
    if (!state || !state->gateway || !source_rid || part_count == 0) {
        close_parts(parts, part_count);
        return;
    }
    if (part_count != 1) {
        state->send_failures.fetch_add(1, std::memory_order_acq_rel);
        close_parts(parts, part_count);
        return;
    }

    const void *payload_data = zlink_msg_data(&parts[0]);
    const size_t payload_size = zlink_msg_size(&parts[0]);

    bool has_pending = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        has_pending = !state->pending.empty();
    }
    if (has_pending) {
        state->recv_count.fetch_add(1, std::memory_order_acq_rel);
        enqueue_pending_reply(state, source_rid, payload_data, payload_size);
        close_parts(parts, part_count);
        return;
    }

    state->recv_count.fetch_add(1, std::memory_order_acq_rel);
    zlink_msg_t send_part;
    if (!move_part(&send_part, &parts[0])) {
        state->send_failures.fetch_add(1, std::memory_order_acq_rel);
        close_parts(parts, part_count);
        return;
    }

    const int send_rc =
      zlink_gateway_send_rid(state->gateway, source_rid, &send_part, 1,
                             ZLINK_DONTWAIT);
    if (send_rc == 0) {
        state->send_ok_count.fetch_add(1, std::memory_order_acq_rel);
        close_parts(parts, part_count);
        return;
    }
    const int saved_errno = errno;
    if (saved_errno == EAGAIN || saved_errno == EHOSTUNREACH
        || saved_errno == ENOTCONN) {
        enqueue_pending_reply(state, source_rid, zlink_msg_data(&send_part),
                            zlink_msg_size(&send_part));
    } else {
        state->send_failures.fetch_add(1, std::memory_order_acq_rel);
        std::cerr << "gateway server send failed: "
                  << zlink_strerror(saved_errno) << std::endl;
    }

    (void) zlink_msg_close(&send_part);
    close_parts(parts, part_count);
}

void gateway_server_send_ready(void *subject)
{
    gateway_server_state_t *state = g_server_state;
    if (!state || subject != state->gateway)
        return;

    while (!g_stop_requested.load(std::memory_order_acquire)) {
        pending_gateway_echo_t pending;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->pending.empty())
                break;
            pending = state->pending.front();
        }

        const send_status_t send_rc =
          try_send_gateway_reply(state->gateway, &pending.routing_id,
                                 pending.payload.empty()
                                   ? static_cast<const void *>(NULL)
                                   : static_cast<const void *>(pending.payload.data()),
                                 pending.payload.size());
        if (send_rc == send_status_ok) {
            state->send_ok_count.fetch_add(1, std::memory_order_acq_rel);
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->pending.empty())
                state->pending.pop_front();
            continue;
        }

        if (send_rc == send_status_blocked)
            break;

        state->send_failures.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->pending.empty())
                state->pending.pop_front();
        }
        break;
    }
}

void print_server_metrics(const std::string &lib_name,
                          const std::string &transport,
                          const std::vector<size_t> &sizes,
                          const bench_multi_resource_metrics_t &metrics,
                          const server_queue_stats_t &queue_stats)
{
    for (size_t i = 0; i < sizes.size(); ++i) {
        if (metrics.has_cpu_pct) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_cpu_pct," << std::fixed
                      << std::setprecision(2) << metrics.cpu_pct << std::endl;
        }
        if (metrics.has_mem_mb) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_mem_mb," << std::fixed
                      << std::setprecision(2) << metrics.mem_mb << std::endl;
        }
        print_server_queue_metrics(lib_name, k_pattern, transport, sizes[i],
                                   queue_stats);
    }
}

bool run_server_loop(const std::string &lib_name,
                     const std::string &transport)
{
    while (!g_stop_requested.load(std::memory_order_acquire)) {
        emit_requested_queue_probe(lib_name, transport);
        if (g_server_state
            && g_server_state->send_failures.load(std::memory_order_acquire) > 0)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return !g_server_state
           || g_server_state->send_failures.load(std::memory_order_acquire) == 0;
}

int run_server_benchmark(const std::string &lib_name,
                         const std::string &transport)
{
    set_perf_multi_pattern_env(k_pattern);

    if (!is_supported_transport(transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }
    if (!transport_available(transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    void *gateway = zlink_gateway_new(ctx.get(), k_service_name,
                                      k_server_routing_id,
                                      &gateway_server_handler);
    if (!gateway)
        return 1;

    if (!apply_gateway_options(gateway, settings)
        || zlink_gateway_set_send_ready_handler(gateway,
                                                &gateway_server_send_ready)
             != 0
        || !configure_gateway_tls_server(gateway, transport)) {
        zlink_gateway_destroy(&gateway);
        return 1;
    }

    const std::string endpoint =
      bind_gateway_endpoint(gateway, transport,
                            lib_name + std::string("_gateway_server"));
    if (endpoint.empty()) {
        zlink_gateway_destroy(&gateway);
        return 1;
    }

    gateway_server_state_t state;
    state.gateway = gateway;
    g_server_state = &state;

    g_stop_requested.store(false, std::memory_order_release);
    g_queue_probe_pending.store(false, std::memory_order_release);
    g_queue_probe_size.store(0, std::memory_order_release);
    install_signal_handlers();

    std::thread stdin_watcher([]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command(line, &queue_size)) {
                request_queue_probe(queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                g_stop_requested.store(true, std::memory_order_release);
                return;
            }
        }
        g_stop_requested.store(true, std::memory_order_release);
    });
    stdin_watcher.detach();

    std::vector<size_t> sizes = resolve_bench_msg_sizes(64);
    if (sizes.empty())
        sizes.push_back(64);

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();

    std::cout << "READY," << endpoint << std::endl;

    const size_t target_clients =
      resolve_multi_service_clients(settings.clients);
    if (!wait_for_gateway_peers(gateway, target_clients,
                                settings.connect_ready_timeout_ms)) {
        g_server_state = NULL;
        zlink_gateway_destroy(&gateway);
        return 1;
    }

    const bool ok = run_server_loop(lib_name, transport);

    size_t pending_depth = 0;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        pending_depth = state.pending.size();
    }
    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    const server_queue_stats_t queue_stats =
      sample_gateway_queue_stats(gateway, pending_depth);
    if (bench_debug_enabled ()) {
        std::cerr << "[multi-gateway-server] recv=" << state.recv_count.load ()
                  << " send_ok=" << state.send_ok_count.load ()
                  << " send_fail=" << state.send_failures.load ()
                  << " pending=" << pending_depth << std::endl;
    }
    print_server_metrics(lib_name, transport, sizes, metrics, queue_stats);

    g_server_state = NULL;
    zlink_gateway_destroy(&gateway);
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark(lib_name, transport);
}
