/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TYPES_HPP_INCLUDED
#define ZLINK_CPP_TYPES_HPP_INCLUDED

#include "common.hpp"
#include "message.hpp"

#include <cerrno>
#include <stdexcept>

namespace zlink
{

enum class socket_type : int
{
    pair = ZLINK_SOCKET_PAIR,
    pub = ZLINK_SOCKET_PUB,
    sub = ZLINK_SOCKET_SUB,
    dealer = ZLINK_SOCKET_DEALER,
    router = ZLINK_SOCKET_ROUTER,
    xpub = ZLINK_SOCKET_XPUB,
    xsub = ZLINK_SOCKET_XSUB,
    stream = ZLINK_SOCKET_STREAM
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
    thread_name_prefix = ZLINK_THREAD_NAME_PREFIX,
    blocky = ZLINK_CTX_OPT_BLOCKY
};

enum class socket_option : int
{
    affinity = ZLINK_OPT_AFFINITY,
    rate = ZLINK_OPT_RATE,
    recovery_ivl = ZLINK_OPT_RECOVERY_IVL,
    sndbuf = ZLINK_OPT_SNDBUF,
    rcvbuf = ZLINK_OPT_RCVBUF,
    fd = ZLINK_OPT_FD,
    events = ZLINK_OPT_EVENTS,
    type = ZLINK_OPT_TYPE,
    linger = ZLINK_OPT_LINGER,
    reconnect_ivl = ZLINK_OPT_RECONNECT_IVL,
    backlog = ZLINK_OPT_BACKLOG,
    reconnect_ivl_max = ZLINK_OPT_RECONNECT_IVL_MAX,
    maxmsgsize = ZLINK_OPT_MAXMSGSIZE,
    sndhwm = ZLINK_OPT_SNDHWM,
    rcvhwm = ZLINK_OPT_RCVHWM,
    multicast_hops = ZLINK_OPT_MULTICAST_HOPS,
    rcvtimeo = ZLINK_OPT_RCVTIMEO,
    sndtimeo = ZLINK_OPT_SNDTIMEO,
    last_endpoint = ZLINK_OPT_LAST_ENDPOINT,
    tcp_keepalive = ZLINK_OPT_TCP_KEEPALIVE,
    tcp_keepalive_cnt = ZLINK_OPT_TCP_KEEPALIVE_CNT,
    tcp_keepalive_idle = ZLINK_OPT_TCP_KEEPALIVE_IDLE,
    tcp_keepalive_intvl = ZLINK_OPT_TCP_KEEPALIVE_INTVL,
    tcp_nodelay = ZLINK_OPT_TCP_NODELAY,
    immediate = ZLINK_OPT_IMMEDIATE,
    ipv6 = ZLINK_OPT_IPV6,
    conflate = ZLINK_OPT_CONFLATE,
    tos = ZLINK_OPT_TOS,
    handshake_ivl = ZLINK_OPT_HANDSHAKE_IVL,
    blocky = ZLINK_OPT_BLOCKY,
    invert_matching = ZLINK_OPT_INVERT_MATCHING,
    heartbeat_ivl = ZLINK_OPT_HEARTBEAT_IVL,
    heartbeat_ttl = ZLINK_OPT_HEARTBEAT_TTL,
    heartbeat_timeout = ZLINK_OPT_HEARTBEAT_TIMEOUT,
    connect_timeout = ZLINK_OPT_CONNECT_TIMEOUT,
    tcp_maxrt = ZLINK_OPT_TCP_MAXRT,
    multicast_maxtpdu = ZLINK_OPT_MULTICAST_MAXTPDU,
    bindtodevice = ZLINK_OPT_BINDTODEVICE,
    tls_cert = ZLINK_OPT_TLS_CERT,
    tls_key = ZLINK_OPT_TLS_KEY,
    tls_ca = ZLINK_OPT_TLS_CA,
    tls_verify = ZLINK_OPT_TLS_VERIFY,
    tls_require_client_cert = ZLINK_OPT_TLS_REQUIRE_CLIENT_CERT,
    tls_hostname = ZLINK_OPT_TLS_HOSTNAME,
    tls_trust_system = ZLINK_OPT_TLS_TRUST_SYSTEM,
    tls_password = ZLINK_OPT_TLS_PASSWORD,
    zmp_metadata = ZLINK_OPT_ZMP_METADATA,
    discovery_metadata_max_size = ZLINK_OPT_DISCOVERY_METADATA_MAX_SIZE
};

enum class router_option : int
{
    mandatory = ZLINK_ROUTER_OPT_MANDATORY,
    handover = ZLINK_ROUTER_OPT_HANDOVER,
    probe = ZLINK_ROUTER_OPT_PROBE,
    connect_routing_id = ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID
};

enum class dealer_option : int
{
    probe = ZLINK_DEALER_OPT_PROBE
};

enum class pub_option : int
{
    verbose = ZLINK_PUB_OPT_VERBOSE,
    verboser = ZLINK_PUB_OPT_VERBOSER,
    manual = ZLINK_PUB_OPT_MANUAL,
    manual_last_value = ZLINK_PUB_OPT_MANUAL_LAST_VALUE,
    nodrop = ZLINK_PUB_OPT_NODROP,
    welcome_msg = ZLINK_PUB_OPT_WELCOME_MSG,
    topics_count = ZLINK_PUB_OPT_TOPICS_COUNT,
    approve_subscribe = ZLINK_PUB_OPT_APPROVE_SUBSCRIBE,
    reject_subscribe = ZLINK_PUB_OPT_REJECT_SUBSCRIBE
};

enum class sub_option : int
{
    topics_count = ZLINK_SUB_OPT_TOPICS_COUNT
};

enum class stream_option : int
{
    notify = ZLINK_STREAM_OPT_NOTIFY
};

template<typename T> struct socket_option_key_t
{
    explicit constexpr socket_option_key_t (socket_option option_)
        : option (option_)
    {
    }

    socket_option option;
};

template<typename T> struct router_option_key_t
{
    explicit constexpr router_option_key_t (router_option option_)
        : option (option_)
    {
    }

    router_option option;
};

template<typename T> struct dealer_option_key_t
{
    explicit constexpr dealer_option_key_t (dealer_option option_)
        : option (option_)
    {
    }

    dealer_option option;
};

template<typename T> struct pub_option_key_t
{
    explicit constexpr pub_option_key_t (pub_option option_)
        : option (option_)
    {
    }

    pub_option option;
};

template<typename T> struct sub_option_key_t
{
    explicit constexpr sub_option_key_t (sub_option option_)
        : option (option_)
    {
    }

    sub_option option;
};

template<typename T> struct stream_option_key_t
{
    explicit constexpr stream_option_key_t (stream_option option_)
        : option (option_)
    {
    }

    stream_option option;
};

inline void validate_no_embedded_null (const std::string &value_,
                                       const char *field_name_)
{
    if (value_.find ('\0') != std::string::npos)
        throw std::invalid_argument (
          std::string (field_name_) + " must not contain embedded null");
}

inline void validate_bounded_c_string (const std::string &value_,
                                       size_t max_bytes_,
                                       const char *field_name_)
{
    validate_no_embedded_null (value_, field_name_);
    if (value_.size () > max_bytes_) {
        throw std::invalid_argument (
          std::string (field_name_) + " exceeds " + std::to_string (max_bytes_)
          + " bytes");
    }
}

namespace socket_options
{
static const socket_option_key_t<uint64_t> affinity (socket_option::affinity);
static const socket_option_key_t<int> rate (socket_option::rate);
static const socket_option_key_t<int> recovery_ivl (
  socket_option::recovery_ivl);
static const socket_option_key_t<int> sndbuf (socket_option::sndbuf);
static const socket_option_key_t<int> rcvbuf (socket_option::rcvbuf);
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
static const socket_option_key_t<int> ipv6 (socket_option::ipv6);
static const socket_option_key_t<int> conflate (socket_option::conflate);
static const socket_option_key_t<int> tos (socket_option::tos);
static const socket_option_key_t<int> handshake_ivl (
  socket_option::handshake_ivl);
static const socket_option_key_t<int> blocky (socket_option::blocky);
static const socket_option_key_t<int> invert_matching (
  socket_option::invert_matching);
static const socket_option_key_t<int> heartbeat_ivl (
  socket_option::heartbeat_ivl);
static const socket_option_key_t<int> heartbeat_ttl (
  socket_option::heartbeat_ttl);
static const socket_option_key_t<int> heartbeat_timeout (
  socket_option::heartbeat_timeout);
static const socket_option_key_t<int> connect_timeout (
  socket_option::connect_timeout);
static const socket_option_key_t<int> tcp_maxrt (socket_option::tcp_maxrt);
static const socket_option_key_t<int> multicast_maxtpdu (
  socket_option::multicast_maxtpdu);
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
static const socket_option_key_t<int> zmp_metadata (
  socket_option::zmp_metadata);
} // namespace socket_options

namespace router_options
{
static const router_option_key_t<int> mandatory (router_option::mandatory);
static const router_option_key_t<int> handover (router_option::handover);
static const router_option_key_t<int> probe (router_option::probe);
static const router_option_key_t<std::string> connect_routing_id (
  router_option::connect_routing_id);
} // namespace router_options

namespace dealer_options
{
static const dealer_option_key_t<int> probe (dealer_option::probe);
} // namespace dealer_options

namespace pub_options
{
static const pub_option_key_t<int> verbose (pub_option::verbose);
static const pub_option_key_t<int> verboser (pub_option::verboser);
static const pub_option_key_t<int> manual (pub_option::manual);
static const pub_option_key_t<int> manual_last_value (
  pub_option::manual_last_value);
static const pub_option_key_t<int> nodrop (pub_option::nodrop);
static const pub_option_key_t<std::string> welcome_msg (
  pub_option::welcome_msg);
static const pub_option_key_t<int> topics_count (pub_option::topics_count);
static const pub_option_key_t<std::string> approve_subscribe (
  pub_option::approve_subscribe);
static const pub_option_key_t<std::string> reject_subscribe (
  pub_option::reject_subscribe);
} // namespace pub_options

namespace sub_options
{
static const sub_option_key_t<int> topics_count (sub_option::topics_count);
} // namespace sub_options

namespace stream_options
{
static const stream_option_key_t<int> notify (stream_option::notify);
} // namespace stream_options

struct common_socket_options_t
{
    inline static const socket_option_key_t<int> linger =
      socket_options::linger;
    inline static const socket_option_key_t<int> sndhwm =
      socket_options::sndhwm;
    inline static const socket_option_key_t<int> rcvhwm =
      socket_options::rcvhwm;
    inline static const socket_option_key_t<int> sndtimeo =
      socket_options::sndtimeo;
    inline static const socket_option_key_t<int> rcvtimeo =
      socket_options::rcvtimeo;
    inline static const socket_option_key_t<int> immediate =
      socket_options::immediate;
    inline static const socket_option_key_t<int> connect_timeout =
      socket_options::connect_timeout;
    inline static const socket_option_key_t<int> ipv6 =
      socket_options::ipv6;
    inline static const socket_option_key_t<int> tcp_nodelay =
      socket_options::tcp_nodelay;
    inline static const socket_option_key_t<int> tcp_keepalive =
      socket_options::tcp_keepalive;
    inline static const socket_option_key_t<int> heartbeat_ivl =
      socket_options::heartbeat_ivl;
    inline static const socket_option_key_t<int> heartbeat_ttl =
      socket_options::heartbeat_ttl;
    inline static const socket_option_key_t<int> heartbeat_timeout =
      socket_options::heartbeat_timeout;
    inline static const socket_option_key_t<int64_t> maxmsgsize =
      socket_options::maxmsgsize;
    inline static const socket_option_key_t<int> backlog =
      socket_options::backlog;
    inline static const socket_option_key_t<int> reconnect_ivl =
      socket_options::reconnect_ivl;
    inline static const socket_option_key_t<int> reconnect_ivl_max =
      socket_options::reconnect_ivl_max;
};

struct router_socket_options_t
{
    inline static const router_option_key_t<int> mandatory =
      router_options::mandatory;
    inline static const router_option_key_t<int> handover =
      router_options::handover;
    inline static const router_option_key_t<int> probe =
      router_options::probe;
    inline static const router_option_key_t<std::string> connect_routing_id =
      router_options::connect_routing_id;
};

struct dealer_socket_options_t
{
    inline static const dealer_option_key_t<int> probe =
      dealer_options::probe;
};

struct stream_socket_options_t
{
    inline static const stream_option_key_t<int> notify =
      stream_options::notify;
};

struct pub_socket_options_t
{
    inline static const pub_option_key_t<int> verbose = pub_options::verbose;
    inline static const pub_option_key_t<int> verboser = pub_options::verboser;
    inline static const pub_option_key_t<int> nodrop = pub_options::nodrop;
    inline static const pub_option_key_t<int> manual = pub_options::manual;
};

struct sub_socket_options_t
{
    inline static const sub_option_key_t<int> topics_count =
      sub_options::topics_count;
};

enum class send_flag : int
{
    none = 0,
    dontwait = ZLINK_DONTWAIT,
    sndmore = 0x0002
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

enum class send_result_t : int
{
    sent = ZLINK_SEND_RESULT_SENT,
    backpressured = ZLINK_SEND_RESULT_BACKPRESSURED,
    not_ready = ZLINK_SEND_RESULT_NOT_READY
};

class routing_id_t
{
  public:
    routing_id_t () noexcept : _native ()
    {
        std::memset (&_native, 0, sizeof (_native));
    }

