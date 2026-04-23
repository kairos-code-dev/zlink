#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../common/perf_multi_handshake.hpp"
#include "../common/perf_multi_spot_control.hpp"
#include "../common/perf_multi_spot_handle.hpp"
#include "../common/perf_multi_spot_handshake.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_SPOT_SENDSEND";
static const char *k_topic = "bench";
static const char *k_server_node_rid = "SPOT-SENDSEND-SERVER-NODE";
static const char *k_server_spot_rid = "SPOT-SENDSEND-SERVER-SPOT";

using perf_multi_client::is_supported_transport;

static std::atomic<int> g_server_debug_recv_logs(0);

struct spot_reqrep_server_state_t
{
    spot_reqrep_server_state_t() :
        node(NULL),
        pub(NULL),
        control_node(NULL),
        control_pub(NULL),
        control_sub(NULL),
        expected_ready_count(1),
        fatal_errno(0),
        control_connected(false),
        start_gate(),
        ready_state(),
        control_peers(),
        data_peers(),
        recv_stop(false)
    {
    }

    void *node;
    void *pub;
    void *control_node;
    void *control_pub;
    void *control_sub;
    size_t expected_ready_count;
    std::atomic<int> fatal_errno;
    std::atomic<bool> control_connected;
    perf_multi_handshake::start_signal_state_t start_gate;
    perf_multi_spot_handshake::ready_state_t ready_state;
    perf_multi_spot_handshake::peer_registry_t control_peers;
    perf_multi_spot_handshake::peer_registry_t data_peers;
    std::atomic<bool> recv_stop;
    std::thread recv_thread;
};

bool accept_ready_barrier_payload(spot_reqrep_server_state_t *state,
                                  const std::string &payload)
{
    if (!state || payload.empty()) {
        errno = EINVAL;
        return false;
    }

    size_t ready_size = 0;
    size_t ready_count = 0;
    size_t slot_index = 0;
    std::string data_endpoint;

    if (perf_multi_spot_handshake::parse_control_connected(
          payload.data(), payload.size())) {
        state->control_connected.store(true, std::memory_order_release);
        return true;
    }

    if (perf_multi_spot_handshake::parse_data_endpoint_command(
          payload.data(), payload.size(), &data_endpoint)) {
        return perf_multi_spot_handshake::register_peer(&state->data_peers,
                                                        data_endpoint)
               && perf_multi_spot_control::ensure_connected_peers(
                 state->node, state->data_peers);
    }

    if (perf_multi_spot_handshake::parse_ready_count_command(payload.data(),
                                                             payload.size(),
                                                             &ready_size,
                                                             &ready_count)) {
        perf_multi_spot_handshake::record_ready_count(
          &state->ready_state, ready_size, ready_count);
        return true;
    }

    if (perf_multi_spot_handshake::parse_ready_slot_command(payload.data(),
                                                            payload.size(),
                                                            &ready_size,
                                                            &slot_index)) {
        perf_multi_spot_handshake::record_ready_slot(
          &state->ready_state, ready_size, slot_index);
    }

    return true;
}

bool ready_barrier_satisfied(spot_reqrep_server_state_t *state,
                             size_t msg_size)
{
    return state
           && state->control_connected.load(std::memory_order_acquire)
           && perf_multi_spot_handshake::ready_units(
                &state->ready_state,
                msg_size)
                >= state->expected_ready_count;
}

bool wait_for_data_peers_ready(spot_reqrep_server_state_t *state,
                               int timeout_ms)
{
    if (!state || !state->node) {
        errno = EINVAL;
        return false;
    }

    if (std::getenv("ZLINK_ENABLE_SPOT_DIRECT_ROUTE") != NULL)
        return true;

    const size_t expected_connected =
      std::max<size_t>(1, state->data_peers.endpoints.size());
    return perf_multi_spot_control::wait_for_connected_peer_count(
      state->node, expected_connected, timeout_ms, &state->fatal_errno);
}

