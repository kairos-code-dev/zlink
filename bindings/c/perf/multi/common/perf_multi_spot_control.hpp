#ifndef PERF_MULTI_SPOT_CONTROL_HPP
#define PERF_MULTI_SPOT_CONTROL_HPP

#include "perf_common.hpp"
#include "perf_multi_poll.hpp"
#include "perf_multi_spot_handshake.hpp"
#include "perf_multi_spot_handle.hpp"
#include "../../common/perf_tls_setup.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace perf_multi_spot_control {

using ::setup_tls_client;
using ::setup_tls_server;

inline long remaining_wait_ms(
  const std::chrono::steady_clock::time_point &deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
        return 0;

    const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return std::max<long>(1, static_cast<long>(remaining.count()));
}

inline bool wait_for_control_readable(
  void *control_sub, const std::chrono::steady_clock::time_point &deadline)
{
    if (!control_sub)
        return false;

    const long wait_ms = std::min<long>(remaining_wait_ms(deadline), 50);
    if (wait_ms <= 0)
        return true;

    return perf_socket_poll(NULL, 0, wait_ms) == 0;
}

struct client_session_t
{
    client_session_t() : node(NULL), pub(NULL), sub(NULL), local_endpoint() {}

    void *node;
    void *pub;
    void *sub;
    std::string local_endpoint;
};

struct server_session_t
{
    server_session_t() :
        node(NULL),
        pub(NULL),
        control_node(NULL),
        control_pub(NULL),
        control_sub(NULL),
        data_endpoint(),
        control_endpoint()
    {
    }

    void *node;
    void *pub;
    void *control_node;
    void *control_pub;
    void *control_sub;
    std::string data_endpoint;
    std::string control_endpoint;
};

inline void destroy_client_session(client_session_t *session)
{
    if (!session)
        return;
    if (session->pub)
        perf_destroy_default_spot_handle(&session->pub);
    if (session->sub)
        perf_destroy_default_spot_handle(&session->sub);
    if (session->node)
        zlink_spot_node_destroy(&session->node);
    session->local_endpoint.clear();
}

inline void destroy_server_session(server_session_t *session)
{
    if (!session)
        return;
    if (session->control_pub)
        perf_destroy_default_spot_handle(&session->control_pub);
    if (session->control_sub)
        perf_destroy_default_spot_handle(&session->control_sub);
    if (session->pub)
        perf_destroy_default_spot_handle(&session->pub);
    if (session->node)
        zlink_spot_node_destroy(&session->node);
    if (session->control_node)
        zlink_spot_node_destroy(&session->control_node);
    session->data_endpoint.clear();
    session->control_endpoint.clear();
}

inline bool connect_peer(void *node, const std::string &endpoint)
{
    if (!node || endpoint.empty()) {
        errno = EINVAL;
        return false;
    }
    return zlink_spot_node_connect_peer(node, endpoint.c_str())
           == ZLINK_CONNECT_OK;
}

inline bool apply_control_options(void *pub,
                                  void *sub,
                                  const multi_bench_settings_t &settings)
{
    if (!pub || !sub)
        return false;

    const int linger_ms = 0;
    const int timeout_ms = std::max(1000, settings.connect_ready_timeout_ms);
    const int nodrop = 1;

    if (zlink_set_option(pub, ZLINK_OPT_LINGER, &linger_ms,
                         sizeof(linger_ms))
          != ZLINK_CONFIG_OK
        || zlink_set_option(pub, ZLINK_OPT_SNDTIMEO, &timeout_ms,
                            sizeof(timeout_ms))
             != ZLINK_CONFIG_OK
        || zlink_set_pub_option(pub, ZLINK_PUB_OPT_NODROP, &nodrop,
                                sizeof(nodrop))
             != ZLINK_CONFIG_OK
        || zlink_set_option(sub, ZLINK_OPT_LINGER, &linger_ms,
                            sizeof(linger_ms))
             != ZLINK_CONFIG_OK
        || zlink_set_option(sub, ZLINK_OPT_RCVTIMEO, &timeout_ms,
                            sizeof(timeout_ms))
             != ZLINK_CONFIG_OK) {
        return false;
    }

    return true;
}

