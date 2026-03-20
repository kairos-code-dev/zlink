#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_GATEWAY";
static const char *k_service_name = "perf-gateway";
static const char *k_server_routing_id = "perf-gateway-server";

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
        pending_count(0),
        recv_count(0),
        send_ok_count(0),
        send_failures(0)
    {
    }

    void *gateway;
    std::mutex mutex;
    std::deque<pending_gateway_echo_t> pending;
    std::atomic<size_t> pending_count;
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
    return zlink_set_tls_server(gateway, cert_path.c_str(),
                                key_path.c_str(), 0)
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

    return zlink_set_option(gateway, ZLINK_OPT_LINGER,
                                    &linger_ms, sizeof(linger_ms))
             == 0
           && zlink_set_option(gateway, ZLINK_OPT_SNDHWM,
                                       &sndhwm, sizeof(sndhwm))
                == 0
           && zlink_set_option(gateway, ZLINK_OPT_RCVHWM,
                                       &rcvhwm, sizeof(rcvhwm))
                == 0
           && zlink_set_option(gateway, ZLINK_OPT_SNDTIMEO,
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
    if (zlink_get_option(gateway, ZLINK_OPT_LAST_ENDPOINT,
                         last_endpoint, &last_endpoint_size)
        == 0) {
        endpoint.assign(last_endpoint);
    }

    return replace_any_host_with_localhost(endpoint);
}