spot_reqrep_server_state_t *g_server_state = NULL;

void fast_exit_process(int exit_code)
{
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
}

bool set_routing_id_text(void *handle, const char *text)
{
    return handle && text && *text
           && zlink_set_routing_id(handle, text, std::strlen(text)) == 0;
}

bool apply_replier_options(void *spot,
                           const multi_bench_settings_t &settings)
{
    if (!spot)
        return false;

    apply_benchmark_socket_options(spot, settings.hwm, "tcp");
    return true;
}

bool initialize_reqrep_server_session(
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &token,
  const multi_bench_settings_t &settings,
  perf_multi_spot_control::server_session_t *session)
{
    if (!session) {
        errno = EINVAL;
        return false;
    }

    perf_multi_spot_control::destroy_server_session(session);
    session->node = zlink_spot_node_new(ctx.get());
    session->control_node = zlink_spot_node_new(ctx.get());
    if (!session->node || !session->control_node
        || !setup_tls_server(session->node, transport)
        || !setup_tls_client(session->node, transport)
        || !setup_tls_server(session->control_node, transport)
        || !setup_tls_client(session->control_node, transport)) {
        perf_multi_spot_control::destroy_server_session(session);
        return false;
    }

    session->pub = perf_create_default_spot_handle(session->node);
    session->control_pub =
      perf_create_default_spot_handle(session->control_node);
    session->control_sub =
      perf_create_default_spot_handle(session->control_node);
    if (!session->pub || !session->control_pub || !session->control_sub
        || !set_routing_id_text(session->node, k_server_node_rid)
        || !set_routing_id_text(session->pub, k_server_spot_rid)
        || !apply_replier_options(session->pub, settings)
        || !perf_multi_spot_control::apply_control_options(
             session->control_pub, session->control_sub, settings)
        || zlink_set_subscription(session->control_sub, k_topic)
             != ZLINK_CONFIG_OK) {
        perf_multi_spot_control::destroy_server_session(session);
        return false;
    }

    session->data_endpoint =
      perf_multi_spot_control::bind_data_endpoint(session->node, transport, token);
    if (session->data_endpoint.empty()) {
        perf_multi_spot_control::destroy_server_session(session);
        return false;
    }

    session->control_endpoint =
      perf_multi_spot_control::bind_control_endpoint(
        session->control_node, transport);
    if (session->control_endpoint.empty()) {
        perf_multi_spot_control::destroy_server_session(session);
        return false;
    }

    return true;
}

void fail_server(spot_reqrep_server_state_t *state, int err)
{
    if (!state)
        return;

    const int saved_errno = err != 0 ? err : EIO;
    state->fatal_errno.store(saved_errno, std::memory_order_release);
    perf_stop_requested().store(true, std::memory_order_release);
}

bool echo_routed_payload(void *spot,
                         spot_reqrep_server_state_t *state,
                         const zlink_routing_id_t *source_rid,
                         const zlink_routing_id_t *source_spot_rid,
                         zlink_msg_t *parts,
                         size_t part_count)
{
    if (!spot || !state || !source_rid || !source_spot_rid || !parts
        || part_count == 0) {
        errno = EINVAL;
        return false;
    }

    zlink_submit_result_t submit_rc =
      zlink_spot_send_spot(
        spot, source_rid, source_spot_rid, parts, part_count, ZLINK_DONTWAIT);
    if (submit_rc == ZLINK_SUBMIT_BACKPRESSURED) {
        submit_rc = zlink_spot_send_spot(
          spot,
          source_rid,
          source_spot_rid,
          parts,
          part_count,
          ZLINK_SEND_FLAGS_NONE);
    }
    if (submit_rc == ZLINK_SUBMIT_OK)
        return true;

    if (bench_debug_enabled()) {
        std::cerr << "[multi-spot-sendsend-server] echo send failed rc="
                  << submit_rc << " err=" << zlink_errno() << std::endl;
    }
    zlink_multipart_close(parts, part_count);
    fail_server(state, zlink_errno());
    return false;
}

