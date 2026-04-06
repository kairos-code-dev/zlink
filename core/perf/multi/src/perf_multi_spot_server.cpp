#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../common/perf_multi_spot_handle.hpp"
#include "../../common/perf_tls_setup.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <condition_variable>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace zlink {
class spot_node_t;
class spot_pub_t;
class spot_sub_t;
}

namespace {

static const int k_spot_role_pub = 1;

static const char *k_pattern = "MULTI_SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const uint32_t k_metric_run_id = 1U;

static std::atomic<bool> g_queue_probe_pending(false);
static std::atomic<size_t> g_queue_probe_size(0);

void ensure_multi_spot_mesh_pub_budget_default()
{
    // Keep perf aligned with the core default unless the caller overrides it.
}

struct spot_server_state_t
{
    spot_server_state_t() :
        node(NULL),
        pub(NULL),
        control_node(NULL),
        control_pub(NULL),
        control_sub(NULL),
        msg_size(0),
        phase(perf_multi_metric::phase_unknown),
        next_seq(1),
        expected_ready_count(1),
        send_enabled(false),
        send_pending(false),
        fatal_errno(0)
    {
    }

    void *node;
    void *pub;
    void *control_node;
    void *control_pub;
    void *control_sub;
    size_t msg_size;
    perf_multi_metric::phase_t phase;
    uint64_t next_seq;
    size_t expected_ready_count;
    std::atomic<bool> send_enabled;
    std::atomic<bool> send_pending;
    std::atomic<int> fatal_errno;
    std::mutex start_wait_mutex;
    std::condition_variable start_wait_cv;
    std::set<size_t> pending_start_sizes;
    std::mutex ready_mutex;
    std::condition_variable ready_cv;
    std::map<size_t, std::set<size_t> > ready_slots_by_size;
    std::map<size_t, size_t> ready_count_by_size;
    std::vector<std::string> control_peer_endpoints;
};

spot_server_state_t *g_server_state = NULL;

void discard_spot_parts(const zlink_routing_id_t *,
                        const char *,
                        size_t,
                        zlink_msg_t *parts,
                        size_t part_count,
                        void *)
{
    if (!parts)
        return;
    for (size_t i = 0; i < part_count; ++i)
        zlink_msg_close(&parts[i]);
}

void fast_exit_process(int exit_code)
{
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
}

void request_queue_probe(size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store(msg_size, std::memory_order_release);
    g_queue_probe_pending.store(true, std::memory_order_release);
}

bool parse_start_command(const std::string &line, size_t *msg_size_out)
{
    static const char prefix[] = "START,";
    if (!msg_size_out
        || line.compare(0, sizeof(prefix) - 1, prefix) != 0) {
        return false;
    }

    const char *value = line.c_str() + (sizeof(prefix) - 1);
    char *end = NULL;
    const unsigned long long parsed = std::strtoull(value, &end, 10);
    if (!end || *end != '\0' || parsed == 0)
        return false;

    *msg_size_out = static_cast<size_t>(parsed);
    return true;
}

bool parse_ready_command(const void *data,
                         size_t size,
                         size_t *msg_size_out,
                         size_t *slot_index_out)
{
    static const char prefix[] = "READY,";
    if (!data || !msg_size_out || !slot_index_out
        || size < (sizeof(prefix) - 1)
        || std::memcmp(data, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }

    std::string line(static_cast<const char *>(data), size);
    const size_t comma = line.find(',', sizeof(prefix) - 1);
    if (comma == std::string::npos)
        return false;

    char *end = NULL;
    const unsigned long long msg_size =
      std::strtoull(line.c_str() + (sizeof(prefix) - 1), &end, 10);
    if (!end || static_cast<size_t>(end - line.c_str()) != comma || msg_size == 0)
        return false;

    const unsigned long long slot_index =
      std::strtoull(line.c_str() + comma + 1, &end, 10);
    if (!end || *end != '\0')
        return false;

    *msg_size_out = static_cast<size_t>(msg_size);
    *slot_index_out = static_cast<size_t>(slot_index);
    return true;
}

bool parse_ready_count_command(const void *data,
                               size_t size,
                               size_t *msg_size_out,
                               size_t *ready_count_out)
{
    static const char prefix[] = "READY_COUNT,";
    if (!data || !msg_size_out || !ready_count_out
        || size < (sizeof(prefix) - 1)
        || std::memcmp(data, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }

    std::string line(static_cast<const char *>(data), size);
    const size_t comma = line.find(',', sizeof(prefix) - 1);
    if (comma == std::string::npos)
        return false;

    char *end = NULL;
    const unsigned long long msg_size =
      std::strtoull(line.c_str() + (sizeof(prefix) - 1), &end, 10);
    if (!end || static_cast<size_t>(end - line.c_str()) != comma || msg_size == 0)
        return false;

    const unsigned long long ready_count =
      std::strtoull(line.c_str() + comma + 1, &end, 10);
    if (!end || *end != '\0' || ready_count == 0)
        return false;

    *msg_size_out = static_cast<size_t>(msg_size);
    *ready_count_out = static_cast<size_t>(ready_count);
    return true;
}

bool parse_connect_command(const std::string &line, std::string *endpoint_out)
{
    static const char prefix[] = "CONNECT_CONTROL,";
    if (!endpoint_out
        || line.compare(0, sizeof(prefix) - 1, prefix) != 0) {
        return false;
    }

    const std::string endpoint = line.substr(sizeof(prefix) - 1);
    if (endpoint.empty())
        return false;

    *endpoint_out = endpoint;
    return true;
}

void emit_requested_queue_probe(const std::string &lib_name,
                                const std::string &transport);

bool apply_spot_server_options(void *pub,
                               const multi_bench_settings_t &settings)
{
    const int linger_ms = 0;
    const int sndhwm = bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200);
    const int nodrop =
      resolve_multi_int_env("PERF_MULTI_SPOT_XPUB_NODROP", 1, 0);
    const int sndbuf = bench_socket_buffer_bytes_from_env("PERF_SNDBUF", -1);
    const int rcvbuf = bench_socket_buffer_bytes_from_env("PERF_RCVBUF", -1);

    if (zlink_set_option(pub, ZLINK_OPT_LINGER, &linger_ms,
                             sizeof(linger_ms))
          != 0
        || zlink_set_option(pub, ZLINK_OPT_SNDHWM, &sndhwm,
                                sizeof(sndhwm))
             != 0
        || zlink_set_option(pub, ZLINK_OPT_SNDTIMEO, &sndtimeo_ms,
                                sizeof(sndtimeo_ms))
             != 0
        || zlink_set_pub_option(pub, ZLINK_PUB_OPT_NODROP, &nodrop,
                                sizeof(nodrop))
             != 0) {
        return false;
    }

    if (sndbuf > 0
        && zlink_set_option(pub, ZLINK_OPT_SNDBUF, &sndbuf,
                                sizeof(sndbuf))
             != 0) {
        return false;
    }

    if (rcvbuf > 0
        && zlink_set_option(pub, ZLINK_OPT_RCVBUF, &rcvbuf,
                                sizeof(rcvbuf))
             != 0) {
        return false;
    }

    return true;
}

bool apply_spot_control_options(void *pub,
                                void *sub,
                                const multi_bench_settings_t &settings)
{
    if (!pub || !sub)
        return false;

    const int linger_ms = 0;
    const int timeout_ms = std::max(1000, settings.connect_ready_timeout_ms);
    const int hwm = std::max<int>(1024, static_cast<int>(settings.clients * 8));
    const int nodrop = 1;

    if (zlink_set_option(pub, ZLINK_OPT_LINGER, &linger_ms,
                         sizeof(linger_ms))
          != 0
        || zlink_set_option(pub, ZLINK_OPT_SNDHWM, &hwm, sizeof(hwm)) != 0
        || zlink_set_option(pub, ZLINK_OPT_SNDTIMEO, &timeout_ms,
                            sizeof(timeout_ms))
             != 0
        || zlink_set_pub_option(pub, ZLINK_PUB_OPT_NODROP, &nodrop,
                                sizeof(nodrop))
             != 0
        || zlink_set_option(sub, ZLINK_OPT_LINGER, &linger_ms,
                            sizeof(linger_ms))
             != 0
        || zlink_set_option(sub, ZLINK_OPT_RCVHWM, &hwm, sizeof(hwm)) != 0
        || zlink_set_option(sub, ZLINK_OPT_RCVTIMEO, &timeout_ms,
                            sizeof(timeout_ms))
             != 0) {
        return false;
    }

    return true;
}

std::string bind_spot_endpoint(void *node,
                               const std::string &transport,
                               const std::string &token)
{
    const int bind_port =
      resolve_multi_int_env("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    if (bind_port > 0) {
        return perf_bind_endpoint_once(node,
                                       make_fixed_endpoint(transport, bind_port),
                                       transport,
                                       &perf_bind_spot_node_endpoint,
                                       false);
    }

    int base_port = 32000;
#if !defined(_WIN32)
    base_port += static_cast<int>(::getpid() % 1000) * 8;
#endif
    (void) token;
    return perf_bind_fixed_endpoint_range(
      node, transport, base_port, 64, &perf_bind_spot_node_endpoint);
}

std::string bind_control_spot_endpoint(void *node,
                                       const std::string &transport)
{
    const int bind_port =
      resolve_multi_int_env("PERF_MULTI_SERVER_CONTROL_BIND_PORT", 0, 0);
    if (bind_port > 0) {
        return perf_bind_endpoint_once(node,
                                       make_fixed_endpoint(transport, bind_port),
                                       transport,
                                       &perf_bind_spot_node_endpoint,
                                       false);
    }

    int base_port = 32128;
#if !defined(_WIN32)
    base_port += static_cast<int>(::getpid() % 1000) * 8;
#endif
    return perf_bind_fixed_endpoint_range(
      node, transport, base_port, 64, &perf_bind_spot_node_endpoint);
}

server_queue_stats_t sample_spot_queue_stats(void *pub, bool send_pending)
{
    server_queue_stats_t stats;
    (void) pub;
    if (send_pending) {
        stats.snd_pending_max = 1.0;
    }
    return stats;
}

void emit_requested_queue_probe(const std::string &lib_name,
                                const std::string &transport)
{
    if (!g_queue_probe_pending.exchange(false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load(std::memory_order_acquire);
    spot_server_state_t *state = g_server_state;
    if (msg_size == 0 || !state || !state->pub)
        return;

    const bool send_pending =
      state->send_pending.load(std::memory_order_acquire);
    const server_queue_stats_t queue_stats =
      sample_spot_queue_stats(state->pub, send_pending);
    print_server_queue_metrics(lib_name, k_pattern, transport, msg_size,
                               queue_stats);
}

void notify_size_start(spot_server_state_t *state, size_t msg_size)
{
    if (!state || msg_size == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(state->start_wait_mutex);
        state->pending_start_sizes.insert(msg_size);
    }
    state->start_wait_cv.notify_all();
}

bool connect_control_peer(spot_server_state_t *state,
                          const std::string &endpoint)
{
    if (!state || !state->control_node || endpoint.empty()) {
        errno = EINVAL;
        return false;
    }

    if (zlink_spot_node_connect_peer(state->control_node, endpoint.c_str()) != 0)
        return false;

    if (bench_transition_debug_enabled()) {
        zlink_spot_node_status_t status;
        std::memset(&status, 0, sizeof(status));
        if (zlink_spot_node_status_snapshot(state->control_node, &status) == 0) {
            std::cerr << "[multi-spot-server] reverse connect ok ts_us="
                      << perf_multi_metric::now_us()
                      << " endpoint=" << endpoint
                      << " active=" << status.active_peer_count
                      << " connected=" << status.connected_peer_count
                      << " ready_subjects=" << status.ready_subject_count
                      << std::endl;
        } else {
            std::cerr << "[multi-spot-server] reverse connect ok ts_us="
                      << perf_multi_metric::now_us()
                      << " endpoint=" << endpoint << std::endl;
        }
    }

    return true;
}

bool wait_for_control_peer_connection(spot_server_state_t *state,
                                      size_t expected_connected_count,
                                      int timeout_ms)
{
    if (!state || !state->control_node || expected_connected_count == 0) {
        errno = EINVAL;
        return false;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline) {
        zlink_spot_node_status_t status;
        std::memset(&status, 0, sizeof(status));
        if (zlink_spot_node_status_snapshot(state->control_node, &status) == 0
            && status.connected_peer_count >= expected_connected_count) {
            return true;
        }

        if (perf_stop_requested ().load(std::memory_order_acquire)
            || state->fatal_errno.load(std::memory_order_acquire) != 0) {
            errno = EIO;
            return false;
        }

        zlink_pollitem_t item = {NULL, 0, 0, 0};
        if (zlink_poll(&item, 0, 5) < 0 && zlink_errno() != EINTR)
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

bool register_control_peer_endpoint(spot_server_state_t *state,
                                    const std::string &endpoint)
{
    if (!state || endpoint.empty())
        return false;

    for (size_t i = 0; i < state->control_peer_endpoints.size(); ++i) {
        if (state->control_peer_endpoints[i] == endpoint)
            return true;
    }

    state->control_peer_endpoints.push_back(endpoint);
    return true;
}

bool ensure_control_peers_connected(spot_server_state_t *state)
{
    if (!state)
        return false;

    for (size_t i = 0; i < state->control_peer_endpoints.size(); ++i) {
        if (!connect_control_peer(state, state->control_peer_endpoints[i]))
            return false;
    }

    return true;
}

void disconnect_control_peers(spot_server_state_t *state)
{
    if (!state || !state->control_node)
        return;

    for (size_t i = 0; i < state->control_peer_endpoints.size(); ++i) {
        const std::string &endpoint = state->control_peer_endpoints[i];
        if (endpoint.empty())
            continue;
        (void) zlink_spot_node_disconnect_peer(state->control_node,
                                               endpoint.c_str());
    }
}

void record_ready_slot(spot_server_state_t *state,
                       size_t msg_size,
                       size_t slot_index)
{
    if (!state || msg_size == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(state->ready_mutex);
        state->ready_slots_by_size[msg_size].insert(slot_index);
    }
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] ready slot ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " slot=" << slot_index << std::endl;
    }
    state->ready_cv.notify_all();
}

void record_ready_count(spot_server_state_t *state,
                        size_t msg_size,
                        size_t ready_count)
{
    if (!state || msg_size == 0 || ready_count == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(state->ready_mutex);
        state->ready_count_by_size[msg_size] += ready_count;
    }
    state->ready_cv.notify_all();
}

bool wait_for_ready_slots(spot_server_state_t *state,
                          size_t msg_size,
                          int timeout_ms)
{
    if (!state || msg_size == 0)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        char topic[256];
        size_t topic_len = sizeof(topic) - 1;
        const int rc =
          zlink_subscribe(state->control_sub,
                          &parts,
                          &part_count,
                          ZLINK_DONTWAIT,
                          topic,
                          &topic_len);
        if (rc == 0) {
            size_t ready_size = 0;
            size_t slot_index = 0;
            size_t ready_count = 0;
            if (topic_len > 0 && topic[topic_len - 1] == '\0')
                --topic_len;
            if (bench_transition_debug_enabled() && part_count > 0) {
                std::string payload(
                  static_cast<const char *>(zlink_msg_data(&parts[0])),
                  zlink_msg_size(&parts[0]));
                std::cerr << "[multi-spot-server] ctrl recv ts_us="
                          << perf_multi_metric::now_us()
                          << " topic="
                          << std::string(topic, topic_len)
                          << " payload=" << payload << std::endl;
            }
            if (topic_len == std::strlen(k_topic)
                && std::memcmp(topic, k_topic, std::strlen(k_topic)) == 0
                && part_count > 0
                && parse_ready_command(zlink_msg_data(&parts[0]),
                                       zlink_msg_size(&parts[0]),
                                       &ready_size,
                                       &slot_index)) {
                record_ready_slot(state, ready_size, slot_index);
            } else if (topic_len == std::strlen(k_topic)
                       && std::memcmp(topic, k_topic, std::strlen(k_topic)) == 0
                       && part_count > 0
                       && parse_ready_count_command(zlink_msg_data(&parts[0]),
                                                   zlink_msg_size(&parts[0]),
                                                   &ready_size,
                                                   &ready_count)) {
                record_ready_count(state, ready_size, ready_count);
            }
            perf_close_multipart(parts, part_count);
        } else {
            const int err = zlink_errno() != 0 ? zlink_errno() : errno;
            if (!(err == EAGAIN || err == EINTR || err == EWOULDBLOCK
                  || err == ETIMEDOUT)) {
                return false;
            }
        }

        if (perf_stop_requested ().load(std::memory_order_acquire)
            || state->fatal_errno.load(std::memory_order_acquire) != 0) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(state->ready_mutex);
            size_t ready_units = 0;
            std::map<size_t, size_t>::const_iterator count_it =
              state->ready_count_by_size.find(msg_size);
            if (count_it != state->ready_count_by_size.end())
                ready_units += count_it->second;
            std::map<size_t, std::set<size_t> >::const_iterator it =
              state->ready_slots_by_size.find(msg_size);
            if (it != state->ready_slots_by_size.end())
                ready_units += it->second.size();
            if (ready_units >= state->expected_ready_count) {
                return true;
            }
        }

        zlink_pollitem_t item = {NULL, 0, 0, 0};
        if (zlink_poll(&item, 0, 5) < 0 && zlink_errno() != EINTR)
            return false;
    }

    return false;
}

bool publish_control_start(spot_server_state_t *state, size_t msg_size)
{
    if (!state || !state->control_pub || msg_size == 0)
        return false;

    char payload[64];
    const int payload_len = std::snprintf(payload,
                                          sizeof(payload),
                                          "START,%lu",
                                          static_cast<unsigned long>(msg_size));
    if (payload_len <= 0
        || static_cast<size_t>(payload_len) >= sizeof(payload)) {
        errno = EMSGSIZE;
        return false;
    }

    zlink_msg_t part;
    if (zlink_msg_init_size(&part, static_cast<size_t>(payload_len)) != 0)
        return false;
    std::memcpy(zlink_msg_data(&part), payload, static_cast<size_t>(payload_len));
    const int rc = zlink_publish(state->control_pub, k_topic, &part, 1, 0);
    const int saved_errno = rc == 0 ? 0 : errno;
    (void) zlink_msg_close(&part);
    if (rc == 0)
        return true;
    errno = saved_errno;
    return false;
}

bool wait_for_size_start(spot_server_state_t *state,
                         size_t msg_size,
                         int timeout_ms)
{
    if (!state || msg_size == 0)
        return false;

    std::unique_lock<std::mutex> lock(state->start_wait_mutex);
    if (state->pending_start_sizes.erase(msg_size) != 0)
        return true;

    const bool signaled = state->start_wait_cv.wait_for(
      lock,
      std::chrono::milliseconds(std::max(1, timeout_ms)),
      [state, msg_size]() {
          return perf_stop_requested ().load(std::memory_order_acquire)
                 || state->fatal_errno.load(std::memory_order_acquire) != 0
                 || state->pending_start_sizes.count(msg_size) != 0;
      });
    return signaled && state->pending_start_sizes.erase(msg_size) != 0;
}


bool wait_for_spot_send_progress(bool send_enabled)
{
    return perf_socket_poll(NULL, 0, send_enabled ? 2 : 1) >= 0;
}

enum send_status_t
{
    send_status_ok = 0,
    send_status_blocked = 1,
    send_status_fatal = 2
};

send_status_t try_publish_locked(spot_server_state_t *state)
{
    if (!state || !state->pub || state->msg_size == 0
        || !state->send_enabled.load(std::memory_order_acquire))
        return send_status_fatal;

    const size_t payload_size =
      std::max(state->msg_size, perf_multi_metric::header_size());
    zlink_msg_t part;
    if (zlink_msg_init_size(&part, payload_size) != 0) {
        return send_status_fatal;
    }
    if (!perf_multi_metric::stamp_payload(
          zlink_msg_data(&part),
          payload_size,
          k_metric_run_id,
          state->phase,
          state->msg_size,
          state->next_seq,
          perf_multi_metric::now_us())) {
        zlink_msg_close(&part);
        return send_status_fatal;
    }

    const int rc =
      zlink_publish(state->pub, k_topic, &part, 1, ZLINK_DONTWAIT);
    const int saved_errno = rc == 0 ? 0 : errno;
    (void) zlink_msg_close(&part);

    if (rc == 0) {
        state->send_pending.store(false, std::memory_order_release);
        ++state->next_seq;
        return send_status_ok;
    }
    if (saved_errno == EAGAIN) {
        state->send_pending.store(true, std::memory_order_release);
        errno = saved_errno;
        return send_status_blocked;
    }

    errno = saved_errno;
    return send_status_fatal;
}

bool run_phase(spot_server_state_t *state,
               const std::string &lib_name,
               const std::string &transport,
               size_t msg_size,
               perf_multi_metric::phase_t phase,
               double duration_seconds,
               bool send_enabled)
{
    if (duration_seconds <= 0.0)
        return true;

    state->msg_size = msg_size;
    state->phase = phase;
    state->next_seq = 1;
    state->send_enabled.store(send_enabled, std::memory_order_release);
    state->send_pending.store(false, std::memory_order_release);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase start ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " phase=" << static_cast<int>(phase)
                  << " send=" << (send_enabled ? 1 : 0) << std::endl;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(duration_seconds));

    while (!perf_stop_requested ().load(std::memory_order_acquire)
           && std::chrono::steady_clock::now() < deadline) {
        emit_requested_queue_probe(lib_name, transport);

        bool progressed = false;
        if (state->fatal_errno.load(std::memory_order_acquire) != 0)
            return false;
        if (state->send_enabled.load(std::memory_order_acquire)) {
            const send_status_t rc = try_publish_locked(state);
            if (rc == send_status_fatal) {
                state->fatal_errno.store(errno != 0 ? errno : EIO,
                                         std::memory_order_release);
                return false;
            }
            progressed = rc == send_status_ok;
        }

        if (!progressed) {
            wait_for_spot_send_progress(send_enabled);
        }
    }

    state->send_enabled.store(false, std::memory_order_release);
    state->send_pending.store(false, std::memory_order_release);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase done ts_us="
                  << perf_multi_metric::now_us()
                  << " size=" << msg_size
                  << " phase=" << static_cast<int>(phase)
                  << " sent="
                  << (state->next_seq > 0 ? state->next_seq - 1 : 0)
                  << " stop="
                  << (perf_stop_requested ().load(std::memory_order_acquire) ? 1 : 0)
                  << " fatal_errno="
                  << state->fatal_errno.load(std::memory_order_acquire)
                  << std::endl;
    }
    if (state->fatal_errno.load(std::memory_order_acquire) != 0)
        return false;

