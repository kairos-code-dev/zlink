#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_handshake.hpp"
#include "../common/perf_multi_metric_header.hpp"
#include "../common/perf_multi_spot_control.hpp"
#include "../common/perf_multi_spot_handle.hpp"
#include "../common/perf_multi_spot_handshake.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

static const char *k_pattern = "MULTI_SPOT_REQREP";
static const char *k_topic = "bench";
static const char *k_control_ready_prefix = "CLIENT_CONTROL_ENDPOINT,";
static const char *k_server_node_rid_text = "SPOT-REQREP-SERVER-NODE";
static const char *k_server_spot_rid_text = "SPOT-REQREP-SERVER-SPOT";

using perf_multi_client::is_supported_transport;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::print_echo_client_result_lines;
using perf_multi_client::resolve_case_msg_sizes;

static std::atomic<int> g_client_debug_send_logs(0);
static std::atomic<int> g_client_debug_recv_logs(0);
static std::atomic<int> g_client_trace_send_logs(0);
static std::atomic<int> g_client_trace_reply_logs(0);

bool spot_trace_enabled()
{
    static const bool enabled = std::getenv("PERF_MULTI_SPOT_TRACE") != NULL;
    return enabled;
}

size_t active_spot_slot_limit(size_t total_slots, size_t msg_size)
{
    if (msg_size >= 131072)
        return std::min(total_slots, static_cast<size_t>(8));
    if (msg_size >= 65536)
        return std::min(total_slots, static_cast<size_t>(32));
    return total_slots;
}

struct spot_reqrep_client_state_t;
struct spot_reqrep_client_slot_t;

void on_request_reply(zlink_request_result_t result,
                      zlink_msg_t *parts,
                      size_t part_count,
                      void *userdata);

struct spot_reqrep_client_slot_t
{
    spot_reqrep_client_slot_t() :
        owner(NULL),
        socket(NULL),
        monitor(),
        index(0),
        next_seq(1),
        waiting_reply(false),
        send_pending(false),
        last_sent_ts_ns(0),
        payload()
    {
    }

    spot_reqrep_client_slot_t(spot_reqrep_client_slot_t &&other) noexcept :
        owner(other.owner),
        socket(other.socket),
        monitor(other.monitor),
        index(other.index),
        next_seq(other.next_seq),
        waiting_reply(other.waiting_reply.load(std::memory_order_acquire)),
        send_pending(other.send_pending.load(std::memory_order_acquire)),
        last_sent_ts_ns(other.last_sent_ts_ns.load(std::memory_order_acquire)),
        payload(std::move(other.payload))
    {
        other.owner = NULL;
        other.socket = NULL;
        other.monitor = ready_monitor_t();
        other.index = 0;
        other.next_seq = 1;
        other.waiting_reply.store(false, std::memory_order_release);
        other.send_pending.store(false, std::memory_order_release);
        other.last_sent_ts_ns.store(0, std::memory_order_release);
    }

    spot_reqrep_client_slot_t &operator=(spot_reqrep_client_slot_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        owner = other.owner;
        socket = other.socket;
        monitor = other.monitor;
        index = other.index;
        next_seq = other.next_seq;
        waiting_reply.store(other.waiting_reply.load(std::memory_order_acquire),
                            std::memory_order_release);
        send_pending.store(other.send_pending.load(std::memory_order_acquire),
                           std::memory_order_release);
        last_sent_ts_ns.store(other.last_sent_ts_ns.load(std::memory_order_acquire),
                              std::memory_order_release);
        payload = std::move(other.payload);
        other.owner = NULL;
        other.socket = NULL;
        other.monitor = ready_monitor_t();
        other.index = 0;
        other.next_seq = 1;
        other.waiting_reply.store(false, std::memory_order_release);
        other.send_pending.store(false, std::memory_order_release);
        other.last_sent_ts_ns.store(0, std::memory_order_release);
        return *this;
    }

    spot_reqrep_client_slot_t(const spot_reqrep_client_slot_t &) = delete;
    spot_reqrep_client_slot_t &operator=(const spot_reqrep_client_slot_t &) = delete;

    spot_reqrep_client_state_t *owner;
    void *socket;
    ready_monitor_t monitor;
    size_t index;
    uint64_t next_seq;
    std::atomic<bool> waiting_reply;
    std::atomic<bool> send_pending;
    std::atomic<uint64_t> last_sent_ts_ns;
    std::vector<char> payload;
};