inline bool initialize_client_session(void *node,
                                      const std::string &server_endpoint,
                                      const std::string &local_endpoint,
                                      const char *topic,
                                      const multi_bench_settings_t &settings,
                                      client_session_t *session,
                                      std::atomic<bool> *connected_flag)
{
    if (!session || !node || local_endpoint.empty() || server_endpoint.empty()
        || !topic || !*topic) {
        errno = EINVAL;
        return false;
    }

    if (session->pub)
        perf_destroy_default_spot_handle(&session->pub);
    if (session->sub)
        perf_destroy_default_spot_handle(&session->sub);
    session->node = node;
    session->pub = NULL;
    session->sub = NULL;
    session->local_endpoint.clear();
    session->pub = perf_create_default_spot_handle(node);
    session->sub = perf_create_default_spot_handle(node);
    if (!session->pub || !session->sub
        || !apply_control_options(session->pub, session->sub, settings)) {
        destroy_client_session(session);
        return false;
    }

    session->local_endpoint = local_endpoint;
    if (zlink_spot_node_connect_peer(node, server_endpoint.c_str())
             != ZLINK_CONNECT_OK
        || zlink_set_subscription(session->sub, topic) != ZLINK_CONFIG_OK) {
        destroy_client_session(session);
        return false;
    }

    if (connected_flag)
        connected_flag->store(true, std::memory_order_release);
    return true;
}

inline bool initialize_client_control_session(
  const std::string &transport,
  const std::string &server_control_endpoint,
  const std::string &local_endpoint,
  const char *topic,
  const multi_bench_settings_t &settings,
  client_session_t *session,
  std::atomic<bool> *connected_flag)
{
    if (!session || server_control_endpoint.empty() || local_endpoint.empty()
        || !topic || !*topic) {
        errno = EINVAL;
        return false;
    }

    if (!session->node
        || !setup_tls_server(session->node, transport)
        || !setup_tls_client(session->node, transport)) {
        destroy_client_session(session);
        return false;
    }

    session->local_endpoint = local_endpoint;

    if (!initialize_client_session(session->node,
                                   server_control_endpoint,
                                   session->local_endpoint,
                                   topic,
                                   settings,
                                   session,
                                   connected_flag)) {
        destroy_client_session(session);
        return false;
    }

    return true;
}

template<typename SlotT>
inline bool initialize_client_slot(ctx_guard_t &ctx,
                                   const std::string &transport,
                                   const std::string &endpoint,
                                   const char *topic,
                                   const multi_bench_settings_t &settings,
                                   bool (*apply_sub_options)(
                                     void *, const multi_bench_settings_t &),
                                   SlotT *slot)
{
    if (!slot || endpoint.empty() || !topic || !*topic) {
        errno = EINVAL;
        return false;
    }

    slot->node = zlink_spot_node_new(ctx.get());
    if (!slot->node || !setup_tls_client(slot->node, transport)) {
        if (slot->node)
            zlink_spot_node_destroy(&slot->node);
        slot->node = NULL;
        return false;
    }

    slot->handle = perf_create_default_spot_handle(slot->node);
    if (!slot->handle) {
        zlink_spot_node_destroy(&slot->node);
        slot->node = NULL;
        return false;
    }

    if (!apply_sub_options
        || !apply_sub_options(slot->handle, settings)
        || zlink_spot_node_connect_peer(slot->node, endpoint.c_str())
             != ZLINK_CONNECT_OK
        || zlink_set_subscription(slot->handle, topic) != ZLINK_CONFIG_OK) {
        perf_destroy_default_spot_handle(&slot->handle);
        zlink_spot_node_destroy(&slot->node);
        slot->handle = NULL;
        slot->node = NULL;
        return false;
    }

    return true;
}

