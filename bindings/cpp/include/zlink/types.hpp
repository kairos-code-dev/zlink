/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TYPES_HPP_INCLUDED
#define ZLINK_CPP_TYPES_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

/**
 * @brief Socket kinds used with `socket_t`.
 */
enum class socket_type : int
{
    pair = ZLINK_PAIR,
    pub = ZLINK_PUB,
    sub = ZLINK_SUB,
    dealer = ZLINK_DEALER,
    router = ZLINK_ROUTER,
    xpub = ZLINK_XPUB,
    xsub = ZLINK_XSUB,
    stream = ZLINK_STREAM
};

/**
 * @brief Context option keys for `context_t::set/get`.
 */
enum class context_option : int
{
    io_threads = ZLINK_IO_THREADS,
    max_sockets = ZLINK_MAX_SOCKETS,
    socket_limit = ZLINK_SOCKET_LIMIT,
    thread_priority = ZLINK_THREAD_PRIORITY,
    thread_sched_policy = ZLINK_THREAD_SCHED_POLICY,
    max_msgsz = ZLINK_MAX_MSGSZ,
    msg_t_size = ZLINK_MSG_T_SIZE,
    thread_affinity_cpu_add = ZLINK_THREAD_AFFINITY_CPU_ADD,
    thread_affinity_cpu_remove = ZLINK_THREAD_AFFINITY_CPU_REMOVE,
    thread_name_prefix = ZLINK_THREAD_NAME_PREFIX
};

/**
 * @brief Socket option keys for `socket_t::set/get`.
 */
enum class socket_option : int
{
    affinity = ZLINK_AFFINITY,
    routing_id = ZLINK_ROUTING_ID,
    subscribe = ZLINK_SUBSCRIBE,
    unsubscribe = ZLINK_UNSUBSCRIBE,
    rate = ZLINK_RATE,
    recovery_ivl = ZLINK_RECOVERY_IVL,
    sndbuf = ZLINK_SNDBUF,
    rcvbuf = ZLINK_RCVBUF,
    rcvmore = ZLINK_RCVMORE,
    fd = ZLINK_FD,
    events = ZLINK_EVENTS,
    type = ZLINK_TYPE,
    linger = ZLINK_LINGER,
    reconnect_ivl = ZLINK_RECONNECT_IVL,
    backlog = ZLINK_BACKLOG,
    reconnect_ivl_max = ZLINK_RECONNECT_IVL_MAX,
    maxmsgsize = ZLINK_MAXMSGSIZE,
    sndhwm = ZLINK_SNDHWM,
    rcvhwm = ZLINK_RCVHWM,
    multicast_hops = ZLINK_MULTICAST_HOPS,
    rcvtimeo = ZLINK_RCVTIMEO,
    sndtimeo = ZLINK_SNDTIMEO,
    last_endpoint = ZLINK_LAST_ENDPOINT,
    router_mandatory = ZLINK_ROUTER_MANDATORY,
    tcp_keepalive = ZLINK_TCP_KEEPALIVE,
    tcp_keepalive_cnt = ZLINK_TCP_KEEPALIVE_CNT,
    tcp_keepalive_idle = ZLINK_TCP_KEEPALIVE_IDLE,
    tcp_keepalive_intvl = ZLINK_TCP_KEEPALIVE_INTVL,
    tcp_nodelay = ZLINK_TCP_NODELAY,
    immediate = ZLINK_IMMEDIATE,
    xpub_verbose = ZLINK_XPUB_VERBOSE,
    ipv6 = ZLINK_IPV6,
    probe_router = ZLINK_PROBE_ROUTER,
    conflate = ZLINK_CONFLATE,
    router_handover = ZLINK_ROUTER_HANDOVER,
    tos = ZLINK_TOS,
    connect_routing_id = ZLINK_CONNECT_ROUTING_ID,
    handshake_ivl = ZLINK_HANDSHAKE_IVL,
    xpub_nodrop = ZLINK_XPUB_NODROP,
    blocky = ZLINK_BLOCKY,
    xpub_manual = ZLINK_XPUB_MANUAL,
    xpub_welcome_msg = ZLINK_XPUB_WELCOME_MSG,
    stream_notify = ZLINK_STREAM_NOTIFY,
    invert_matching = ZLINK_INVERT_MATCHING,
    heartbeat_ivl = ZLINK_HEARTBEAT_IVL,
    heartbeat_ttl = ZLINK_HEARTBEAT_TTL,
    heartbeat_timeout = ZLINK_HEARTBEAT_TIMEOUT,
    xpub_verboser = ZLINK_XPUB_VERBOSER,
    connect_timeout = ZLINK_CONNECT_TIMEOUT,
    tcp_maxrt = ZLINK_TCP_MAXRT,
    multicast_maxtpdu = ZLINK_MULTICAST_MAXTPDU,
    use_fd = ZLINK_USE_FD,
    bindtodevice = ZLINK_BINDTODEVICE,
    tls_cert = ZLINK_TLS_CERT,
    tls_key = ZLINK_TLS_KEY,
    tls_ca = ZLINK_TLS_CA,
    tls_verify = ZLINK_TLS_VERIFY,
    tls_require_client_cert = ZLINK_TLS_REQUIRE_CLIENT_CERT,
    tls_hostname = ZLINK_TLS_HOSTNAME,
    tls_trust_system = ZLINK_TLS_TRUST_SYSTEM,
    tls_password = ZLINK_TLS_PASSWORD,
    xpub_manual_last_value = ZLINK_XPUB_MANUAL_LAST_VALUE,
    only_first_subscribe = ZLINK_ONLY_FIRST_SUBSCRIBE,
    topics_count = ZLINK_TOPICS_COUNT,
    zmp_metadata = ZLINK_ZMP_METADATA
};