struct spot_reqrep_client_state_t
{
    spot_reqrep_client_state_t() :
        data_node(NULL),
        control_node(NULL),
        control_pub(NULL),
        control_sub(NULL),
        poller(NULL),
        control_link_ready(false),
        control_started_msg_size(0),
        fatal(false),
        connected_published(false),
        active_run_id(0),
        active_msg_size(0),
        active_deadline_ns(0),
        active_reply_count(0),
        start_gate(),
        mutex(),
        cv(),
        data_endpoint(),
        control_endpoint(),
        server_control_endpoint(),
        slots(),
        events()
    {
    }

    void *data_node;
    void *control_node;
    void *control_pub;
    void *control_sub;
    void *poller;
    std::atomic<bool> control_link_ready;
    std::atomic<size_t> control_started_msg_size;
    std::atomic<bool> fatal;
    std::atomic<bool> connected_published;
    std::atomic<uint32_t> active_run_id;
    std::atomic<size_t> active_msg_size;
    std::atomic<uint64_t> active_deadline_ns;
    std::atomic<unsigned long long> active_reply_count;
    bench_latency_sampler_t active_latency;
    perf_multi_handshake::start_signal_state_t start_gate;
    std::mutex mutex;
    std::condition_variable cv;
    std::string data_endpoint;
    std::string control_endpoint;
    std::string server_control_endpoint;
    std::vector<spot_reqrep_client_slot_t> slots;
    std::vector<zlink_poller_event_t> events;
};

spot_reqrep_client_state_t *g_client_state = NULL;

void fast_exit_process(int exit_code)
{
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
}

bool parse_control_endpoint_arg(int argc,
                                char **argv,
                                std::string *endpoint_out)
{
    if (!endpoint_out)
        return false;

    endpoint_out->clear();
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--control-endpoint") == 0) {
            *endpoint_out = argv[i + 1];
            return !endpoint_out->empty();
        }
    }

    return false;
}

bool init_routing_id_text(const char *text, zlink_routing_id_t *rid_out)
{
    if (!text || !*text || !rid_out) {
        errno = EINVAL;
        return false;
    }

    const size_t text_len = std::strlen(text);
    if (text_len > sizeof(rid_out->data)) {
        errno = EINVAL;
        return false;
    }

    std::memset(rid_out, 0, sizeof(*rid_out));
    rid_out->size = static_cast<uint8_t>(text_len);
    std::memcpy(rid_out->data, text, text_len);
    return true;
}

std::string bind_client_spot_endpoint(void *node,
                                      const std::string &transport,
                                      size_t slot_index)
{
    if (!node) {
        errno = EINVAL;
        return std::string();
    }

    const int base_bind_port =
      resolve_multi_int_env("PERF_MULTI_CLIENT_BIND_PORT", 0, 0);
    std::string bind_endpoint;
    if (base_bind_port > 0) {
        bind_endpoint = make_fixed_endpoint(
          transport, base_bind_port + static_cast<int>(slot_index));
    } else {
        const int slot_offset = static_cast<int>(slot_index % 32) * 64;
        int range_base_port = 33000 + slot_offset;
#if !defined(_WIN32)
        range_base_port += static_cast<int>(::getpid() % 1000) * 16;
#endif
        bind_endpoint = perf_bind_fixed_endpoint_range(
          node, transport, range_base_port, 64, &perf_bind_spot_node_endpoint);
    }

    if (bind_endpoint.empty()) {
        errno = EINVAL;
        return std::string();
    }

    if (base_bind_port > 0
        && zlink_spot_node_bind(node, bind_endpoint.c_str()) != ZLINK_BIND_OK) {
        return std::string();
    }

    zlink_spot_node_status_t status;
    std::memset(&status, 0, sizeof(status));
    if (zlink_spot_node_status_snapshot(node, &status) != ZLINK_CONFIG_OK
        || status.local_endpoint[0] == '\0') {
        errno = zlink_errno() != 0 ? zlink_errno() : EIO;
        return std::string();
    }

    return perf_normalize_bind_endpoint_host(status.local_endpoint, transport);
}

int resolve_spot_ready_settle_ms()
{
    return resolve_multi_int_env("PERF_MULTI_SPOT_READY_SETTLE_MS", 1000, 0);
}

int resolve_spot_control_settle_ms()
{
    return resolve_multi_int_env("PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25, 0);
}

bool idle_until(const std::chrono::steady_clock::time_point &deadline,
                const spot_reqrep_client_state_t *state)
{
    while (std::chrono::steady_clock::now() < deadline) {
        if (state && state->fatal.load(std::memory_order_acquire)) {
            errno = EIO;
            return false;
        }

        const long wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               deadline - std::chrono::steady_clock::now())
                               .count();
        if (wait_ms <= 0)
            break;
        if (perf_socket_poll(NULL, 0, std::min<long>(wait_ms, 10)) < 0
            && zlink_errno() != EINTR) {
            return false;
        }
    }

    return !(state && state->fatal.load(std::memory_order_acquire));
}