inline std::string bind_data_endpoint(void *node,
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

inline std::string bind_control_endpoint(void *node,
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

inline bool initialize_server_session(ctx_guard_t &ctx,
                                      const std::string &transport,
                                      const std::string &token,
                                      const char *topic,
                                      const multi_bench_settings_t &settings,
                                      bool (*apply_server_options)(
                                        void *, const multi_bench_settings_t &),
                                      server_session_t *session)
{
    if (!session || !topic || !*topic || !apply_server_options) {
        errno = EINVAL;
        return false;
    }

    destroy_server_session(session);
    session->node = zlink_spot_node_new(ctx.get());
    session->control_node = zlink_spot_node_new(ctx.get());
    if (!session->node || !session->control_node
        || !setup_tls_server(session->node, transport)
        || !setup_tls_server(session->control_node, transport)
        || !setup_tls_client(session->control_node, transport)) {
        destroy_server_session(session);
        return false;
    }

    session->pub = perf_create_default_spot_handle(session->node);
    session->control_pub =
      perf_create_default_spot_handle(session->control_node);
    session->control_sub =
      perf_create_default_spot_handle(session->control_node);
    if (!session->pub || !session->control_pub || !session->control_sub
        || !apply_server_options(session->pub, settings)
        || !apply_control_options(
             session->control_pub, session->control_sub, settings)
        || zlink_set_subscription(session->control_sub, topic)
             != ZLINK_CONFIG_OK) {
        destroy_server_session(session);
        return false;
    }

    session->data_endpoint = bind_data_endpoint(session->node, transport, token);
    if (session->data_endpoint.empty()) {
        destroy_server_session(session);
        return false;
    }

    session->control_endpoint =
      bind_control_endpoint(session->control_node, transport);
    if (session->control_endpoint.empty()) {
        destroy_server_session(session);
        return false;
    }

    return true;
}

inline bool wait_for_connected_peer_count(void *node,
                                          size_t expected_connected_count,
                                          int timeout_ms,
                                          const std::atomic<int> *fatal_errno)
{
    if (!node || expected_connected_count == 0) {
        errno = EINVAL;
        return false;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline) {
        zlink_spot_node_status_t status;
        std::memset(&status, 0, sizeof(status));
        if (zlink_spot_node_status_snapshot(node, &status) == 0
            && status.connected_peer_count >= expected_connected_count) {
            return true;
        }

        if (perf_stop_requested().load(std::memory_order_acquire)
            || (fatal_errno
                && fatal_errno->load(std::memory_order_acquire) != 0)) {
            errno = EIO;
            return false;
        }

        if (perf_socket_poll(
              NULL, 0, std::min<long>(remaining_wait_ms(deadline), 50))
            < 0
            && zlink_errno() != EINTR)
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

inline bool ensure_connected_peers(
  void *node,
  const perf_multi_spot_handshake::peer_registry_t &registry)
{
    if (!node)
        return false;

    for (size_t i = 0; i < registry.endpoints.size(); ++i) {
        if (!connect_peer(node, registry.endpoints[i]))
            return false;
    }
    return true;
}

inline bool accept_reverse_connect_request(
  void *node,
  perf_multi_spot_handshake::peer_registry_t *registry,
  const std::string &endpoint,
  int timeout_ms,
  const std::atomic<int> *fatal_errno)
{
    (void) timeout_ms;
    (void) fatal_errno;

    if (!node || !registry || endpoint.empty()) {
        errno = EINVAL;
        return false;
    }
    if (!perf_multi_spot_handshake::register_peer(registry, endpoint)
        || !connect_peer(node, endpoint)) {
        return false;
    }
    return true;
}

inline void disconnect_peers(
  void *node,
  const perf_multi_spot_handshake::peer_registry_t &registry)
{
    if (!node)
        return;

    for (size_t i = 0; i < registry.endpoints.size(); ++i) {
        const std::string &endpoint = registry.endpoints[i];
        if (endpoint.empty())
            continue;
        (void) zlink_spot_node_disconnect_peer(node, endpoint.c_str());
    }
}

inline bool publish_control_payload(void *control_pub,
                                    const char *topic,
                                    const std::string &payload)
{
    if (!control_pub || !topic || !*topic || payload.empty()) {
        errno = EINVAL;
        return false;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(
          1,
          resolve_multi_int_env("PERF_MULTI_CONNECT_READY_TIMEOUT_MS", 5000, 0)));

    while (std::chrono::steady_clock::now() < deadline) {
        zlink_msg_t part;
        if (zlink_msg_init_size(&part, payload.size()) != 0)
            return false;
        std::memcpy(zlink_msg_data(&part), payload.data(), payload.size());

        const int rc = zlink_publish(control_pub,
                                     topic,
                                     &part,
                                     1,
                                     static_cast<zlink_send_flags_t>(0));
        const int saved_errno = rc == 0 ? 0 : errno;
        if (rc == 0)
            return true;

        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK
            && saved_errno != ETIMEDOUT) {
            errno = saved_errno;
            return false;
        }

        const long wait_ms = std::min<long>(remaining_wait_ms(deadline), 10);
        if (wait_ms <= 0)
            break;
        if (perf_socket_poll(NULL, 0, wait_ms) < 0 && zlink_errno() != EINTR) {
            errno = zlink_errno() != 0 ? zlink_errno() : errno;
            return false;
        }
    }

    errno = ETIMEDOUT;
    return false;
}

inline bool control_topic_matches(const char *actual_topic,
                                  size_t actual_topic_len,
                                  const char *expected_topic)
{
    if (!actual_topic || !expected_topic || !*expected_topic)
        return false;

    const size_t expected_len = std::strlen(expected_topic);
    if (actual_topic_len > 0 && actual_topic[actual_topic_len - 1] == '\0')
        --actual_topic_len;
    return actual_topic_len == expected_len
           && std::memcmp(actual_topic, expected_topic, expected_len) == 0;
}

inline bool receive_control_payload(void *control_sub,
                                    const char *expected_topic,
                                    std::string *payload_out,
                                    bool *received)
{
    if (!control_sub || !expected_topic || !*expected_topic || !payload_out) {
        errno = EINVAL;
        return false;
    }

    if (received)
        *received = false;
    payload_out->clear();

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic[256];
    size_t topic_len = sizeof(topic) - 1;
    const int rc =
      zlink_subscribe(control_sub,
                      NULL,
                      &parts,
                      &part_count,
                      topic,
                      &topic_len,
                      static_cast<zlink_recv_flags_t>(ZLINK_DONTWAIT));
    if (rc != 0) {
        const int err = zlink_errno() != 0 ? zlink_errno() : errno;
        if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
            || err == ETIMEDOUT) {
            return true;
        }
        return false;
    }

    if (received)
        *received = true;

    if (control_topic_matches(topic, topic_len, expected_topic) && part_count > 0) {
        payload_out->assign(
          static_cast<const char *>(zlink_msg_data(&parts[0])),
          zlink_msg_size(&parts[0]));
    }

    perf_close_multipart(parts, part_count);
    return true;
}

inline bool publish_ready_slot(void *control_pub,
                               const char *topic,
                               size_t msg_size,
                               size_t slot_index)
{
    return publish_control_payload(
      control_pub,
      topic,
      perf_multi_spot_handshake::make_ready_slot_command(
        msg_size, slot_index));
}

inline bool publish_ready_count(void *control_pub,
                                const char *topic,
                                size_t msg_size,
                                size_t ready_count)
{
    return publish_control_payload(
      control_pub,
      topic,
      perf_multi_spot_handshake::make_ready_count_command(
        msg_size, ready_count));
}

inline bool publish_connected(void *control_pub, const char *topic)
{
    return publish_control_payload(control_pub, topic, "CONNECTED");
}

inline bool publish_start(void *control_pub,
                          const char *topic,
                          size_t msg_size)
{
    return publish_control_payload(
      control_pub,
      topic,
      perf_multi_spot_handshake::make_start_command(msg_size));
}

inline bool publish_data_endpoint(void *control_pub,
                                  const char *topic,
                                  const std::string &endpoint)
{
    if (endpoint.empty()) {
        errno = EINVAL;
        return false;
    }

    return publish_control_payload(
      control_pub,
      topic,
      perf_multi_spot_handshake::make_data_endpoint_command(endpoint));
}

inline bool wait_for_ready_units(
  void *control_sub,
  const char *topic,
  perf_multi_spot_handshake::ready_state_t *ready_state,
  size_t msg_size,
  size_t expected_ready_count,
  int timeout_ms,
  const std::atomic<int> *fatal_errno)
{
    if (!control_sub || !topic || !*topic || !ready_state || msg_size == 0
        || expected_ready_count == 0) {
        errno = EINVAL;
        return false;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));

    while (std::chrono::steady_clock::now() < deadline) {
        bool received = false;
        std::string payload;
        if (!receive_control_payload(control_sub, topic, &payload, &received))
            return false;

        if (received && !payload.empty()) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-control] recv ts_ns="
                          << perf_multi_metric::now_ns()
                          << " topic=" << topic
                          << " payload=" << payload << std::endl;
            }
            size_t ready_size = 0;
            size_t slot_index = 0;
            size_t ready_count = 0;
            if (perf_multi_spot_handshake::parse_ready_slot_command(
                  payload.data(), payload.size(), &ready_size, &slot_index)) {
                perf_multi_spot_handshake::record_ready_slot(
                  ready_state, ready_size, slot_index);
            } else if (perf_multi_spot_handshake::parse_ready_count_command(
                         payload.data(),
                         payload.size(),
                         &ready_size,
                         &ready_count)) {
                perf_multi_spot_handshake::record_ready_count(
                  ready_state, ready_size, ready_count);
            }
        }

        if (perf_stop_requested().load(std::memory_order_acquire)
            || (fatal_errno
                && fatal_errno->load(std::memory_order_acquire) != 0)) {
            errno = EIO;
            return false;
        }

        if (perf_multi_spot_handshake::ready_units(ready_state, msg_size)
            >= expected_ready_count) {
            return true;
        }

        if (!wait_for_control_readable(control_sub, deadline))
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

