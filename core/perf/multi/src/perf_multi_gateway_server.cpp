#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_metric_header.hpp"
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
static std::atomic<int> g_debug_recv_logs(0);
static std::atomic<int> g_debug_send_logs(0);

struct pending_gateway_echo_t
{
    pending_gateway_echo_t()
    {
        std::memset(&routing_id, 0, sizeof(routing_id));
    }

    zlink_routing_id_t routing_id;
    std::vector<std::vector<char> > parts;
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

ptrdiff_t select_gateway_payload_part(zlink_msg_t *parts, size_t part_count)
{
    if (!parts || part_count == 0)
        return -1;

    for (size_t i = 0; i < part_count; ++i) {
        perf_multi_metric::header_t header;
        if (perf_multi_metric::decode_payload_header(
              zlink_msg_data(&parts[i]),
              zlink_msg_size(&parts[i]),
              &header)
            && header.magic == perf_multi_metric::k_magic) {
            return static_cast<ptrdiff_t>(i);
        }
    }

    for (size_t i = 0; i < part_count; ++i) {
        if (zlink_msg_size(&parts[i]) > 0)
            return static_cast<ptrdiff_t>(i);
    }

    return -1;
}

bool init_owned_part_copy(zlink_msg_t *out_part,
                          zlink_msg_t *parts,
                          size_t part_index)
{
    if (!out_part || !parts)
        return false;

    const size_t payload_size = zlink_msg_size(&parts[part_index]);
    if (zlink_msg_init_size(out_part, payload_size) != 0)
        return false;
    if (payload_size > 0) {
        std::memcpy(zlink_msg_data(out_part),
                    zlink_msg_data(&parts[part_index]),
                    payload_size);
    }
    return true;
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
        zlink_gateway_monitor_snapshot_t snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        if (zlink_gateway_monitor_snapshot(gateway, &snapshot) == 0
            && static_cast<int>(snapshot.ready_peer_count) >= expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    zlink_gateway_monitor_snapshot_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    return zlink_gateway_monitor_snapshot(gateway, &snapshot) == 0
           && static_cast<int>(snapshot.ready_peer_count) >= expected;
}

server_queue_stats_t sample_gateway_queue_stats(void *gateway,
                                                size_t pending_depth)
{
    server_queue_stats_t stats;
    if (!gateway)
        return stats;

    // Registry-based gateway peer introspection intentionally does not expose
    // socket queue counters. Preserve the local pending depth signal only.
    stats.snd_pending_max = static_cast<double>(pending_depth);
    stats.rcv_pending_max = 0.0;
    stats.rcv_pending_end = 0.0;
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

send_status_t try_send_gateway_reply(void *gateway,
                                     const zlink_routing_id_t *routing_id,
                                     zlink_msg_t *parts,
                                     size_t part_count)
{
    if (!gateway || !routing_id || !parts || part_count == 0)
        return send_status_fatal;

    const ptrdiff_t payload_index = select_gateway_payload_part(parts, part_count);
    if (payload_index < 0)
        return send_status_fatal;

    for (int attempt = 0; attempt < 256; ++attempt) {
        zlink_msg_t reply_part;
        if (!init_owned_part_copy(
              &reply_part,
              parts,
              static_cast<size_t>(payload_index))) {
            return send_status_fatal;
        }

        const int rc =
          zlink_gateway_send_rid(gateway, routing_id, &reply_part, 1, 0);
        if (rc == 0)
            return send_status_ok;

        const int saved_errno = errno;
        zlink_msg_close(&reply_part);
        if (saved_errno != EAGAIN && saved_errno != EINTR
            && saved_errno != EHOSTUNREACH && saved_errno != ENOTCONN) {
            errno = saved_errno;
            return send_status_fatal;
        }
        if (attempt + 1 == 256) {
            errno = saved_errno;
            return send_status_blocked;
        }
        std::this_thread::yield();
    }

    errno = EAGAIN;
    return send_status_blocked;
}

pending_gateway_echo_t make_pending_reply(const zlink_routing_id_t *routing_id,
                                          zlink_msg_t *parts,
                                          size_t part_count)
{
    pending_gateway_echo_t entry;
    if (!routing_id || !parts || part_count == 0)
        return entry;

    const ptrdiff_t payload_index = select_gateway_payload_part(parts, part_count);
    if (payload_index < 0)
        return entry;

    entry.routing_id = *routing_id;
    entry.parts.resize(1);
    const char *data =
      static_cast<const char *>(
        zlink_msg_data(&parts[static_cast<size_t>(payload_index)]));
    const size_t size =
      zlink_msg_size(&parts[static_cast<size_t>(payload_index)]);
    entry.parts[0].assign(data, data + size);
    return entry;
}

void enqueue_pending_reply(gateway_server_state_t *state,
                           const pending_gateway_echo_t &entry)
{
    if (!state || entry.parts.empty())
        return;

    std::lock_guard<std::mutex> lock(state->mutex);
    state->pending.push_back(entry);
}

send_status_t try_send_pending_reply(void *gateway,
                                     const pending_gateway_echo_t &pending)
{
    if (!gateway || pending.parts.empty())
        return send_status_fatal;

    std::vector<zlink_msg_t> send_parts(pending.parts.size());
    for (size_t i = 0; i < pending.parts.size(); ++i) {
        const std::vector<char> &payload = pending.parts[i];
        if (zlink_msg_init_data(
              &send_parts[i],
              payload.empty()
                ? static_cast<void *>(NULL)
                : static_cast<void *>(const_cast<char *>(payload.data())),
              payload.size(),
              NULL,
              NULL)
            != 0) {
            close_parts(&send_parts[0], i);
            return send_status_fatal;
        }
    }

    const int rc = zlink_gateway_send_rid(gateway, &pending.routing_id,
                                          &send_parts[0], send_parts.size(),
                                          ZLINK_DONTWAIT);
    if (rc == 0)
        return send_status_ok;

    const int saved_errno = errno;
    close_parts(&send_parts[0], send_parts.size());
    if (saved_errno == EAGAIN || saved_errno == EINTR
        || saved_errno == EHOSTUNREACH || saved_errno == ENOTCONN) {
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
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
        if (bench_debug_enabled()
            && g_debug_recv_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
            std::cerr << "[multi-gateway-server] drop multipart part_count="
                      << part_count << std::endl;
        }
        close_parts(parts, part_count);
        return;
    }
    const zlink_routing_id_t reply_routing_id = *source_rid;
    const ptrdiff_t payload_index = select_gateway_payload_part(parts, part_count);
    const size_t payload_size =
      payload_index >= 0
        ? zlink_msg_size(&parts[static_cast<size_t>(payload_index)])
        : 0;
    if (bench_debug_enabled()
        && g_debug_recv_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
        size_t pending_depth = 0;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            pending_depth = state->pending.size();
        }
        std::cerr << "[multi-gateway-server] recv size=" << payload_size
                  << " part_count=" << part_count
                  << " pending=" << pending_depth;
        std::cerr << std::endl;
    }

    bool has_pending = false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        has_pending = !state->pending.empty();
    }
    if (has_pending) {
        state->recv_count.fetch_add(1, std::memory_order_acq_rel);
        enqueue_pending_reply(state,
                              make_pending_reply(&reply_routing_id,
                                                 parts,
                                                 part_count));
        close_parts(parts, part_count);
        return;
    }

    state->recv_count.fetch_add(1, std::memory_order_acq_rel);
    const pending_gateway_echo_t pending_entry =
      make_pending_reply(&reply_routing_id, parts, part_count);
    const send_status_t send_rc =
      try_send_gateway_reply(state->gateway,
                             &reply_routing_id,
                             parts,
                             part_count);
    if (send_rc == send_status_ok) {
        if (bench_debug_enabled()
            && g_debug_send_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
            std::cerr << "[multi-gateway-server] send ok size="
                      << payload_size << " part_count=" << part_count
                      << std::endl;
        }
        state->send_ok_count.fetch_add(1, std::memory_order_acq_rel);
        close_parts(parts, part_count);
        return;
    }
    if (send_rc == send_status_blocked) {
        if (bench_debug_enabled()
            && g_debug_send_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
            std::cerr << "[multi-gateway-server] send blocked errno="
                      << errno << " size=" << payload_size
                      << " part_count=" << part_count
                      << std::endl;
        }
        enqueue_pending_reply(state, pending_entry);
        close_parts(parts, part_count);
    } else {
        state->send_failures.fetch_add(1, std::memory_order_acq_rel);
        std::cerr << "gateway server send failed: "
                  << zlink_strerror(errno) << std::endl;
        close_parts(parts, part_count);
    }
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
          try_send_pending_reply(state->gateway, pending);
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
        if (g_server_state && g_server_state->gateway)
            gateway_server_send_ready(g_server_state->gateway);
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