bool wait_for_ready_settle(spot_reqrep_client_state_t *state)
{
    const int settle_ms = resolve_spot_ready_settle_ms();
    if (settle_ms <= 0)
        return true;

    return idle_until(std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(settle_ms),
                      state);
}

bool wait_for_control_settle(spot_reqrep_client_state_t *state)
{
    const int settle_ms = resolve_spot_control_settle_ms();
    if (settle_ms <= 0)
        return true;

    return idle_until(std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(settle_ms),
                      state);
}

void destroy_client_state(spot_reqrep_client_state_t *state)
{
    if (!state)
        return;

    for (size_t i = 0; i < state->slots.size(); ++i) {
        close_ready_monitor(state->slots[i].monitor);
        if (state->slots[i].socket) {
            perf_destroy_default_spot_handle(&state->slots[i].socket);
            state->slots[i].socket = NULL;
        }
        state->slots[i].payload.clear();
    }
    state->slots.clear();
    state->events.clear();

    if (state->poller)
        zlink_poller_destroy(&state->poller);
    if (state->control_pub)
        perf_destroy_default_spot_handle(&state->control_pub);
    if (state->control_sub)
        perf_destroy_default_spot_handle(&state->control_sub);
    if (state->control_node)
        zlink_spot_node_destroy(&state->control_node);
    if (state->data_node)
        zlink_spot_node_destroy(&state->data_node);
    state->data_endpoint.clear();
    state->control_endpoint.clear();
    state->server_control_endpoint.clear();
}

bool recv_one_control_message(spot_reqrep_client_state_t *state, bool *received)
{
    if (!state)
        return false;

    std::string payload;
    if (!perf_multi_spot_control::receive_control_payload(
          state->control_sub, k_topic, &payload, received)) {
        return false;
    }

    size_t started_size = 0;
    if (perf_multi_spot_handshake::parse_control_connected(
          payload.data(), payload.size())) {
        state->control_link_ready.store(true, std::memory_order_release);
        state->cv.notify_all();
        return true;
    }
    if (perf_multi_spot_handshake::parse_start_command(
          payload.data(), payload.size(), &started_size)) {
        state->control_started_msg_size.store(started_size,
                                              std::memory_order_release);
        state->cv.notify_all();
    }
    return true;
}

bool wait_for_control_link_ready(spot_reqrep_client_state_t *state, int timeout_ms)
{
    return perf_multi_spot_control::wait_for_control_link_ready(
      state, timeout_ms, recv_one_control_message);
}

bool wait_for_started_size(spot_reqrep_client_state_t *state,
                           size_t msg_size,
                           int timeout_ms)
{
    return perf_multi_spot_control::wait_for_started_size(
      state, msg_size, timeout_ms, recv_one_control_message);
}

bool wait_for_runner_start(spot_reqrep_client_state_t *state,
                           size_t msg_size,
                           int timeout_ms)
{
    if (!state)
        return false;

    return perf_multi_handshake::wait_for_start(
      &state->start_gate, msg_size, timeout_ms);
}

bool publish_connected_once(spot_reqrep_client_state_t *state)
{
    if (!state)
        return false;
    if (state->connected_published.load(std::memory_order_acquire))
        return true;

    if (!perf_multi_spot_control::publish_connected(state->control_pub, k_topic)) {
        return false;
    }
    state->connected_published.store(true, std::memory_order_release);
    return true;
}

bool publish_ready_count(spot_reqrep_client_state_t *state, size_t msg_size)
{
    if (!state || msg_size == 0 || state->slots.empty()) {
        errno = EINVAL;
        return false;
    }

    return perf_multi_spot_control::publish_ready_count(
      state->control_pub, k_topic, msg_size, state->slots.size());
}

bool publish_data_endpoint(spot_reqrep_client_state_t *state)
{
    if (!state || state->data_endpoint.empty()) {
        errno = EINVAL;
        return false;
    }

    return perf_multi_spot_control::publish_data_endpoint(
      state->control_pub, k_topic, state->data_endpoint);
}

bool wait_for_data_link_ready(spot_reqrep_client_state_t *state,
                              const multi_bench_settings_t &settings)
{
    if (!state || !state->data_node) {
        errno = EINVAL;
        return false;
    }

    return perf_multi_spot_control::wait_for_connected_peer_count(
      state->data_node, 1, settings.connect_ready_timeout_ms, NULL);
}