/**
 * @brief Typed socket option key.
 *
 * This mirrors the .NET `SocketOptionKey<T>` usability pattern and lets
 * `socket_t` expose type-safe `set/get` overloads.
 */
template<typename T> struct socket_option_key_t
{
    explicit constexpr socket_option_key_t (socket_option option_)
        : option (option_)
    {
    }

    socket_option option;
};

/**
 * @brief Predefined typed socket option keys.
 */
namespace socket_options
{
static const socket_option_key_t<uint64_t> affinity (socket_option::affinity);
static const socket_option_key_t<std::string> routing_id (
  socket_option::routing_id);
static const socket_option_key_t<std::string> subscribe (
  socket_option::subscribe);
static const socket_option_key_t<std::string> unsubscribe (
  socket_option::unsubscribe);
static const socket_option_key_t<int> rate (socket_option::rate);
static const socket_option_key_t<int> recovery_ivl (
  socket_option::recovery_ivl);
static const socket_option_key_t<int> sndbuf (socket_option::sndbuf);
static const socket_option_key_t<int> rcvbuf (socket_option::rcvbuf);
static const socket_option_key_t<int> rcvmore (socket_option::rcvmore);
static const socket_option_key_t<zlink_fd_t> fd (socket_option::fd);
static const socket_option_key_t<int> events (socket_option::events);
static const socket_option_key_t<int> type (socket_option::type);
static const socket_option_key_t<int> linger (socket_option::linger);
static const socket_option_key_t<int> reconnect_ivl (
  socket_option::reconnect_ivl);
static const socket_option_key_t<int> backlog (socket_option::backlog);
static const socket_option_key_t<int> reconnect_ivl_max (
  socket_option::reconnect_ivl_max);
static const socket_option_key_t<int64_t> maxmsgsize (
  socket_option::maxmsgsize);
static const socket_option_key_t<int> sndhwm (socket_option::sndhwm);
static const socket_option_key_t<int> rcvhwm (socket_option::rcvhwm);
static const socket_option_key_t<int> multicast_hops (
  socket_option::multicast_hops);
static const socket_option_key_t<int> rcvtimeo (socket_option::rcvtimeo);
static const socket_option_key_t<int> sndtimeo (socket_option::sndtimeo);
static const socket_option_key_t<std::string> last_endpoint (
  socket_option::last_endpoint);
static const socket_option_key_t<int> router_mandatory (
  socket_option::router_mandatory);
static const socket_option_key_t<int> tcp_keepalive (
  socket_option::tcp_keepalive);
static const socket_option_key_t<int> tcp_keepalive_cnt (
  socket_option::tcp_keepalive_cnt);
static const socket_option_key_t<int> tcp_keepalive_idle (
  socket_option::tcp_keepalive_idle);
static const socket_option_key_t<int> tcp_keepalive_intvl (
  socket_option::tcp_keepalive_intvl);
static const socket_option_key_t<int> tcp_nodelay (
  socket_option::tcp_nodelay);
static const socket_option_key_t<int> immediate (socket_option::immediate);
static const socket_option_key_t<int> xpub_verbose (
  socket_option::xpub_verbose);
static const socket_option_key_t<int> ipv6 (socket_option::ipv6);
static const socket_option_key_t<int> probe_router (
  socket_option::probe_router);
static const socket_option_key_t<int> conflate (socket_option::conflate);
static const socket_option_key_t<int> router_handover (
  socket_option::router_handover);
static const socket_option_key_t<int> tos (socket_option::tos);
static const socket_option_key_t<std::string> connect_routing_id (
  socket_option::connect_routing_id);
static const socket_option_key_t<int> handshake_ivl (
  socket_option::handshake_ivl);
static const socket_option_key_t<int> xpub_nodrop (
  socket_option::xpub_nodrop);
static const socket_option_key_t<int> blocky (socket_option::blocky);
static const socket_option_key_t<int> xpub_manual (
  socket_option::xpub_manual);
static const socket_option_key_t<std::string> xpub_welcome_msg (
  socket_option::xpub_welcome_msg);
static const socket_option_key_t<int> stream_notify (
  socket_option::stream_notify);
static const socket_option_key_t<int> invert_matching (
  socket_option::invert_matching);
static const socket_option_key_t<int> heartbeat_ivl (
  socket_option::heartbeat_ivl);
static const socket_option_key_t<int> heartbeat_ttl (
  socket_option::heartbeat_ttl);
static const socket_option_key_t<int> heartbeat_timeout (
  socket_option::heartbeat_timeout);
static const socket_option_key_t<int> xpub_verboser (
  socket_option::xpub_verboser);
static const socket_option_key_t<int> connect_timeout (
  socket_option::connect_timeout);
static const socket_option_key_t<int> tcp_maxrt (socket_option::tcp_maxrt);
static const socket_option_key_t<int> multicast_maxtpdu (
  socket_option::multicast_maxtpdu);
static const socket_option_key_t<int> use_fd (socket_option::use_fd);
static const socket_option_key_t<std::string> bindtodevice (
  socket_option::bindtodevice);
static const socket_option_key_t<std::string> tls_cert (
  socket_option::tls_cert);
static const socket_option_key_t<std::string> tls_key (
  socket_option::tls_key);
static const socket_option_key_t<std::string> tls_ca (socket_option::tls_ca);
static const socket_option_key_t<int> tls_verify (socket_option::tls_verify);
static const socket_option_key_t<int> tls_require_client_cert (
  socket_option::tls_require_client_cert);
static const socket_option_key_t<std::string> tls_hostname (
  socket_option::tls_hostname);
static const socket_option_key_t<int> tls_trust_system (
  socket_option::tls_trust_system);
static const socket_option_key_t<std::string> tls_password (
  socket_option::tls_password);
static const socket_option_key_t<int> xpub_manual_last_value (
  socket_option::xpub_manual_last_value);
static const socket_option_key_t<int> only_first_subscribe (
  socket_option::only_first_subscribe);
static const socket_option_key_t<int> topics_count (
  socket_option::topics_count);
static const socket_option_key_t<int> zmp_metadata (
  socket_option::zmp_metadata);
} // namespace socket_options