inline bool ensure_connected(void *node,
                             const std::string &endpoint,
                             std::atomic<bool> *connected_flag)
{
    if (!node || endpoint.empty()) {
        errno = EINVAL;
        return false;
    }
    if (connected_flag
        && connected_flag->load(std::memory_order_acquire)) {
        return true;
    }
    if (zlink_spot_node_connect_peer(node, endpoint.c_str())
        != ZLINK_CONNECT_OK)
        return false;
    if (connected_flag)
        connected_flag->store(true, std::memory_order_release);
    return true;
}

inline void disconnect_peer(void *node,
                            const std::string &endpoint,
                            std::atomic<bool> *connected_flag)
{
    if (!node || endpoint.empty())
        return;
    (void) zlink_spot_node_disconnect_peer(node, endpoint.c_str());
    if (connected_flag)
        connected_flag->store(false, std::memory_order_release);
}

template<typename StateT, typename ReceiveFn>
inline bool wait_for_control_link_ready(StateT *state,
                                        int timeout_ms,
                                        const ReceiveFn &receive_fn)
{
    if (!state)
        return false;
    if (state->control_link_ready.load(std::memory_order_acquire))
        return true;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline) {
        if (state->control_link_ready.load(std::memory_order_acquire))
            return true;
        bool received = false;
        if (!receive_fn(state, &received))
            return false;
        if (state->control_link_ready.load(std::memory_order_acquire))
            return true;
        if (!wait_for_control_readable(state->control_sub, deadline))
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

template<typename StateT, typename ReceiveFn>
inline bool wait_for_started_size(StateT *state,
                                  size_t msg_size,
                                  int timeout_ms,
                                  const ReceiveFn &receive_fn)
{
    if (!state || msg_size == 0)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline) {
        if (state->fatal.load(std::memory_order_acquire)
            || state->control_started_msg_size.load(std::memory_order_acquire)
                 == msg_size) {
            return true;
        }

        bool received = false;
        if (!receive_fn(state, &received))
            return false;
        if (state->fatal.load(std::memory_order_acquire)
            || state->control_started_msg_size.load(std::memory_order_acquire)
                 == msg_size) {
            return true;
        }

        zlink_pollitem_t item = {NULL, 0, 0, 0};
        if (zlink_poll(&item, 0, 5, NULL) < 0 && zlink_errno() != EINTR)
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

template<typename StateT>
inline bool wait_for_phase(StateT *state,
                           size_t msg_size,
                           int expected_phase,
                           int timeout_ms)
{
    if (!state || msg_size == 0)
        return false;

    std::unique_lock<std::mutex> lock(state->mutex);
    return state->cv.wait_for(
      lock,
      std::chrono::milliseconds(std::max(1, timeout_ms)),
      [state, msg_size, expected_phase]() {
          return state->fatal.load(std::memory_order_acquire)
                 || (state->seen_msg_size.load(std::memory_order_acquire)
                       == msg_size
                     && state->seen_phase.load(std::memory_order_acquire)
                          == expected_phase);
      });
}

template<typename StateT>
inline bool wait_for_phase_duration(StateT *state, double seconds)
{
    if (!state)
        return false;
    if (seconds <= 0.0)
        return true;

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(seconds));

    std::unique_lock<std::mutex> lock(state->mutex);
    while (!state->fatal.load(std::memory_order_acquire)) {
        if (state->cv.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }

    return !state->fatal.load(std::memory_order_acquire);
}

template<typename StateT, typename NotifyStartFn>
inline void start_client_stdin_watcher(StateT *state, NotifyStartFn notify_start)
{
    if (!state)
        return;

    std::thread stdin_watcher([state, notify_start]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (bench_transition_debug_enabled()) {
                std::cerr << "[multi-spot-client] stdin line ts_ns="
                          << perf_multi_metric::now_ns()
                          << " line=" << line << std::endl;
            }

            size_t start_size = 0;
            if (perf_multi_spot_handshake::parse_control_connected_line(line)) {
                state->control_link_ready.store(true, std::memory_order_release);
                state->cv.notify_all();
                continue;
            }
            if (perf_multi_spot_handshake::parse_start_command(
                  line.data(), line.size(), &start_size)) {
                notify_start(state, start_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                state->fatal.store(true, std::memory_order_release);
                state->cv.notify_all();
                perf_multi_handshake::signal_stop(&state->start_gate);
                return;
            }
        }
    });
    stdin_watcher.detach();
}

template<typename StateT, typename NotifyStartFn>
inline void start_server_stdin_watcher(StateT *state,
                                       int connect_ready_timeout_ms,
                                       NotifyStartFn notify_start)
{
    if (!state)
        return;

    std::thread stdin_watcher(
      [state, connect_ready_timeout_ms, notify_start]() {
          std::string line;
          while (std::getline(std::cin, line)) {
              if (bench_transition_debug_enabled()) {
                  std::cerr << "[multi-spot-server] stdin line ts_ns="
                            << perf_multi_metric::now_ns()
                            << " line=" << line << std::endl;
              }

              size_t start_size = 0;
              std::string connect_endpoint;
              if (perf_multi_handshake::parse_endpoint_command_line(
                    line, "CONNECT_CONTROL,", &connect_endpoint)) {
                  if (bench_transition_debug_enabled()) {
                      std::cerr
                        << "[multi-spot-server] reverse connect request ts_ns="
                        << perf_multi_metric::now_ns()
                        << " endpoint=" << connect_endpoint << std::endl;
                  }
                  if (!accept_reverse_connect_request(
                        state->control_node,
                        &state->control_peers,
                        connect_endpoint,
                        connect_ready_timeout_ms,
                        &state->fatal_errno)) {
                      if (bench_debug_enabled()) {
                          std::cerr
                            << "[multi-spot-server] reverse connect failed"
                            << " endpoint=" << connect_endpoint
                            << " err=" << zlink_errno() << std::endl;
                      }
                      const int err = zlink_errno() != 0 ? zlink_errno() : errno;
                      state->fatal_errno.store(
                        err != 0 ? err : EIO,
                        std::memory_order_release);
                      perf_stop_requested().store(true, std::memory_order_release);
                      return;
                  }
                  std::cout << "CONTROL_CONNECTED," << connect_endpoint
                            << std::endl;
                  continue;
              }
              if (perf_multi_handshake::parse_size_command_line(
                    line, "START,", &start_size)) {
                  notify_start(state, start_size);
                  continue;
              }
              if (line == "STOP" || line == "QUIT") {
                  perf_stop_requested().store(true, std::memory_order_release);
                  return;
              }
          }
    });
    stdin_watcher.detach();
}

inline void emit_server_ready_lines(const server_session_t &session)
{
    std::cout << "READY," << session.data_endpoint << std::endl;
    std::cout << "CONTROL_READY," << session.control_endpoint << std::endl;
}

template<typename StateT, typename NotifyStartFn>
inline void prepare_server_runtime(StateT *state,
                                   const server_session_t &session,
                                   size_t expected_ready_count,
                                   int connect_ready_timeout_ms,
                                   NotifyStartFn notify_start)
{
    if (!state)
        return;

    state->node = session.node;
    state->pub = session.pub;
    state->control_node = session.control_node;
    state->control_pub = session.control_pub;
    state->control_sub = session.control_sub;
    state->expected_ready_count = expected_ready_count;

    perf_stop_requested().store(false, std::memory_order_release);
    install_perf_signal_handlers();
    start_server_stdin_watcher(
      state,
      connect_ready_timeout_ms,
      notify_start);
    emit_server_ready_lines(session);
}

} // namespace perf_multi_spot_control

#endif