server_queue_stats_t sample_gateway_queue_stats(void *gateway,
                                                size_t pending_depth)
{
    server_queue_stats_t stats;
    if (!gateway)
        return stats;

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

    const size_t pending_depth =
      state->pending_count.load(std::memory_order_acquire);

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

enum recv_status_t
{
    recv_status_ok = 0,
    recv_status_none = 1,
    recv_status_fatal = 2
};

send_status_t try_send_gateway_reply(void *gateway,
                                     const zlink_routing_id_t *routing_id,
                                     zlink_msg_t *parts,
                                     size_t part_count)
{
    if (!gateway || !routing_id || !parts || part_count != 1)
        return send_status_fatal;

    zlink_msg_t reply_part;
    if (zlink_msg_init(&reply_part) != 0)
        return send_status_fatal;
    if (zlink_msg_move(&reply_part, &parts[0]) != 0) {
        zlink_msg_close(&reply_part);
        return send_status_fatal;
    }

    const int rc =
      zlink_gateway_send_rid(gateway, routing_id, &reply_part, 1, ZLINK_DONTWAIT);
    if (rc == 0)
        return send_status_ok;

    const int saved_errno = errno;
    zlink_msg_close(&reply_part);
    if (saved_errno == EAGAIN || saved_errno == EINTR
        || saved_errno == EHOSTUNREACH || saved_errno == ENOTCONN) {
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
}

pending_gateway_echo_t make_pending_reply(const zlink_routing_id_t *routing_id,
                                          zlink_msg_t *parts,
                                          size_t part_count)
{
    pending_gateway_echo_t entry;
    if (!routing_id || !parts || part_count != 1)
        return entry;

    entry.routing_id = *routing_id;
    entry.parts.resize(1);
    const char *data =
      static_cast<const char *>(zlink_msg_data(&parts[0]));
    const size_t size = zlink_msg_size(&parts[0]);
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
    state->pending_count.store(state->pending.size(), std::memory_order_release);
}

send_status_t try_send_pending_reply(void *gateway,
                                     const pending_gateway_echo_t &pending)
{
    if (!gateway || pending.parts.size() != 1)
        return send_status_fatal;

    const std::vector<char> &payload = pending.parts[0];
    zlink_msg_t reply_part;
    if (zlink_msg_init_data(
          &reply_part,
          payload.empty()
            ? static_cast<void *>(NULL)
            : static_cast<void *>(const_cast<char *>(payload.data())),
          payload.size(),
          NULL,
          NULL)
        != 0) {
        return send_status_fatal;
    }

    const int rc = zlink_gateway_send_rid(gateway, &pending.routing_id,
                                          &reply_part, 1, ZLINK_DONTWAIT);
    if (rc == 0)
        return send_status_ok;

    const int saved_errno = errno;
    zlink_msg_close(&reply_part);
    if (saved_errno == EAGAIN || saved_errno == EINTR
        || saved_errno == EHOSTUNREACH || saved_errno == ENOTCONN) {
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
}

recv_status_t receive_gateway_request(gateway_server_state_t *state)
{
    if (!state || !state->gateway)
        return recv_status_fatal;

    zlink_routing_id_t source_rid;
    std::memset(&source_rid, 0, sizeof(source_rid));
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc =
      zlink_recv(state->gateway, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
    if (rc != 0) {
        const int err = zlink_errno();
        if (err == EAGAIN || err == EINTR)
            return recv_status_none;
        return recv_status_fatal;
    }

    if (!parts || part_count == 0) {
        if (parts) {
            zlink_multipart_close(parts, part_count);
            free(parts);
        }
        return recv_status_fatal;
    }
    if (part_count != 1) {
        if (bench_debug_enabled()
            && g_debug_recv_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
            std::cerr << "[multi-gateway-server] drop multipart part_count="
                      << part_count << std::endl;
        }
        zlink_multipart_close(parts, part_count);
        free(parts);
        return recv_status_ok;
    }

    const size_t payload_size = zlink_msg_size(&parts[0]);
    if (bench_debug_enabled()
        && g_debug_recv_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
        const size_t pending_depth =
          state->pending_count.load(std::memory_order_acquire);
        std::cerr << "[multi-gateway-server] recv size=" << payload_size
                  << " part_count=" << part_count
                  << " pending=" << pending_depth << std::endl;
    }

    const bool has_pending =
      state->pending_count.load(std::memory_order_acquire) > 0;
    state->recv_count.fetch_add(1, std::memory_order_acq_rel);
    if (has_pending) {
        enqueue_pending_reply(state, make_pending_reply(&source_rid, parts, part_count));
        zlink_multipart_close(parts, part_count);
        free(parts);
        return recv_status_ok;
    }

    const send_status_t send_rc =
      try_send_gateway_reply(state->gateway, &source_rid, parts, part_count);
    if (send_rc == send_status_ok) {
        if (bench_debug_enabled()
            && g_debug_send_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
            std::cerr << "[multi-gateway-server] send ok size="
                      << payload_size << " part_count=" << part_count
                      << std::endl;
        }
        state->send_ok_count.fetch_add(1, std::memory_order_acq_rel);
        zlink_multipart_close(parts, part_count);
        free(parts);
        return recv_status_ok;
    }

    if (send_rc == send_status_blocked) {
        const pending_gateway_echo_t pending_entry =
          make_pending_reply(&source_rid, parts, part_count);
        if (bench_debug_enabled()
            && g_debug_send_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
            std::cerr << "[multi-gateway-server] send blocked errno="
                      << errno << " size=" << payload_size
                      << " part_count=" << part_count << std::endl;
        }
        if (pending_entry.parts.empty()) {
            state->send_failures.fetch_add(1, std::memory_order_acq_rel);
            zlink_multipart_close(parts, part_count);
            free(parts);
            return recv_status_fatal;
        }
        enqueue_pending_reply(state, pending_entry);
        zlink_multipart_close(parts, part_count);
        free(parts);
        return recv_status_ok;
    }

    state->send_failures.fetch_add(1, std::memory_order_acq_rel);
    std::cerr << "gateway server send failed: "
              << zlink_strerror(errno) << std::endl;
    zlink_multipart_close(parts, part_count);
    free(parts);
    return recv_status_fatal;
}

bool drain_gateway_requests(gateway_server_state_t *state)
{
    while (!perf_stop_requested().load(std::memory_order_acquire)) {
        const recv_status_t recv_rc = receive_gateway_request(state);
        if (recv_rc == recv_status_none)
            return true;
        if (recv_rc == recv_status_fatal)
            return false;
    }
    return true;
}

bool flush_pending_replies(gateway_server_state_t *state)
{
    if (!state || !state->gateway)
        return false;

    while (!perf_stop_requested().load(std::memory_order_acquire)) {
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
            if (!state->pending.empty()) {
                state->pending.pop_front();
                state->pending_count.store(state->pending.size(),
                                           std::memory_order_release);
            }
            continue;
        }

        if (send_rc == send_status_blocked)
            return true;

        state->send_failures.fetch_add(1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->pending.empty()) {
                state->pending.pop_front();
                state->pending_count.store(state->pending.size(),
                                           std::memory_order_release);
            }
        }
        return false;
    }

    return true;
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

bool run_server_loop(void *poller,
                     const std::string &lib_name,
                     const std::string &transport)
{
    gateway_server_state_t *state = g_server_state;
    if (!state || !state->gateway || !poller)
        return false;

    while (!perf_stop_requested().load(std::memory_order_acquire)) {
        emit_requested_queue_probe(lib_name, transport);
        if (state->send_failures.load(std::memory_order_acquire) > 0)
            return false;

        const bool want_send =
          state->pending_count.load(std::memory_order_acquire) > 0;
        if (zlink_poller_modify(
              poller,
              state->gateway,
              static_cast<short>(ZLINK_POLLIN | (want_send ? ZLINK_POLLOUT : 0)))
            != 0) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-gateway-server] poller modify failed: "
                          << zlink_strerror(zlink_errno()) << std::endl;
            }
            return false;
        }

        zlink_poller_event_t event;
        std::memset(&event, 0, sizeof(event));
        const int poll_rc = zlink_poller_wait(poller, &event, 5);
        if (poll_rc < 0 && zlink_errno() != EINTR && zlink_errno() != EAGAIN) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-gateway-server] poller wait failed: "
                          << zlink_strerror(zlink_errno()) << std::endl;
            }
            return false;
        }

        if ((event.events & ZLINK_POLLIN) != 0 && !drain_gateway_requests(state))
            return false;
        if (state->pending_count.load(std::memory_order_acquire) > 0
            && (((event.events & ZLINK_POLLOUT) != 0)
                || ((event.events & ZLINK_POLLIN) != 0))
            && !flush_pending_replies(state)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-gateway-server] pending flush failed"
                          << std::endl;
            }
            return false;
        }
    }

    return state->send_failures.load(std::memory_order_acquire) == 0;
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
    void *gateway = zlink_gateway_new(ctx.get(), k_service_name);
    if (!gateway)
        return 1;
    if (zlink_set_routing_id(gateway, k_server_routing_id,
                             std::strlen(k_server_routing_id))
        != 0) {
        zlink_gateway_destroy(&gateway);
        return 1;
    }

    if (!apply_gateway_options(gateway, settings)
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
    void *poller = zlink_poller_new();
    if (!poller || zlink_poller_add(poller, gateway, gateway, ZLINK_POLLIN) != 0) {
        if (poller)
            zlink_poller_destroy(&poller);
        g_server_state = NULL;
        zlink_gateway_destroy(&gateway);
        return 1;
    }

    perf_stop_requested().store(false, std::memory_order_release);
    g_queue_probe_pending.store(false, std::memory_order_release);
    g_queue_probe_size.store(0, std::memory_order_release);
    install_perf_signal_handlers();

    std::thread stdin_watcher([]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command(line, &queue_size)) {
                request_queue_probe(queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                perf_stop_requested().store(true, std::memory_order_release);
                return;
            }
        }
        perf_stop_requested().store(true, std::memory_order_release);
    });
    stdin_watcher.detach();

    std::vector<size_t> sizes = resolve_bench_msg_sizes(64);
    if (sizes.empty())
        sizes.push_back(64);

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();

    std::cout << "READY," << endpoint << std::endl;

    const bool ok = run_server_loop(poller, lib_name, transport);

    size_t pending_depth = 0;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        pending_depth = state.pending.size();
    }
    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    const server_queue_stats_t queue_stats =
      sample_gateway_queue_stats(gateway, pending_depth);
    if (bench_debug_enabled()) {
        std::cerr << "[multi-gateway-server] recv=" << state.recv_count.load()
                  << " send_ok=" << state.send_ok_count.load()
                  << " send_fail=" << state.send_failures.load()
                  << " pending=" << pending_depth << std::endl;
    }
    print_server_metrics(lib_name, transport, sizes, metrics, queue_stats);

    g_server_state = NULL;
    zlink_poller_destroy(&poller);
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