/**
 * @brief Send flag bitmask.
 */
enum class send_flag : int
{
    none = 0,
    dontwait = ZLINK_DONTWAIT,
    sndmore = ZLINK_SNDMORE
};

/**
 * @brief Combine send flags.
 */
inline send_flag operator| (send_flag a, send_flag b)
{
    return static_cast<send_flag> (static_cast<int> (a)
                                   | static_cast<int> (b));
}

/**
 * @brief Receive flag bitmask.
 */
enum class recv_flag : int
{
    none = 0,
    dontwait = ZLINK_DONTWAIT
};

/**
 * @brief Combine receive flags.
 */
inline recv_flag operator| (recv_flag a, recv_flag b)
{
    return static_cast<recv_flag> (static_cast<int> (a)
                                   | static_cast<int> (b));
}

/**
 * @brief Stream packet dispatch mode.
 */
enum class stream_dispatch_mode : int
{
    none = 0,
    len32be = ZLINK_STREAM_DISPATCH_LEN32BE
};

/**
 * @brief Canonical zlink runtime error codes.
 */
enum class error_code : int
{
    efsm = EFSM,
    enocompatproto = ENOCOMPATPROTO,
    eterm = ETERM,
    emthread = EMTHREAD
};

/**
 * @brief Protocol-specific handshake and framing errors.
 */