void drain_spot_routed_recv(void *spot, spot_reqrep_server_state_t *state)
{
    if (!state || !spot)
        return;

    for (;;) {
        const zlink_routing_id_t *source_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;

        const zlink_recv_result_t rc =
          zlink_spot_recv(spot,
                          &source_rid,
                          &source_spot_rid,
                          &request_seq,
                          &parts,
                          &part_count,
                          static_cast<zlink_recv_flags_t>(ZLINK_DONTWAIT));
        const int saved_errno = zlink_errno();
        if (rc == ZLINK_RECV_NO_DATA
            && (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK)) {
            return;
        }
        if (rc != ZLINK_RECV_OK) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-sendsend-server] recv failed err="
                          << saved_errno << std::endl;
            }
            fail_server(state, saved_errno);
            return;
        }

        if (!source_rid || source_rid->size == 0
            || !source_spot_rid || source_spot_rid->size == 0
            || !parts || part_count == 0) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-sendsend-server] invalid routed recv"
                          << " rid=" << (source_rid ? source_rid->size : 0)
                          << " spot="
                          << (source_spot_rid ? source_spot_rid->size : 0)
                          << " seq=" << request_seq
                          << " parts=" << part_count << std::endl;
            }
            zlink_multipart_close(parts, part_count);
            fail_server(state, EPROTO);
            return;
        }
        if (bench_debug_enabled()
            && g_server_debug_recv_logs.fetch_add(1, std::memory_order_acq_rel)
                 < 8) {
            std::cerr << "[multi-spot-sendsend-server] recv payload seq="
                      << request_seq << " parts=" << part_count << std::endl;
        }

        if (!echo_routed_payload(
              spot, state, source_rid, source_spot_rid, parts, part_count)) {
            return;
        }
    }
}

void on_spot_dispatch(void *spot,
                      const zlink_spot_dispatch_info_t *info,
                      void *userdata)
{
    spot_reqrep_server_state_t *state =
      static_cast<spot_reqrep_server_state_t *>(userdata);
    if (!state || !info
        || info->event != ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE) {
        return;
    }
    drain_spot_routed_recv(spot, state);
}

void spot_recv_worker_main(spot_reqrep_server_state_t *state)
{
    if (!state || !state->pub) {
        if (state)
            fail_server(state, EFAULT);
        return;
    }

    while (!state->recv_stop.load(std::memory_order_acquire)
           && !perf_stop_requested().load(std::memory_order_acquire)
           && state->fatal_errno.load(std::memory_order_acquire) == 0) {
        drain_spot_routed_recv(state->pub, state);
        if (state->fatal_errno.load(std::memory_order_acquire) != 0)
            break;
        if (perf_socket_poll(NULL, 0, 1) < 0 && zlink_errno() != EINTR) {
            fail_server(state, zlink_errno());
            break;
        }
    }
}

bool wait_for_ready_barrier(spot_reqrep_server_state_t *state,
                            size_t msg_size,
                            int timeout_ms)
{
    if (!state || !state->control_sub || msg_size == 0) {
        errno = EINVAL;
        return false;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        bool received = false;
        std::string payload;
        if (!perf_multi_spot_control::receive_control_payload(
              state->control_sub, k_topic, &payload, &received)) {
            return false;
        }

        if (received && !payload.empty()
            && !accept_ready_barrier_payload(state, payload)) {
            return false;
        }

        if (perf_stop_requested().load(std::memory_order_acquire)
            || state->fatal_errno.load(std::memory_order_acquire) != 0) {
            errno = EIO;
            return false;
        }

        if (ready_barrier_satisfied(state, msg_size)) {
            return true;
        }

        if (!perf_multi_spot_control::wait_for_control_readable(
              state->control_sub, deadline)) {
            return false;
        }
    }

    errno = ETIMEDOUT;
    return false;
}