bool complete_ready_barrier(spot_reqrep_client_state_t *state,
                            const multi_bench_settings_t &settings,
                            size_t msg_size)
{
    if (!state || msg_size == 0) {
        errno = EINVAL;
        return false;
    }

    if (!wait_for_control_link_ready(state, settings.connect_ready_timeout_ms)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] control link ready timeout size="
                      << msg_size << std::endl;
        return false;
    }
    if (!wait_for_ready_settle(state)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] ready settle failed size="
                      << msg_size << " err=" << zlink_errno() << std::endl;
        return false;
    }
    if (!publish_data_endpoint(state)) {
        if (bench_debug_enabled())
            std::cerr
              << "[multi-spot-reqrep-client] publish DATA_ENDPOINT failed size="
              << msg_size << " err=" << zlink_errno() << std::endl;
        return false;
    }
    if (!wait_for_control_settle(state)) {
        if (bench_debug_enabled())
            std::cerr
              << "[multi-spot-reqrep-client] data endpoint settle failed size="
              << msg_size << " err=" << zlink_errno() << std::endl;
        return false;
    }
    if (!wait_for_data_link_ready(state, settings)) {
        if (bench_debug_enabled())
            std::cerr
              << "[multi-spot-reqrep-client] data link ready timeout size="
              << msg_size << " err=" << zlink_errno() << std::endl;
        return false;
    }
    if (!publish_connected_once(state)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] publish CONNECTED failed size="
                      << msg_size << " err=" << zlink_errno() << std::endl;
        return false;
    }
    if (!wait_for_control_settle(state)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] control settle failed size="
                      << msg_size << " err=" << zlink_errno() << std::endl;
        return false;
    }
    if (!publish_ready_count(state, msg_size)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] publish READY_COUNT failed size="
                      << msg_size << " err=" << zlink_errno() << std::endl;
        return false;
    }

    std::cout << "CLIENT_READY," << msg_size << std::endl;
    return true;
}

bool create_control_spot(ctx_guard_t &ctx,
                         const std::string &transport,
                         const std::string &server_control_endpoint,
                         const multi_bench_settings_t &settings,
                         size_t max_msg_size,
                         spot_reqrep_client_state_t *state)
{
    if (!state || server_control_endpoint.empty()) {
        errno = EINVAL;
        return false;
    }

    state->control_node = zlink_spot_node_new(ctx.get(), NULL);
    if (!state->control_node
        || !setup_tls_server(state->control_node, transport)
        || !setup_tls_client(state->control_node, transport)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] control node init failed err="
                      << zlink_errno() << std::endl;
        return false;
    }
    if (!apply_benchmark_context_auto_hwm_msg_unit(ctx.get(), max_msg_size))
        return false;

    state->control_pub = perf_create_default_spot_handle(state->control_node);
    state->control_sub = perf_create_default_spot_handle(state->control_node);
    if (!state->control_pub || !state->control_sub
        || !perf_multi_spot_control::apply_control_options(
             state->control_pub, state->control_sub, settings)) {
        if (bench_debug_enabled())
            std::cerr
              << "[multi-spot-reqrep-client] control spot init failed err="
              << zlink_errno() << std::endl;
        return false;
    }

    state->control_endpoint =
      bind_client_spot_endpoint(state->control_node, transport, 10000);
    if (state->control_endpoint.empty()) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] control bind failed err="
                      << zlink_errno() << std::endl;
        return false;
    }
    state->server_control_endpoint = server_control_endpoint;

    if (zlink_spot_node_connect_peer(state->control_node,
                                     server_control_endpoint.c_str())
        != ZLINK_CONNECT_OK
        || zlink_set_subscription(state->control_sub, k_topic)
             != ZLINK_CONFIG_OK) {
        if (bench_debug_enabled())
            std::cerr
              << "[multi-spot-reqrep-client] control connect/subscribe failed err="
              << zlink_errno() << std::endl;
        return false;
    }

    std::cout << k_control_ready_prefix << state->control_endpoint << std::endl;
    return true;
}