enum class protocol_error : int
{
    zmp_unspecified = ZLINK_PROTOCOL_ERROR_ZMP_UNSPECIFIED,
    zmp_unexpected_command = ZLINK_PROTOCOL_ERROR_ZMP_UNEXPECTED_COMMAND,
    zmp_invalid_sequence = ZLINK_PROTOCOL_ERROR_ZMP_INVALID_SEQUENCE,
    zmp_key_exchange = ZLINK_PROTOCOL_ERROR_ZMP_KEY_EXCHANGE,
    zmp_malformed_command_unspecified =
      ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_UNSPECIFIED,
    zmp_malformed_command_message =
      ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_MESSAGE,
    zmp_malformed_command_hello =
      ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO,
    zmp_malformed_command_initiate =
      ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_INITIATE,
    zmp_malformed_command_error =
      ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_ERROR,
    zmp_malformed_command_ready =
      ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_READY,
    zmp_malformed_command_welcome =
      ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_WELCOME,
    zmp_invalid_metadata = ZLINK_PROTOCOL_ERROR_ZMP_INVALID_METADATA,
    zmp_cryptographic = ZLINK_PROTOCOL_ERROR_ZMP_CRYPTOGRAPHIC,
    zmp_mechanism_mismatch = ZLINK_PROTOCOL_ERROR_ZMP_MECHANISM_MISMATCH,
    ws_unspecified = ZLINK_PROTOCOL_ERROR_WS_UNSPECIFIED
};

/**
 * @brief Event mask for socket monitoring.
 */
enum class monitor_event : int
{
    connected = ZLINK_EVENT_CONNECTED,
    connect_delayed = ZLINK_EVENT_CONNECT_DELAYED,
    connect_retried = ZLINK_EVENT_CONNECT_RETRIED,
    listening = ZLINK_EVENT_LISTENING,
    bind_failed = ZLINK_EVENT_BIND_FAILED,
    accepted = ZLINK_EVENT_ACCEPTED,
    accept_failed = ZLINK_EVENT_ACCEPT_FAILED,
    closed = ZLINK_EVENT_CLOSED,
    close_failed = ZLINK_EVENT_CLOSE_FAILED,
    disconnected = ZLINK_EVENT_DISCONNECTED,
    monitor_stopped = ZLINK_EVENT_MONITOR_STOPPED,
    handshake_failed_no_detail = ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL,
    connection_ready = ZLINK_EVENT_CONNECTION_READY,
    handshake_failed_protocol = ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL,
    handshake_failed_auth = ZLINK_EVENT_HANDSHAKE_FAILED_AUTH,
    all = ZLINK_EVENT_ALL
};

/**
 * @brief Combine monitor event masks.
 */
inline monitor_event operator| (monitor_event a, monitor_event b)
{
    return static_cast<monitor_event> (static_cast<int> (a)
                                       | static_cast<int> (b));
}

/**
 * @brief Peer disconnect classification.
 */
enum class disconnect_reason : int
{
    unknown = ZLINK_DISCONNECT_UNKNOWN,
    local = ZLINK_DISCONNECT_LOCAL,
    remote = ZLINK_DISCONNECT_REMOTE,
    handshake_failed = ZLINK_DISCONNECT_HANDSHAKE_FAILED,
    transport_error = ZLINK_DISCONNECT_TRANSPORT_ERROR,
    ctx_term = ZLINK_DISCONNECT_CTX_TERM
};

/**
 * @brief Poll event mask.
 */
enum class poll_event : int
{
    pollin = ZLINK_POLLIN,
    pollout = ZLINK_POLLOUT,
    pollerr = ZLINK_POLLERR,
    pollpri = ZLINK_POLLPRI
};

/**
 * @brief Combine poll event masks.
 */
inline poll_event operator| (poll_event a, poll_event b)
{
    return static_cast<poll_event> (static_cast<int> (a)
                                    | static_cast<int> (b));
}

/**
 * @brief Service-plane participant type.
 */