    explicit routing_id_t (const std::string &bytes_) : routing_id_t ()
    {
        assign (bytes_.data (), bytes_.size ());
    }

    routing_id_t (const void *bytes_, size_t size_) : routing_id_t ()
    {
        assign (bytes_, size_);
    }

    routing_id_t (const zlink_routing_id_t &native_) : _native (native_) {}

    size_t size () const noexcept { return _native.size; }
    bool empty () const noexcept { return _native.size == 0; }

    std::vector<uint8_t> to_bytes () const
    {
        return std::vector<uint8_t> (
          _native.data, _native.data + static_cast<size_t> (_native.size));
    }

    std::string to_string () const
    {
        return std::string (
          reinterpret_cast<const char *> (_native.data),
          static_cast<size_t> (_native.size));
    }

    const zlink_routing_id_t &native () const noexcept { return _native; }
    operator zlink_routing_id_t () const noexcept { return _native; }

  private:
    void assign (const void *bytes_, size_t size_)
    {
        if (size_ > sizeof (_native.data))
            throw std::invalid_argument ("routing id exceeds 255 bytes");
        if (size_ > 0 && !bytes_)
            throw std::invalid_argument (
              "routing id bytes must not be null for non-empty input");

        std::memset (&_native, 0, sizeof (_native));
        _native.size = static_cast<uint8_t> (size_);
        if (size_ > 0)
            std::memcpy (_native.data, bytes_, size_);
    }

