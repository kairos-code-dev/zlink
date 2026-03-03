/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TYPES_HPP_INCLUDED
#define ZLINK_CPP_TYPES_HPP_INCLUDED

#include "common.hpp"

namespace zlink
{

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

enum class send_flag : int
{
    none = 0,
    dontwait = ZLINK_DONTWAIT,
    sndmore = ZLINK_SNDMORE
};

inline send_flag operator| (send_flag a, send_flag b)
{
    return static_cast<send_flag> (static_cast<int> (a)
                                   | static_cast<int> (b));
}

enum class recv_flag : int
{
    none = 0,
    dontwait = ZLINK_DONTWAIT
};

inline recv_flag operator| (recv_flag a, recv_flag b)
{
    return static_cast<recv_flag> (static_cast<int> (a)
                                   | static_cast<int> (b));
}

enum class stream_dispatch_mode : int
{
    none = 0,
    len32be = ZLINK_STREAM_DISPATCH_LEN32BE
};

enum class error_code : int
{
    efsm = EFSM,
    enocompatproto = ENOCOMPATPROTO,
    eterm = ETERM,
    emthread = EMTHREAD
};

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

inline monitor_event operator| (monitor_event a, monitor_event b)
{
    return static_cast<monitor_event> (static_cast<int> (a)
                                       | static_cast<int> (b));
}

enum class disconnect_reason : int
{
    unknown = ZLINK_DISCONNECT_UNKNOWN,
    local = ZLINK_DISCONNECT_LOCAL,
    remote = ZLINK_DISCONNECT_REMOTE,
    handshake_failed = ZLINK_DISCONNECT_HANDSHAKE_FAILED,
    transport_error = ZLINK_DISCONNECT_TRANSPORT_ERROR,
    ctx_term = ZLINK_DISCONNECT_CTX_TERM
};

enum class poll_event : int
{
    pollin = ZLINK_POLLIN,
    pollout = ZLINK_POLLOUT,
    pollerr = ZLINK_POLLERR,
    pollpri = ZLINK_POLLPRI
};

inline poll_event operator| (poll_event a, poll_event b)
{
    return static_cast<poll_event> (static_cast<int> (a)
                                    | static_cast<int> (b));
}

enum class service_type : int
{
    gateway = ZLINK_SERVICE_TYPE_GATEWAY,
    spot = ZLINK_SERVICE_TYPE_SPOT
};

enum class gateway_lb_strategy : int
{
    round_robin = ZLINK_GATEWAY_LB_ROUND_ROBIN,
    weighted = ZLINK_GATEWAY_LB_WEIGHTED
};

enum class registry_socket_role : int
{
    pub = ZLINK_REGISTRY_SOCKET_PUB,
    router = ZLINK_REGISTRY_SOCKET_ROUTER,
    peer_sub = ZLINK_REGISTRY_SOCKET_PEER_SUB
};

enum class discovery_socket_role : int
{
    sub = ZLINK_DISCOVERY_SOCKET_SUB
};

enum class gateway_socket_role : int
{
    router = ZLINK_GATEWAY_SOCKET_ROUTER
};

enum class receiver_socket_role : int
{
    router = ZLINK_RECEIVER_SOCKET_ROUTER,
    dealer = ZLINK_RECEIVER_SOCKET_DEALER
};

enum class spot_node_socket_role : int
{
    node = ZLINK_SPOT_NODE_SOCKET_NODE,
    pub = ZLINK_SPOT_NODE_SOCKET_PUB,
    sub = ZLINK_SPOT_NODE_SOCKET_SUB,
    dealer = ZLINK_SPOT_NODE_SOCKET_DEALER
};

enum class spot_node_option : int
{
    pub_mode = ZLINK_SPOT_NODE_OPT_PUB_MODE,
    pub_queue_hwm = ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM,
    pub_queue_full_policy = ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY
};

enum class spot_node_pub_mode : int
{
    sync = ZLINK_SPOT_NODE_PUB_MODE_SYNC,
    async = ZLINK_SPOT_NODE_PUB_MODE_ASYNC
};

enum class spot_node_pub_queue_full_policy : int
{
    eagain = ZLINK_SPOT_NODE_PUB_QUEUE_FULL_EAGAIN,
    drop = ZLINK_SPOT_NODE_PUB_QUEUE_FULL_DROP
};

enum class spot_socket_role : int
{
    pub = 1,
    sub = 2
};

} // namespace zlink

#endif