    // The multi runner stops the server after the client completes a size case.
    // Treat that external stop as graceful so the next size can run.
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

bool run_server_loop(spot_server_state_t *state,
                     const multi_bench_settings_t &settings,
                     const std::string &lib_name,
                     const std::string &transport,
                     const std::vector<size_t> &msg_sizes)
{
    const double warmup_seconds =
      static_cast<double>(std::max(0, settings.warmup_seconds));
    const double active_seconds =
      static_cast<double>(std::max(1, settings.duration_seconds));
    const int start_timeout_ms =
      std::max(settings.connect_ready_timeout_ms,
               std::max(1000, settings.connect_ready_timeout_ms * 6));

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (perf_stop_requested ().load(std::memory_order_acquire)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] loop stop before size="
                          << msg_sizes[i] << std::endl;
            }
            return state->fatal_errno.load(std::memory_order_acquire) == 0;
        }

        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-server] ready wait begin ts_us="
                      << perf_multi_metric::now_us()
                      << " size=" << msg_sizes[i]
                      << " timeout_ms=" << start_timeout_ms << std::endl;
        }
        if (!wait_for_size_start (state, msg_sizes[i], start_timeout_ms)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] runner start timeout ts_us="
                          << perf_multi_metric::now_us()
                          << " size=" << msg_sizes[i] << std::endl;
            }
            return false;
        }
        if (!ensure_control_peers_connected(state)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-server] ensure control peers failed"
                          << " size=" << msg_sizes[i]
                          << " err=" << zlink_errno() << std::endl;
            }
            return false;
        }
        if (!wait_for_ready_slots(state, msg_sizes[i], start_timeout_ms)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] ready wait timeout ts_us="
                          << perf_multi_metric::now_us()
                          << " size=" << msg_sizes[i] << std::endl;
            }
            return false;
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-server] ready wait done ts_us="
                      << perf_multi_metric::now_us()
                      << " size=" << msg_sizes[i] << std::endl;
        }
        if (!publish_control_start(state, msg_sizes[i])) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-server] start publish failed size="
                          << msg_sizes[i] << " err=" << zlink_errno()
                          << std::endl;
            }
            return false;
        }
        if (bench_transition_debug_enabled()) {
            std::cerr << "[multi-spot-server] start publish done ts_us="
                      << perf_multi_metric::now_us()
                      << " size=" << msg_sizes[i] << std::endl;
        }
        if (!run_phase(state, lib_name, transport, msg_sizes[i],
                       perf_multi_metric::phase_warmup, warmup_seconds, true)
            || !run_phase(state, lib_name, transport, msg_sizes[i],
                          perf_multi_metric::phase_active, active_seconds,
                          true)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] loop abort size="
                          << msg_sizes[i]
                          << " stop="
                          << (perf_stop_requested ().load(std::memory_order_acquire)
                                ? 1
                                : 0)
                          << " fatal_errno="
                          << state->fatal_errno.load(std::memory_order_acquire)
                          << std::endl;
            }
            return false;
        }
    }

    return true;
}