bool create_spot_slots(ctx_guard_t &ctx,
                       const std::string &transport,
                       const std::string &endpoint,
                       const multi_bench_settings_t &settings,
                       size_t max_msg_size,
                       spot_reqrep_client_state_t *state)
{
    if (!state || endpoint.empty()) {
        errno = EINVAL;
        return false;
    }

    state->data_node = zlink_spot_node_new(ctx.get(), NULL);
    if (!state->data_node
        || !setup_tls_server(state->data_node, transport)
        || !setup_tls_client(state->data_node, transport)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] data node init failed err="
                      << zlink_errno() << std::endl;
        return false;
    }
    if (!apply_benchmark_context_auto_hwm_msg_unit(ctx.get(), max_msg_size))
        return false;

    const std::string local_endpoint =
      bind_client_spot_endpoint(state->data_node, transport, 0);
    if (local_endpoint.empty()) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] data node bind failed err="
                      << zlink_errno() << std::endl;
        return false;
    }
    if (zlink_spot_node_connect_peer(state->data_node, endpoint.c_str())
        != ZLINK_CONNECT_OK) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] data node connect failed err="
                      << zlink_errno() << std::endl;
        return false;
    }
    state->data_endpoint = local_endpoint;

    state->slots.resize(settings.clients);
    for (size_t i = 0; i < state->slots.size(); ++i) {
        spot_reqrep_client_slot_t &slot = state->slots[i];
        slot.owner = state;
        slot.index = i;
        slot.socket = perf_create_default_spot_handle(state->data_node);
        if (!slot.socket) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-reqrep-client] spot create failed slot="
                          << i << " err=" << zlink_errno() << std::endl;
            return false;
        }

        apply_benchmark_socket_options(
          slot.socket, settings.hwm, transport, ZLINK_SOCKET_DEALER,
          max_msg_size);
        const std::string routing_id = std::string("SPOT-REQREP-")
                                       + std::to_string(i);
        if (zlink_set_routing_id(
              slot.socket, routing_id.c_str(), routing_id.size())
            != 0) {
            if (bench_debug_enabled())
                std::cerr
                  << "[multi-spot-reqrep-client] spot routing id init failed slot="
                  << i << " err=" << zlink_errno() << std::endl;
            return false;
        }

    }

    perf_print_auto_hwm_snapshot(
      state->data_node, true, "spotnode_data_pub", transport);
    if (!state->slots.empty()) {
        perf_print_auto_hwm_snapshot(
          state->slots[0].socket, true, "spotend_data_pub", transport);
    }

    state->poller = zlink_poller_new();
    if (!state->poller) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] poller create failed err="
                      << zlink_errno() << std::endl;
        return false;
    }

    for (size_t i = 0; i < state->slots.size(); ++i) {
        if (zlink_poller_add(
              state->poller, state->slots[i].socket, &state->slots[i], ZLINK_POLLIN)
            != 0) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-reqrep-client] poller add failed slot="
                          << i << " err=" << zlink_errno() << std::endl;
            return false;
        }
    }

    state->events.resize(state->slots.size());
    return wait_for_ready_settle(state);
}

enum send_result_t
{
    send_result_ok = 0,
    send_result_blocked = 1,
    send_result_not_connected = 2,
    send_result_error = 3
};

void on_request_reply(zlink_request_result_t result,
                      zlink_msg_t *parts,
                      size_t part_count,
                      void *userdata)
{
    spot_reqrep_client_slot_t *slot =
      static_cast<spot_reqrep_client_slot_t *>(userdata);
    if (!slot || !slot->owner)
        return;

    spot_reqrep_client_state_t *state = slot->owner;
    slot->waiting_reply.store(false, std::memory_order_release);
    slot->send_pending.store(false, std::memory_order_release);

    if (result != ZLINK_REQUEST_OK || !parts || part_count == 0)
        return;

    perf_multi_metric::header_t header;
    if (!perf_multi_metric::decode_payload_header(
          zlink_msg_data(&parts[0]), zlink_msg_size(&parts[0]), &header)
        || !perf_multi_metric::is_expected(
          header,
          state->active_run_id.load(std::memory_order_acquire),
          perf_multi_metric::phase_active,
          state->active_msg_size.load(std::memory_order_acquire))) {
        return;
    }

    const uint64_t now_ns = perf_multi_metric::now_ns();
    if (now_ns >= state->active_deadline_ns.load(std::memory_order_acquire)
        || header.sent_ts_ns <= 0
        || now_ns < static_cast<uint64_t>(header.sent_ts_ns)) {
        return;
    }

    const double sample_ns =
      static_cast<double>(now_ns - static_cast<uint64_t>(header.sent_ts_ns))
      / 2.0;
    state->active_reply_count.fetch_add(1, std::memory_order_acq_rel);
    state->active_latency.add(sample_ns);
    if (spot_trace_enabled()
        && slot->index == 0
        && g_client_trace_reply_logs.fetch_add(1, std::memory_order_acq_rel) < 8) {
        std::cerr << "[multi-spot-reqrep-trace] reply slot=" << slot->index
                  << " seq=" << header.seq
                  << " sample_ms=" << (sample_ns / 1000000.0)
                  << " rtt_ms="
                  << ((now_ns - static_cast<uint64_t>(header.sent_ts_ns))
                      / 1000000.0)
                  << std::endl;
    }
    if (bench_debug_enabled()
        && g_client_debug_recv_logs.fetch_add(1, std::memory_order_acq_rel)
             < 8) {
        std::cerr << "[multi-spot-reqrep-client] recv reply slot="
                  << slot->index << " seq=" << header.seq << std::endl;
    }
}