enum class service_type : int
{
    gateway = ZLINK_SERVICE_TYPE_GATEWAY,
    spot = ZLINK_SERVICE_TYPE_SPOT
};

/**
 * @brief Load-balancing strategy for gateway services.
 */
enum class gateway_lb_strategy : int
{
    round_robin = ZLINK_GATEWAY_LB_ROUND_ROBIN,
    weighted = ZLINK_GATEWAY_LB_WEIGHTED
};

/**
 * @brief Socket roles exposed by registry.
 */
enum class registry_socket_role : int
{
    pub = ZLINK_REGISTRY_SOCKET_PUB,
    router = ZLINK_REGISTRY_SOCKET_ROUTER,
    peer_sub = ZLINK_REGISTRY_SOCKET_PEER_SUB
};

/**
 * @brief Socket roles exposed by discovery.
 */
enum class discovery_socket_role : int
{
    sub = ZLINK_DISCOVERY_SOCKET_SUB
};

/**
 * @brief Socket roles exposed by gateway.
 */
enum class gateway_socket_role : int
{
    router = ZLINK_GATEWAY_SOCKET_ROUTER
};

/**
 * @brief Socket roles exposed by receiver.
 */
enum class receiver_socket_role : int
{
    router = ZLINK_RECEIVER_SOCKET_ROUTER,
    dealer = ZLINK_RECEIVER_SOCKET_DEALER
};

/**
 * @brief Socket roles exposed by spot node.
 */
enum class spot_node_socket_role : int
{
    node = ZLINK_SPOT_NODE_SOCKET_NODE,
    pub = ZLINK_SPOT_NODE_SOCKET_PUB,
    sub = ZLINK_SPOT_NODE_SOCKET_SUB,
    dealer = ZLINK_SPOT_NODE_SOCKET_DEALER
};

/**
 * @brief Node-level options for spot node behavior.
 */
enum class spot_node_option : int
{
    pub_mode = ZLINK_SPOT_NODE_OPT_PUB_MODE,
    pub_queue_hwm = ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM,
    pub_queue_full_policy = ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY
};

/**
 * @brief Publish mode for spot node.
 */
enum class spot_node_pub_mode : int
{
    sync = ZLINK_SPOT_NODE_PUB_MODE_SYNC,
    async = ZLINK_SPOT_NODE_PUB_MODE_ASYNC
};

/**
 * @brief Behavior when async spot publish queue is full.
 */
enum class spot_node_pub_queue_full_policy : int
{
    eagain = ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN,
    drop = ZLINK_SPOT_NODE_PUB_QUEUE_FULL_DROP
};

/**
 * @brief Socket roles used by `spot_t`.
 */
enum class spot_socket_role : int
{
    pub = 1,
    sub = 2
};

/**
 * @brief Build a native routing id from raw bytes.
 * @param bytes_ Byte buffer. May be `NULL` only when `size_` is zero.
 * @param size_ Number of bytes in `bytes_` (0..255).
 * @param out_ Output routing id.
 * @return 0 on success, -1 on failure.
 */
inline int routing_id_from (const void *bytes_,
                            size_t size_,
                            zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }
    if (size_ > sizeof (out_->data)) {
        errno = EMSGSIZE;
        return -1;
    }
    if (size_ > 0 && !bytes_) {
        errno = EINVAL;
        return -1;
    }

    std::memset (out_, 0, sizeof (*out_));
    out_->size = static_cast<uint8_t> (size_);
    if (size_ > 0)
        std::memcpy (out_->data, bytes_, size_);
    return 0;
}

/**
 * @brief Build a native routing id from binary string bytes.
 * @param bytes_ Routing id bytes.
 * @param out_ Output routing id.
 * @return 0 on success, -1 on failure.
 */
inline int routing_id_from (const std::string &bytes_,
                            zlink_routing_id_t *out_)
{
    return routing_id_from (bytes_.data (), bytes_.size (), out_);
}

/**
 * @brief Convert native routing id bytes to a binary string.
 * @param routing_id_ Native routing id.
 * @return Binary string with exactly `routing_id_.size` bytes.
 */
inline std::string routing_id_to_string (const zlink_routing_id_t &routing_id_)
{
    const size_t size = static_cast<size_t> (routing_id_.size);
    if (size == 0)
        return std::string ();
    return std::string (
      reinterpret_cast<const char *> (routing_id_.data), size);
}

} // namespace zlink

#endif