    zlink_routing_id_t _native;

    friend inline zlink_routing_id_t *routing_id_native (routing_id_t &) noexcept;
    friend inline const zlink_routing_id_t *
    routing_id_native (const routing_id_t &) noexcept;
};

inline zlink_routing_id_t empty_routing_id () noexcept
{
    zlink_routing_id_t routing_id;
    std::memset (&routing_id, 0, sizeof (routing_id));
    return routing_id;
}

inline zlink_routing_id_t *routing_id_native (routing_id_t &routing_id_) noexcept
{
    return &routing_id_._native;
}

inline const zlink_routing_id_t *
routing_id_native (const routing_id_t &routing_id_) noexcept
{
    return &routing_id_._native;
}

struct received_t
{
    routing_id_t routing_id;
    std::vector<message_t> parts;
};

struct subscribed_t
{
    routing_id_t routing_id;
    std::string topic;
    std::vector<message_t> parts;
};

struct subscription_event_t
{
    subscription_event_t () : routing_id (), subscribed (false) {}

    routing_id_t routing_id;
    std::string topic;
    bool subscribed;
};

template<typename T> class maybe_t
{
  public:
    maybe_t () : _has_value (false), _value () {}

    maybe_t (const T &value_) : _has_value (true), _value (value_) {}

    maybe_t (T &&value_) : _has_value (true), _value (std::move (value_)) {}

    explicit operator bool () const noexcept { return _has_value; }
    bool has_value () const noexcept { return _has_value; }

    T &value () noexcept { return _value; }
    const T &value () const noexcept { return _value; }

    T &operator* () noexcept { return _value; }
    const T &operator* () const noexcept { return _value; }

    T *operator-> () noexcept { return &_value; }
    const T *operator-> () const noexcept { return &_value; }

  private:
    bool _has_value;
    T _value;
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
    zmp_malformed_command_hello =
      ZLINK_PROTOCOL_ERROR_ZMP_MALFORMED_COMMAND_HELLO
};

enum class monitor_event : uint32_t
{
    connected = ZLINK_SOCKET_MONITOR_EVENT_CONNECTED,
    connect_delayed = ZLINK_SOCKET_MONITOR_EVENT_CONNECT_DELAYED,
    connect_retried = ZLINK_SOCKET_MONITOR_EVENT_CONNECT_RETRIED,
    listening = ZLINK_SOCKET_MONITOR_EVENT_LISTENING,
    bind_failed = ZLINK_SOCKET_MONITOR_EVENT_BIND_FAILED,
    accepted = ZLINK_SOCKET_MONITOR_EVENT_ACCEPTED,
    accept_failed = ZLINK_SOCKET_MONITOR_EVENT_ACCEPT_FAILED,
    closed = ZLINK_SOCKET_MONITOR_EVENT_CLOSED,
    close_failed = ZLINK_SOCKET_MONITOR_EVENT_CLOSE_FAILED,
    disconnected = ZLINK_SOCKET_MONITOR_EVENT_DISCONNECTED,
    monitor_stopped = ZLINK_SOCKET_MONITOR_EVENT_MONITOR_STOPPED,
    handshake_failed_no_detail =
      ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_NO_DETAIL,
    connection_ready = ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY,
    connection_ready_changed =
      ZLINK_SOCKET_MONITOR_EVENT_CONNECTION_READY,
    handshake_failed_protocol =
      ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_PROTOCOL,
    handshake_failed_auth = ZLINK_SOCKET_MONITOR_EVENT_HANDSHAKE_FAILED_AUTH,
    all = ZLINK_SOCKET_MONITOR_EVENT_ALL
};

inline monitor_event operator| (monitor_event a, monitor_event b)
{
    return static_cast<monitor_event> (static_cast<uint32_t> (a)
                                       | static_cast<uint32_t> (b));
}

enum class monitor_source_kind : int
{
    socket = ZLINK_MONITOR_SOURCE_SOCKET,
    spot_pub = ZLINK_MONITOR_SOURCE_SPOT_PUB,
    spot_sub = ZLINK_MONITOR_SOURCE_SPOT_SUB
};

enum class monitor_state : uint32_t
{
    ready = ZLINK_MONITOR_STATE_READY,
    bound_ready = ZLINK_MONITOR_STATE_BOUND_READY,
    send_ready = ZLINK_MONITOR_STATE_READY,
    closed = ZLINK_MONITOR_STATE_CLOSED
};

inline monitor_state operator| (monitor_state a, monitor_state b)
{
    return static_cast<monitor_state> (static_cast<uint32_t> (a)
                                       | static_cast<uint32_t> (b));
}

enum class monitor_snapshot_detail : uint32_t
{
    snd_pending_msgs = ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS,
    rcv_pending_msgs = ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS
};

inline monitor_snapshot_detail operator| (monitor_snapshot_detail a,
                                          monitor_snapshot_detail b)
{
    return static_cast<monitor_snapshot_detail> (
      static_cast<uint32_t> (a) | static_cast<uint32_t> (b));
}

enum class disconnect_reason : int
{
    unknown = ZLINK_DISCONNECT_REASON_UNKNOWN,
    handshake_failed = ZLINK_DISCONNECT_REASON_HANDSHAKE_FAILED,
    transport_error = ZLINK_DISCONNECT_REASON_TRANSPORT_ERROR,
    ctx_term = ZLINK_DISCONNECT_REASON_CTX_TERM
};

enum class poll_event : short
{
    pollin = ZLINK_POLLIN,
    pollout = ZLINK_POLLOUT,
    pollerr = ZLINK_POLLERR,
    pollpri = ZLINK_POLLPRI
};

inline poll_event operator| (poll_event a, poll_event b)
{
    return static_cast<poll_event> (static_cast<short> (a)
                                    | static_cast<short> (b));
}

enum class service_type : int
{
    spot = ZLINK_SERVICE_TYPE_SPOT,
    socket = ZLINK_SERVICE_TYPE_SOCKET
};

enum class service_role : int
{
    invalid = ZLINK_SERVICE_ROLE_INVALID,
    spot = ZLINK_SERVICE_ROLE_SPOT,
    router = ZLINK_SERVICE_ROLE_ROUTER,
    dealer = ZLINK_SERVICE_ROLE_DEALER,
    pub = ZLINK_SERVICE_ROLE_PUB,
    sub = ZLINK_SERVICE_ROLE_SUB
};

enum class service_kind : int
{
    discovery = ZLINK_SERVICE_KIND_DISCOVERY,
    spot_sub = ZLINK_SERVICE_KIND_SPOT_SUB,
    spot_pub = ZLINK_SERVICE_KIND_SPOT_PUB,
    socket = ZLINK_SERVICE_KIND_SOCKET
};

enum class service_event_subject_kind : int
{
    none = ZLINK_SERVICE_EVENT_SUBJECT_NONE,
    topic = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC,
    pattern = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN
};

enum class monitor_target_kind : int
{
    socket = ZLINK_MONITOR_TARGET_SOCKET,
    discovery = ZLINK_MONITOR_TARGET_DISCOVERY,
    spot = ZLINK_MONITOR_TARGET_SPOT,
    spot_node = ZLINK_MONITOR_TARGET_SPOT_NODE
};

enum class service_monitor_event : uint32_t
{
    error = ZLINK_SERVICE_MONITOR_EVENT_ERROR,
    closed = ZLINK_SERVICE_MONITOR_EVENT_CLOSED,
    discovery_service_up = ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_UP,
    discovery_service_down =
      ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_SERVICE_DOWN,
    discovery_providers_changed =
      ZLINK_SERVICE_MONITOR_EVENT_DISCOVERY_PROVIDERS_CHANGED,
    peer_up = ZLINK_SERVICE_MONITOR_EVENT_SPOT_PEER_UP,
    peer_down = ZLINK_SERVICE_MONITOR_EVENT_SPOT_PEER_DOWN,
    spot_filter_applied = ZLINK_SERVICE_MONITOR_EVENT_SPOT_FILTER_APPLIED,
    all = ZLINK_SERVICE_MONITOR_EVENT_ALL
};

inline service_monitor_event operator| (service_monitor_event a,
                                        service_monitor_event b)
{
    return static_cast<service_monitor_event> (
      static_cast<uint32_t> (a) | static_cast<uint32_t> (b));
}

enum class registry_socket_role : int
{
    pub = 1,
    router = 2,
    peer_sub = 3
};

enum class discovery_socket_role : int
{
    sub = 0
};

enum class spot_node_socket_role : int
{
    node = 0,
    pub = 1,
    sub = 2,
    dealer = 3
};

enum class spot_socket_role : int
{
    pub = ZLINK_SPOT_ROLE_PUB,
    sub = ZLINK_SPOT_ROLE_SUB
};

enum class spot_node_state : int
{
    idle = ZLINK_SPOT_NODE_STATE_IDLE,
    connecting = ZLINK_SPOT_NODE_STATE_CONNECTING,
    partial_ready = ZLINK_SPOT_NODE_STATE_PARTIAL_READY,
    ready = ZLINK_SPOT_NODE_STATE_READY,
    error = ZLINK_SPOT_NODE_STATE_ERROR
};

enum class spot_peer_source : int
{
    manual = ZLINK_SPOT_PEER_SOURCE_MANUAL,
    discovery = ZLINK_SPOT_PEER_SOURCE_DISCOVERY,
    mixed = ZLINK_SPOT_PEER_SOURCE_MIXED
};

enum class spot_peer_state : int
{
    configured = ZLINK_SPOT_PEER_STATE_CONFIGURED,
    connecting = ZLINK_SPOT_PEER_STATE_CONNECTING,
    connected = ZLINK_SPOT_PEER_STATE_CONNECTED
};

enum class registry_state : int
{
    idle = ZLINK_REGISTRY_STATE_IDLE,
    active = ZLINK_REGISTRY_STATE_ACTIVE,
    degraded = ZLINK_REGISTRY_STATE_DEGRADED,
    error = ZLINK_REGISTRY_STATE_ERROR
};

enum class topology_source : int
{
    manual = ZLINK_TOPOLOGY_SOURCE_MANUAL,
    discovery = ZLINK_TOPOLOGY_SOURCE_DISCOVERY,
    registry = ZLINK_TOPOLOGY_SOURCE_REGISTRY
};

enum class topology_state : int
{
    discovered = ZLINK_TOPOLOGY_STATE_DISCOVERED,
    connecting = ZLINK_TOPOLOGY_STATE_CONNECTING,
    ready = ZLINK_TOPOLOGY_STATE_READY,
    lost = ZLINK_TOPOLOGY_STATE_LOST,
    error = ZLINK_TOPOLOGY_STATE_ERROR,
    stopped = ZLINK_TOPOLOGY_STATE_STOPPED
};

template<size_t N> inline std::string fixed_string_to_string (const char (&src_)[N])
{
    size_t len = 0;
    while (len < N && src_[len] != '\0')
        ++len;
    return std::string (src_, len);
}

inline int routing_id_from (const void *bytes_,
                            size_t size_,
                            routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    try {
        *out_ = routing_id_t (bytes_, size_);
    } catch (const std::invalid_argument &) {
        errno = size_ > 255u ? EMSGSIZE : EINVAL;
        return -1;
    }
    return 0;
}

inline int routing_id_from (const std::string &bytes_,
                            routing_id_t *out_)
{
    return routing_id_from (bytes_.data (), bytes_.size (), out_);
}

inline int routing_id_from (const void *bytes_,
                            size_t size_,
                            zlink_routing_id_t *out_)
{
    if (!out_) {
        errno = EINVAL;
        return -1;
    }

    try {
        const routing_id_t routing_id (bytes_, size_);
        *out_ = *routing_id_native (routing_id);
    } catch (const std::invalid_argument &) {
        errno = size_ > 255u ? EMSGSIZE : EINVAL;
        return -1;
    }
    return 0;
}

inline int routing_id_from (const std::string &bytes_,
                            zlink_routing_id_t *out_)
{
    return routing_id_from (bytes_.data (), bytes_.size (), out_);
}

inline std::vector<uint8_t> routing_id_to_bytes (const routing_id_t &routing_id_)
{
    return routing_id_.to_bytes ();
}

inline std::vector<uint8_t>
routing_id_to_bytes (const zlink_routing_id_t &routing_id_)
{
    return std::vector<uint8_t> (
      routing_id_.data, routing_id_.data + routing_id_.size);
}

inline std::string routing_id_to_string (const routing_id_t &routing_id_)
{
    return routing_id_.to_string ();
}

inline std::string routing_id_to_string (const zlink_routing_id_t &routing_id_)
{
    return routing_id_t (routing_id_.data, routing_id_.size).to_string ();
}

inline std::string
service_name (const zlink_service_event_t &event_)
{
    return fixed_string_to_string (event_.service_name);
}

inline std::string endpoint (const zlink_service_event_t &event_)
{
    return fixed_string_to_string (event_.endpoint);
}

inline std::string subject (const zlink_service_event_t &event_)
{
    return fixed_string_to_string (event_.subject);
}

inline std::string
service_name (const zlink_member_peer_entry_t &entry_)
{
    return fixed_string_to_string (entry_.service_name);
}

inline std::string endpoint (const zlink_member_peer_entry_t &entry_)
{
    return fixed_string_to_string (entry_.endpoint);
}

inline std::string
service_name (const zlink_registry_topology_entry_t &entry_)
{
    return fixed_string_to_string (entry_.service_name);
}

inline std::string
endpoint (const zlink_registry_topology_entry_t &entry_)
{
    return fixed_string_to_string (entry_.endpoint);
}

} // namespace zlink

#endif