send_result_t send_request(spot_reqrep_client_slot_t *slot,
                           const zlink_routing_id_t *server_node_rid,
                           const zlink_routing_id_t *server_spot_rid,
                           uint32_t run_id,
                           size_t msg_size,
                           uint32_t timeout_ms)
{
    if (!slot || !slot->owner || !slot->socket || !server_node_rid
        || !server_spot_rid) {
        errno = EINVAL;
        return send_result_error;
    }

    spot_reqrep_client_state_t *state = slot->owner;
    const size_t payload_size =
      std::max(msg_size, perf_multi_metric::header_size());
    if (slot->payload.size() != payload_size)
        slot->payload.resize(payload_size, 'c');
    const uint64_t sent_ts_ns = perf_multi_metric::now_ns();
    if (!perf_multi_metric::stamp_payload(slot->payload.data(),
                                          payload_size,
                                          run_id,
                                          perf_multi_metric::phase_active,
                                          msg_size,
                                          slot->next_seq,
                                          sent_ts_ns)) {
        return send_result_error;
    }

    zlink_msg_t part;
    if (zlink_msg_init_data(&part,
                            payload_size > 0
                              ? static_cast<void *>(slot->payload.data())
                              : NULL,
                            payload_size,
                            NULL,
                            NULL)
        != 0) {
        return send_result_error;
    }

    slot->waiting_reply.store(true, std::memory_order_release);
    slot->last_sent_ts_ns.store(sent_ts_ns, std::memory_order_release);

    const zlink_submit_result_t rc = perf_zlink_spot_request_spot_parts(
      slot->socket,
      server_node_rid,
      server_spot_rid,
      &part,
      1,
      on_request_reply,
      slot,
      ZLINK_DONTWAIT,
      timeout_ms);
    if (rc != ZLINK_SUBMIT_OK) {
        slot->waiting_reply.store(false, std::memory_order_release);
        zlink_msg_close(&part);
    }

    switch (rc) {
        case ZLINK_SUBMIT_OK:
            if (spot_trace_enabled()
                && slot->index == 0
                && g_client_trace_send_logs.fetch_add(1, std::memory_order_acq_rel)
                     < 8) {
                std::cerr << "[multi-spot-reqrep-trace] send slot=" << slot->index
                          << " seq=" << slot->next_seq
                          << " timeout_ms=" << timeout_ms << std::endl;
            }
            if (bench_debug_enabled()
                && g_client_debug_send_logs.fetch_add(1, std::memory_order_acq_rel)
                     < 8) {
                std::cerr << "[multi-spot-reqrep-client] send ok slot="
                          << slot->index << " seq=" << slot->next_seq
                          << std::endl;
            }
            slot->send_pending.store(false, std::memory_order_release);
            ++slot->next_seq;
            return send_result_ok;
        case ZLINK_SUBMIT_BACKPRESSURED:
            slot->send_pending.store(false, std::memory_order_release);
            return send_result_blocked;
        case ZLINK_SUBMIT_NOT_CONNECTED:
            if (bench_debug_enabled()
                && g_client_debug_send_logs.fetch_add(1, std::memory_order_acq_rel)
                     < 8) {
                std::cerr << "[multi-spot-reqrep-client] send not connected slot="
                          << slot->index << std::endl;
            }
            return send_result_not_connected;
        default:
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-reqrep-client] send failed slot="
                          << slot->index << " rc=" << rc
                          << " err=" << zlink_errno() << std::endl;
            }
            return send_result_error;
    }
}

bool reset_reqrep_poller(spot_reqrep_client_state_t *state)
{
    if (!state || !state->poller) {
        errno = EINVAL;
        return false;
    }

    for (size_t i = 0; i < state->slots.size(); ++i) {
        if (zlink_poller_modify(state->poller, state->slots[i].socket, ZLINK_POLLIN)
            != 0) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-reqrep-client] poller reset failed slot="
                          << i << " err=" << zlink_errno() << std::endl;
            }
            return false;
        }
    }

    return true;
}