int run_server_benchmark(const std::string &lib_name,
                         const std::string &transport)
{
    set_perf_multi_pattern_env(k_pattern);
    ensure_multi_spot_mesh_pub_budget_default();

    if (!is_supported_transport(transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }
    if (!transport_available(transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    sync_spot_internal_mesh_pub_hwm(
      bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm));
    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;
    std::vector<size_t> msg_sizes = resolve_bench_msg_sizes(64);
    if (msg_sizes.empty())
        msg_sizes.push_back(64);

    size_t max_msg_size = 64;
    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];
    }

    void *node = zlink_spot_node_new(ctx.get());
    void *control_node = zlink_spot_node_new(ctx.get());
    void *pub = NULL;
    void *control_pub = NULL;
    void *control_sub = NULL;
    if (!node || !control_node) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] node create failed err="
                      << zlink_errno() << std::endl;
        if (node)
            zlink_spot_node_destroy(&node);
        if (control_node)
            zlink_spot_node_destroy(&control_node);
        return 1;
    }

    if (!setup_tls_server(node, transport)
        || !setup_tls_server(control_node, transport)
        || !setup_tls_client(control_node, transport)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] tls configure failed err="
                      << zlink_errno() << std::endl;
        zlink_spot_node_destroy(&node);
        zlink_spot_node_destroy(&control_node);
        return 1;
    }

    pub = perf_create_default_spot_handle(node);
    if (!pub) {
        zlink_spot_node_destroy(&node);
        zlink_spot_node_destroy(&control_node);
        return 1;
    }

    control_pub = perf_create_default_spot_handle(control_node);
    control_sub = perf_create_default_spot_handle(control_node);
    if (!control_pub || !control_sub) {
        if (control_pub)
            perf_destroy_default_spot_handle(&control_pub);
        if (control_sub)
            perf_destroy_default_spot_handle(&control_sub);
        perf_destroy_default_spot_handle(&pub);
        zlink_spot_node_destroy(&node);
        zlink_spot_node_destroy(&control_node);
        return 1;
    }

    if (!apply_spot_server_options(pub, settings)
        || !apply_spot_control_options(control_pub, control_sub, settings)
        || zlink_set_subscription(control_sub, k_topic) != 0) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] pub init failed err="
                      << zlink_errno() << std::endl;
        perf_destroy_default_spot_handle(&control_pub);
        perf_destroy_default_spot_handle(&control_sub);
        perf_destroy_default_spot_handle(&pub);
        zlink_spot_node_destroy(&node);
        zlink_spot_node_destroy(&control_node);
        return 1;
    }

    const std::string endpoint =
      bind_spot_endpoint(node, transport,
                         lib_name + std::string("_spot_server"));
    if (endpoint.empty()) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] bind failed err="
                      << zlink_errno() << std::endl;
        perf_destroy_default_spot_handle(&control_pub);
        perf_destroy_default_spot_handle(&control_sub);
        perf_destroy_default_spot_handle(&pub);
        zlink_spot_node_destroy(&node);
        zlink_spot_node_destroy(&control_node);
        return 1;
    }

    const std::string control_endpoint =
      bind_control_spot_endpoint(control_node, transport);
    if (control_endpoint.empty()) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] control bind failed err="
                      << zlink_errno() << std::endl;
        perf_destroy_default_spot_handle(&control_pub);
        perf_destroy_default_spot_handle(&control_sub);
        perf_destroy_default_spot_handle(&pub);
        zlink_spot_node_destroy(&node);
        zlink_spot_node_destroy(&control_node);
        return 1;
    }
    if (zlink_publish(control_pub, "__warmup__", NULL, 0, 0) != 0) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-server] control warmup failed err="
                      << zlink_errno() << std::endl;
        perf_destroy_default_spot_handle(&control_pub);
        perf_destroy_default_spot_handle(&control_sub);
        perf_destroy_default_spot_handle(&pub);
        zlink_spot_node_destroy(&node);
        zlink_spot_node_destroy(&control_node);
        return 1;
    }

    spot_server_state_t state;
    state.node = node;
    state.pub = pub;
    state.control_node = control_node;
    state.control_pub = control_pub;
    state.control_sub = control_sub;
    state.expected_ready_count = std::max<size_t>(1, settings.clients);
    g_server_state = &state;
    const int connect_ready_timeout_ms = settings.connect_ready_timeout_ms;

    perf_stop_requested ().store(false, std::memory_order_release);
    g_queue_probe_pending.store(false, std::memory_order_release);
    g_queue_probe_size.store(0, std::memory_order_release);
    install_perf_signal_handlers();

    std::thread stdin_watcher([connect_ready_timeout_ms]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-server] stdin line ts_us="
                          << perf_multi_metric::now_us()
                          << " line=" << line << std::endl;
            }
            size_t queue_size = 0;
            size_t start_size = 0;
            std::string connect_endpoint;
            if (parse_queue_probe_command(line, &queue_size)) {
                request_queue_probe(queue_size);
                continue;
            }
            if (parse_connect_command(line, &connect_endpoint)) {
                if (bench_transition_debug_enabled()) {
                    std::cerr << "[multi-spot-server] reverse connect request ts_us="
                              << perf_multi_metric::now_us()
                              << " endpoint=" << connect_endpoint
                              << std::endl;
                }
                (void) register_control_peer_endpoint(g_server_state,
                                                      connect_endpoint);
                if (!connect_control_peer(g_server_state, connect_endpoint)) {
                    if (bench_debug_enabled()) {
                        std::cerr << "[multi-spot-server] reverse connect failed"
                                  << " endpoint=" << connect_endpoint
                                  << " err=" << zlink_errno() << std::endl;
                    }
                    if (g_server_state) {
                        const int err =
                          zlink_errno() != 0 ? zlink_errno() : errno;
                        g_server_state->fatal_errno.store(
                          err != 0 ? err : EIO,
                          std::memory_order_release);
                    }
                    perf_stop_requested ().store(true, std::memory_order_release);
                    return;
                }
                if (!wait_for_control_peer_connection(
                      g_server_state,
                      g_server_state->control_peer_endpoints.size(),
                      connect_ready_timeout_ms)) {
                    if (bench_debug_enabled()) {
                        std::cerr << "[multi-spot-server] reverse connect settle failed"
                                  << " endpoint=" << connect_endpoint
                                  << " err=" << zlink_errno() << std::endl;
                    }
                    if (g_server_state) {
                        const int err =
                          zlink_errno() != 0 ? zlink_errno() : errno;
                        g_server_state->fatal_errno.store(
                          err != 0 ? err : ETIMEDOUT,
                          std::memory_order_release);
                    }
                    perf_stop_requested ().store(true, std::memory_order_release);
                    return;
                }
                std::cout << "CONTROL_CONNECTED," << connect_endpoint
                          << std::endl;
                continue;
            }
            if (parse_start_command(line, &start_size)) {
                notify_size_start(g_server_state, start_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                perf_stop_requested ().store(true, std::memory_order_release);
                return;
            }
        }
    });
    stdin_watcher.detach();

    const bench_multi_cpu_sample_t sample_start =
      bench_multi_capture_cpu_sample();

    std::cout << "READY," << endpoint << std::endl;
    std::cout << "CONTROL_READY," << control_endpoint << std::endl;
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] phase gate open ts_us="
                  << perf_multi_metric::now_us()
                  << std::endl;
    }

    const bool ok =
      run_server_loop(&state, settings, lib_name, transport,
                      msg_sizes);
    if (bench_transition_debug_enabled()) {
        std::cerr << "[multi-spot-server] benchmark done ok=" << (ok ? 1 : 0)
                  << " stop="
                  << (perf_stop_requested ().load(std::memory_order_acquire) ? 1 : 0)
                  << " fatal_errno="
                  << state.fatal_errno.load(std::memory_order_acquire)
                  << std::endl;
    }

    const bool send_pending =
      state.send_pending.load(std::memory_order_acquire);
    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe(sample_start);
    const server_queue_stats_t queue_stats =
      sample_spot_queue_stats(pub, send_pending);
    print_server_metrics(lib_name, transport, msg_sizes, metrics, queue_stats);
    fast_exit_process(ok ? 0 : 1);
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark(lib_name, transport);
}