bool idle_until_server_stop(const std::chrono::steady_clock::time_point &deadline,
                            const spot_reqrep_server_state_t *state)
{
    while (std::chrono::steady_clock::now() < deadline) {
        if (perf_stop_requested().load(std::memory_order_acquire)
            || (state
                && state->fatal_errno.load(std::memory_order_acquire) != 0)) {
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

    return !(state && state->fatal_errno.load(std::memory_order_acquire) != 0);
}

bool run_server_loop(spot_reqrep_server_state_t *state,
                     const multi_bench_settings_t &settings,
                     const std::vector<size_t> &msg_sizes)
{
    if (!state)
        return false;

    const int start_timeout_ms =
      std::max(settings.connect_ready_timeout_ms,
               std::max(1000, settings.connect_ready_timeout_ms * 6));

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        const size_t msg_size = msg_sizes[i];
        if (!perf_multi_handshake::wait_for_start(
              &state->start_gate, msg_size, start_timeout_ms)) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-reqrep-server] runner start timeout size="
                          << msg_size << std::endl;
            return false;
        }
        if (!perf_multi_spot_control::ensure_connected_peers(
              state->control_node, state->control_peers)) {
            if (bench_debug_enabled())
                std::cerr
                  << "[multi-spot-reqrep-server] ensure control peers failed size="
                  << msg_size << " err=" << zlink_errno() << std::endl;
            return false;
        }
        if (!wait_for_ready_barrier(state, msg_size, start_timeout_ms)) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-reqrep-server] ready barrier failed size="
                          << msg_size << " err=" << zlink_errno() << std::endl;
            return false;
        }
        if (!wait_for_data_peers_ready(state, start_timeout_ms)) {
            if (bench_debug_enabled())
                std::cerr
                  << "[multi-spot-reqrep-server] data peer ready timeout size="
                  << msg_size << " expected="
                  << std::max<size_t>(1, state->data_peers.endpoints.size())
                  << " err=" << zlink_errno() << std::endl;
            return false;
        }
        if (!perf_multi_spot_control::publish_start(
              state->control_pub, k_topic, msg_size)) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-reqrep-server] start publish failed size="
                          << msg_size << " err=" << zlink_errno() << std::endl;
            return false;
        }

        const auto deadline =
          std::chrono::steady_clock::now()
          + std::chrono::seconds(std::max(1, settings.duration_seconds));
        if (!idle_until_server_stop(deadline, state)) {
            if (bench_debug_enabled())
                std::cerr << "[multi-spot-reqrep-server] active wait failed size="
                          << msg_size << " err=" << zlink_errno() << std::endl;
            return false;
        }
    }

    return state->fatal_errno.load(std::memory_order_acquire) == 0;
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

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    std::vector<size_t> msg_sizes = perf_multi_client::resolve_case_msg_sizes(64);
    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    perf_multi_spot_control::server_session_t session;
    if (!initialize_reqrep_server_session(
          ctx,
          transport,
          lib_name + std::string("_spot_sendsend_server"),
          settings,
          &session)) {
        return 1;
    }

    spot_reqrep_server_state_t state;
    state.expected_ready_count =
      std::max<size_t>(1, resolve_multi_service_clients(settings.clients));
    g_server_state = &state;
    perf_multi_spot_control::prepare_server_runtime(
      &state,
      session,
      state.expected_ready_count,
      settings.connect_ready_timeout_ms,
      [](spot_reqrep_server_state_t *server_state, size_t start_size) {
          perf_multi_handshake::signal_start(
            &server_state->start_gate, start_size);
      });
    if (zlink_spot_dispatch_event_handler(state.pub, on_spot_dispatch, &state)
        != 0) {
        return 1;
    }
    const bool ok = run_server_loop(&state, settings, msg_sizes);
    fast_exit_process(ok ? 0 : 1);
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
        return 1;
    if (!multi_perf_validate_recv_mode_for_pattern(k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark(lib_name, transport);
}