void reset_active_slot(spot_reqrep_client_slot_t *slot)
{
    if (!slot)
        return;
    slot->waiting_reply.store(false, std::memory_order_release);
    slot->send_pending.store(false, std::memory_order_release);
    slot->next_seq = 1;
    slot->payload.clear();
}

bool try_submit_ready_requests(spot_reqrep_client_state_t *client_state,
                               const zlink_routing_id_t *server_node_rid,
                               const zlink_routing_id_t *server_spot_rid,
                               uint32_t run_id,
                               size_t msg_size,
                               uint32_t request_timeout_ms,
                               bool *send_progress_out,
                               bool *has_waiting_reply_out)
{
    if (!client_state || !server_node_rid || !server_spot_rid
        || !send_progress_out || !has_waiting_reply_out) {
        errno = EINVAL;
        return false;
    }

    *send_progress_out = false;
    *has_waiting_reply_out = false;
    const size_t active_slots =
      active_spot_slot_limit(client_state->slots.size(), msg_size);
    for (size_t i = 0; i < active_slots; ++i) {
        spot_reqrep_client_slot_t &slot = client_state->slots[i];
        const bool waiting_reply =
          slot.waiting_reply.load(std::memory_order_acquire);
        const bool send_pending =
          slot.send_pending.load(std::memory_order_acquire);
        if (waiting_reply || send_pending) {
            *has_waiting_reply_out = true;
            continue;
        }

        const send_result_t send_rc =
          send_request(&slot,
                       server_node_rid,
                       server_spot_rid,
                       run_id,
                       msg_size,
                       request_timeout_ms);
        if (send_rc == send_result_ok) {
            *send_progress_out = true;
        } else if (send_rc == send_result_blocked) {
            if (bench_debug_enabled()
                && g_client_debug_send_logs.fetch_add(
                     1, std::memory_order_acq_rel)
                     < 8) {
                std::cerr << "[multi-spot-reqrep-client] send blocked slot="
                          << slot.index << std::endl;
            }
        } else if (send_rc != send_result_not_connected) {
            if (bench_debug_enabled()) {
                std::cerr
                  << "[multi-spot-reqrep-client] send request fatal slot="
                  << slot.index << " err=" << zlink_errno() << std::endl;
            }
            return false;
        }
    }
    return true;
}

bool run_active_window(spot_reqrep_client_state_t *state,
                       const multi_bench_settings_t &settings,
                       uint32_t run_id,
                       size_t msg_size,
                       unsigned long long *reply_count_out,
                       bench_latency_stats_t *latency_out)
{
    if (!state || !reply_count_out || !latency_out || !state->poller) {
        errno = EINVAL;
        return false;
    }

    *reply_count_out = 0;
    *latency_out = bench_latency_stats_t();

    zlink_routing_id_t server_node_rid;
    zlink_routing_id_t server_spot_rid;
    if (!init_routing_id_text(k_server_node_rid_text, &server_node_rid)
        || !init_routing_id_text(k_server_spot_rid_text, &server_spot_rid)) {
        return false;
    }

    state->active_run_id.store(run_id, std::memory_order_release);
    state->active_msg_size.store(msg_size, std::memory_order_release);
    state->active_deadline_ns.store(
      perf_multi_metric::now_ns()
        + static_cast<uint64_t>(std::max(1, settings.duration_seconds))
            * 1000000000ULL,
      std::memory_order_release);
    state->active_reply_count.store(0, std::memory_order_release);
    state->active_latency.reset();
    for (size_t i = 0; i < state->slots.size(); ++i) {
        reset_active_slot(&state->slots[i]);
    }

    if (!reset_reqrep_poller(state))
        return false;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::seconds(std::max(1, settings.duration_seconds));
    const uint32_t request_timeout_ms =
      static_cast<uint32_t>(bench_timeout_ms_from_env(
        "PERF_MULTI_RCVTIMEO_MS",
        bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200)));

    while (std::chrono::steady_clock::now() < deadline) {
        bool send_progress = false;
        bool has_waiting_reply = false;
        if (!try_submit_ready_requests(state,
                                       &server_node_rid,
                                       &server_spot_rid,
                                       run_id,
                                       msg_size,
                                       request_timeout_ms,
                                       &send_progress,
                                       &has_waiting_reply))
            return false;
        if (send_progress)
            continue;

        const int event_count = zlink_poller_wait(
          state->poller,
          state->events.empty() ? NULL : &state->events[0],
          static_cast<int>(state->events.size()),
          -1,
          NULL);
        if (event_count < 0) {
            if (zlink_errno() == EINTR)
                continue;
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-reqrep-client] poller wait failed err="
                          << zlink_errno() << std::endl;
            return false;
        }
    }

    *reply_count_out =
      state->active_reply_count.load(std::memory_order_acquire);
    *latency_out = state->active_latency.snapshot();
    return true;
}

bool run_single_size_case(spot_reqrep_client_state_t *state,
                          void *ctx,
                          const multi_bench_settings_t &settings,
                          const std::string &lib_name,
                          const std::string &transport,
                          size_t msg_size,
                          uint32_t run_id)
{
    if (!apply_benchmark_context_auto_hwm_msg_unit(ctx, msg_size))
        return false;
    const int phase_timeout_ms =
      std::max(settings.connect_ready_timeout_ms,
               std::max(1000, settings.connect_ready_timeout_ms * 6));

    state->control_started_msg_size.store(0, std::memory_order_release);
    if (!complete_ready_barrier(state, settings, msg_size))
        return false;

    if (!wait_for_runner_start(state, msg_size, phase_timeout_ms)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] runner start timeout size="
                      << msg_size << std::endl;
        return false;
    }
    if (!wait_for_started_size(state, msg_size, phase_timeout_ms)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] control START timeout size="
                      << msg_size << std::endl;
        return false;
    }

    unsigned long long reply_count = 0;
    bench_latency_stats_t latency;
    if (!run_active_window(state,
                           settings,
                           run_id,
                           msg_size,
                           &reply_count,
                           &latency)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] active window failed size="
                      << msg_size << " err=" << zlink_errno() << std::endl;
        return false;
    }
    if (reply_count == 0 || latency.mean_ns <= 0.0) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] metrics invalid size="
                      << msg_size << " replies=" << reply_count
                      << " latency_mean=" << latency.mean_ns << std::endl;
        return false;
    }

    const double throughput =
      static_cast<double>(reply_count)
      / static_cast<double>(std::max(1, settings.duration_seconds));
    print_echo_client_result_lines(
      k_pattern, lib_name, transport, msg_size, throughput, latency);
    return true;
}

int run_client_benchmark(const std::string &lib_name,
                         const std::string &transport,
                         const std::string &endpoint,
                         const std::string &control_endpoint,
                         size_t fallback_size)
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

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes(fallback_size);
    size_t max_msg_size = fallback_size > 0 ? fallback_size : 64;
    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (msg_sizes[i] > max_msg_size)
            max_msg_size = msg_sizes[i];
    }
    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    spot_reqrep_client_state_t state;
    g_client_state = &state;
    if (!create_control_spot(
          ctx, transport, control_endpoint, settings, max_msg_size, &state)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] create_control_spot failed"
                      << std::endl;
        destroy_client_state(&state);
        return 1;
    }
    perf_multi_spot_control::start_client_stdin_watcher(
      &state,
      [](spot_reqrep_client_state_t *client_state, size_t start_size) {
          perf_multi_handshake::signal_start(
            &client_state->start_gate, start_size);
      });
    if (!create_spot_slots(
          ctx, transport, endpoint, settings, max_msg_size, &state)) {
        if (bench_debug_enabled())
            std::cerr << "[multi-spot-reqrep-client] create_spot_slots failed"
                      << std::endl;
        fast_exit_process(1);
    }

    int rc = 0;
    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-reqrep-client] size begin="
                      << msg_sizes[i] << std::endl;
        }
        if (!run_single_size_case(&state,
                                  ctx.get (),
                                  settings,
                                  lib_name,
                                  transport,
                                  msg_sizes[i],
                                  static_cast<uint32_t>(i + 1))) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-reqrep-client] size failed="
                          << msg_sizes[i] << " err=" << zlink_errno()
                          << std::endl;
            }
            rc = 1;
            break;
        }
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-reqrep-client] size done="
                      << msg_sizes[i] << std::endl;
        }
    }

    fast_exit_process(rc);
    return rc;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern(k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t>(std::strtoull(argv[3], NULL, 10));

    std::string endpoint;
    std::string control_endpoint;
    if (!parse_endpoint_arg(argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }
    if (!parse_control_endpoint_arg(argc, argv, &control_endpoint)) {
        std::cerr << "missing --control-endpoint" << std::endl;
        return 1;
    }

    return run_client_benchmark(
      lib_name, transport, endpoint, control_endpoint, fallback_size);
}
