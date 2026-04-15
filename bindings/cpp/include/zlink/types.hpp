/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TYPES_HPP_INCLUDED
#define ZLINK_CPP_TYPES_HPP_INCLUDED

#include "common.hpp"
#include "message.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <stdexcept>

namespace zlink
{

class routing_id_t;
class received_t;
class topic_message_t;

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

class common_socket_options_t
{
  public:
    explicit common_socket_options_t (void *handle_ = NULL) noexcept
        : _handle (handle_)
    {
    }

    int linger () const;
    void linger (int value);
    int send_hwm () const;
    void send_hwm (int value);
    int recv_hwm () const;
    void recv_hwm (int value);
    int send_timeout () const;
    void send_timeout (int value);
    int recv_timeout () const;
    void recv_timeout (int value);
    bool immediate () const;
    void immediate (bool value);
    int connect_timeout () const;
    void connect_timeout (int value);
    bool ipv6 () const;
    void ipv6 (bool value);
    bool tcp_no_delay () const;
    void tcp_no_delay (bool value);
    bool tcp_keepalive () const;
    void tcp_keepalive (bool value);
    int heartbeat_interval () const;
    void heartbeat_interval (int value);
    int heartbeat_ttl () const;
    void heartbeat_ttl (int value);
    int heartbeat_timeout () const;
    void heartbeat_timeout (int value);
    int64_t max_message_size () const;
    void max_message_size (int64_t value);
    int backlog () const;
    void backlog (int value);
    int reconnect_interval () const;
    void reconnect_interval (int value);
    int reconnect_interval_max () const;
    void reconnect_interval_max (int value);
    std::string last_endpoint () const;

  private:
    void *_handle;
};

class router_socket_options_t
{
  public:
    explicit router_socket_options_t (void *handle_ = NULL) noexcept
        : _handle (handle_)
    {
    }

    bool mandatory () const;
    void mandatory (bool value);
    bool handover () const;
    void handover (bool value);
    bool probe_router () const;
    void probe_router (bool value);
    std::optional<routing_id_t> connect_routing_id () const;
    void connect_routing_id (const routing_id_t &value);

  private:
    void *_handle;
};

class dealer_socket_options_t
{
  public:
    explicit dealer_socket_options_t (void *handle_ = NULL) noexcept
        : _handle (handle_)
    {
    }

    bool probe_router () const;
    void probe_router (bool value);

  private:
    void *_handle;
};

class stream_socket_options_t
{
  public:
    explicit stream_socket_options_t (void *handle_ = NULL) noexcept
        : _handle (handle_)
    {
    }

    bool notify () const;
    void notify (bool value);

  private:
    void *_handle;
};

class pub_socket_options_t
{
  public:
    explicit pub_socket_options_t (void *handle_ = NULL) noexcept
        : _handle (handle_)
    {
    }

    bool verbose () const;
    void verbose (bool value);
    bool verboser () const;
    void verboser (bool value);
    bool no_drop () const;
    void no_drop (bool value);
    bool manual () const;
    void manual (bool value);

  private:
    void *_handle;
};

class sub_socket_options_t
{
  public:
    explicit sub_socket_options_t (void *handle_ = NULL) noexcept
        : _handle (handle_)
    {
    }

    int topics_count () const;

  private:
    void *_handle;
};

enum class send_flags_t : int
{
    none = 0,
    dontwait = 1
};

enum class recv_flags_t : int
{
    none = 0,
    dontwait = 1
};

enum class send_result_t : int
{
    sent = ZLINK_SUBMIT_OK,
    backpressured = ZLINK_SUBMIT_BACKPRESSURED,
    not_ready = ZLINK_SUBMIT_NOT_CONNECTED
};

enum class submit_result_t : int
{
    ok = ZLINK_SUBMIT_OK,
    backpressured = ZLINK_SUBMIT_BACKPRESSURED,
    not_connected = ZLINK_SUBMIT_NOT_CONNECTED,
    not_found = ZLINK_SUBMIT_NOT_FOUND,
    terminated = ZLINK_SUBMIT_TERMINATED,
    invalid_handle = ZLINK_SUBMIT_INVALID_HANDLE,
    invalid_argument = ZLINK_SUBMIT_INVALID_ARGUMENT,
    not_supported = ZLINK_SUBMIT_NOT_SUPPORTED,
    invalid_state = ZLINK_SUBMIT_INVALID_STATE,
    thread_violation = ZLINK_SUBMIT_THREAD_VIOLATION,
    out_of_memory = ZLINK_SUBMIT_OUT_OF_MEMORY,
    seq_exhausted = ZLINK_SUBMIT_SEQ_EXHAUSTED,
    internal_error = ZLINK_SUBMIT_INTERNAL_ERROR
};

enum class request_result_t : int
{
    ok = ZLINK_REQUEST_OK,
    timed_out = ZLINK_REQUEST_TIMED_OUT,
    not_found = ZLINK_REQUEST_NOT_FOUND,
    terminated = ZLINK_REQUEST_TERMINATED,
    protocol_error = ZLINK_REQUEST_PROTOCOL_ERROR
};

enum class recv_result_t : int
{
    ok = ZLINK_RECV_OK,
    no_data = ZLINK_RECV_NO_DATA,
    busy = ZLINK_RECV_BUSY,
    terminated = ZLINK_RECV_TERMINATED,
    invalid_handle = ZLINK_RECV_INVALID_HANDLE,
    not_supported = ZLINK_RECV_NOT_SUPPORTED
};

enum class handler_result_t : int
{
    ok = ZLINK_HANDLER_OK,
    invalid_argument = ZLINK_HANDLER_INVALID_ARGUMENT,
    busy = ZLINK_HANDLER_BUSY,
    not_supported = ZLINK_HANDLER_NOT_SUPPORTED,
    deadlock = ZLINK_HANDLER_DEADLOCK,
    invalid_handle = ZLINK_HANDLER_INVALID_HANDLE
};

enum class close_result_t : int
{
    ok = ZLINK_CLOSE_OK,
    busy = ZLINK_CLOSE_BUSY,
    shutdown = ZLINK_CLOSE_SHUTDOWN,
    invalid_handle = ZLINK_CLOSE_INVALID_HANDLE
};

enum class bind_result_t : int
{
    ok = ZLINK_BIND_OK,
    invalid_argument = ZLINK_BIND_INVALID_ARGUMENT,
    addr_in_use = ZLINK_BIND_ADDR_IN_USE,
    not_supported = ZLINK_BIND_NOT_SUPPORTED,
    invalid_handle = ZLINK_BIND_INVALID_HANDLE
};

enum class connect_result_t : int
{
    ok = ZLINK_CONNECT_OK,
    invalid_argument = ZLINK_CONNECT_INVALID_ARGUMENT,
    not_supported = ZLINK_CONNECT_NOT_SUPPORTED,
    invalid_handle = ZLINK_CONNECT_INVALID_HANDLE
};

enum class config_result_t : int
{
    ok = ZLINK_CONFIG_OK,
    invalid_handle = ZLINK_CONFIG_INVALID_HANDLE,
    invalid_argument = ZLINK_CONFIG_INVALID_ARGUMENT,
    not_supported = ZLINK_CONFIG_NOT_SUPPORTED
};

class routing_id_t
{
  public:
    routing_id_t () noexcept : _native ()
    {
        std::memset (&_native, 0, sizeof (_native));
    }

    routing_id_t (const uint8_t *bytes_, size_t size_) : routing_id_t ()
    {
        assign (bytes_, size_);
    }

    routing_id_t (const zlink_routing_id_t &native_) : _native (native_) {}

    static routing_id_t from_bytes (const uint8_t *bytes_, size_t size_)
    {
        return routing_id_t (bytes_, size_);
    }

    static routing_id_t from_bytes (const std::vector<uint8_t> &bytes_)
    {
        return routing_id_t (
          bytes_.empty () ? NULL : bytes_.data (), bytes_.size ());
    }

    const uint8_t *data () const noexcept { return _native.data; }
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

    std::string to_hex () const
    {
        static const char *digits = "0123456789abcdef";
        std::string hex;
        hex.resize (size () * 2u);
        for (size_t i = 0; i < size (); ++i) {
            const uint8_t byte = _native.data[i];
            hex[(i * 2u)] = digits[(byte >> 4u) & 0x0fu];
            hex[(i * 2u) + 1u] = digits[byte & 0x0fu];
        }
        return hex;
    }

    friend bool operator== (const routing_id_t &a_,
                            const routing_id_t &b_) noexcept
    {
        return a_._native.size == b_._native.size
               && std::memcmp (
                    a_._native.data, b_._native.data, a_._native.size)
                    == 0;
    }

    friend bool operator!= (const routing_id_t &a_,
                            const routing_id_t &b_) noexcept
    {
        return !(a_ == b_);
    }

    const zlink_routing_id_t &native () const noexcept { return _native; }
    operator zlink_routing_id_t () const noexcept { return _native; }

  private:
    void assign (const uint8_t *bytes_, size_t size_)
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

class received_t
{
  public:
    received_t () = default;

    received_t (std::optional<routing_id_t> routing_id_,
                std::optional<routing_id_t> spot_rid_,
                std::optional<uint64_t> request_seq_,
                std::vector<message_t> parts_,
                std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn_ =
                  std::function<void(std::vector<message_t> &, send_flags_t)> ())
        : _routing_id (std::move (routing_id_)),
          _spot_rid (std::move (spot_rid_)),
          _request_seq (std::move (request_seq_)),
          _parts (std::move (parts_)),
          _reply_fn (std::move (reply_fn_))
    {
    }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return _routing_id;
    }

    const std::optional<routing_id_t> &spot_rid () const noexcept
    {
        return _spot_rid;
    }

    const std::optional<uint64_t> &request_seq () const noexcept
    {
        return _request_seq;
    }

    const std::vector<message_t> &parts () const noexcept { return _parts; }
    std::vector<message_t> &parts () noexcept { return _parts; }

    bool is_single_part () const noexcept { return _parts.size () == 1u; }
    message_t &first_part ();
    message_t single_part_or_throw ();
    void reply (message_t &part) const;
    void reply (message_t &part, send_flags_t flags) const;
    void reply (std::vector<message_t> &parts) const;
    void reply (std::vector<message_t> &parts, send_flags_t flags) const;
    void close ();

  private:
    std::optional<routing_id_t> _routing_id;
    std::optional<routing_id_t> _spot_rid;
    std::optional<uint64_t> _request_seq;
    std::vector<message_t> _parts;
    std::function<void(std::vector<message_t> &, send_flags_t)> _reply_fn;
};

class topic_message_t
{
  public:
    topic_message_t () = default;

    topic_message_t (std::optional<routing_id_t> routing_id_,
                     std::string topic_,
                     std::vector<message_t> parts_)
        : topic_message_t (
            std::move (routing_id_), std::nullopt, std::move (topic_),
            std::move (parts_))
    {
    }

    topic_message_t (std::optional<routing_id_t> routing_id_,
                     std::optional<std::string> service_name_,
                     std::string topic_,
                     std::vector<message_t> parts_)
        : _routing_id (std::move (routing_id_)),
          _service_name (std::move (service_name_)),
          _topic (std::move (topic_)),
          _parts (std::move (parts_))
    {
    }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return _routing_id;
    }

    const std::optional<std::string> &service_name () const noexcept
    {
        return _service_name;
    }

    const std::string &topic () const noexcept { return _topic; }
    const std::vector<message_t> &parts () const noexcept { return _parts; }
    std::vector<message_t> &parts () noexcept { return _parts; }

    bool is_single_part () const noexcept { return _parts.size () == 1u; }
    message_t &first_part ();
    message_t single_part_or_throw ();
    void close ();

  private:
    std::optional<routing_id_t> _routing_id;
    std::optional<std::string> _service_name;
    std::string _topic;
    std::vector<message_t> _parts;
};

struct subscription_event_t
{
    subscription_event_t ()
        : routing_id (std::nullopt), service_name (std::nullopt), topic (),
          subscribed (false)
    {
    }

    std::optional<routing_id_t> routing_id;
    std::optional<std::string> service_name;
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

struct non_blocking_t
{
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

namespace service
{

enum class discovery_dealer_peer_mode_t : int
{
    router = ZLINK_DISCOVERY_DEALER_PEER_MODE_ROUTER,
    dealer = ZLINK_DISCOVERY_DEALER_PEER_MODE_DEALER
};

} // namespace service

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
    all = ZLINK_SERVICE_MONITOR_EVENT_ALL
};

inline service_monitor_event operator| (service_monitor_event a,
                                        service_monitor_event b)
{
    return static_cast<service_monitor_event> (
      static_cast<uint32_t> (a) | static_cast<uint32_t> (b));
}

struct monitor_event_t
{
    monitor_event_t ()
        : event (monitor_event::closed), value (0), routing_id (std::nullopt),
          local_addr (), remote_addr ()
    {
    }

    explicit monitor_event_t (const zlink_monitor_event_t &native_)
        : event (static_cast<monitor_event> (native_.event)),
          value (native_.value),
          routing_id (native_.routing_id.size > 0
                        ? std::optional<routing_id_t> (
                            routing_id_t (native_.routing_id))
                        : std::nullopt),
          local_addr (native_.local_addr),
          remote_addr (native_.remote_addr)
    {
    }

    monitor_event event;
    uint32_t value;
    std::optional<routing_id_t> routing_id;
    std::string local_addr;
    std::string remote_addr;
};

struct monitor_snapshot_t
{
    monitor_snapshot_t ()
        : source_kind (monitor_source_kind::socket), state_flags (0),
          detail_flags (0), snd_pending_msgs (0), rcv_pending_msgs (0)
    {
    }

    explicit monitor_snapshot_t (const zlink_monitor_snapshot_t &native_)
        : source_kind (
            static_cast<monitor_source_kind> (native_.source_kind)),
          state_flags (native_.state_flags),
          detail_flags (native_.detail_flags),
          snd_pending_msgs (native_.snd_pending_msgs),
          rcv_pending_msgs (native_.rcv_pending_msgs)
    {
    }

    bool is_ready () const noexcept
    {
        return (state_flags & ZLINK_MONITOR_STATE_READY) != 0u;
    }

    monitor_source_kind source_kind;
    uint32_t state_flags;
    uint32_t detail_flags;
    uint64_t snd_pending_msgs;
    uint64_t rcv_pending_msgs;
};

struct service_event_t
{
    service_event_t ()
        : service_kind (service_kind::socket), event_type (0), status (0),
          error_code (0), value (0), detail_flags (0), service_name (),
          endpoint (), routing_id (std::nullopt), subject (),
          subject_kind (service_event_subject_kind::none)
    {
    }

    explicit service_event_t (const zlink_service_event_t &native_)
        : service_kind (
            static_cast<zlink::service_kind> (native_.service_kind)),
          event_type (native_.event_type),
          status (native_.status),
          error_code (native_.error_code),
          value (native_.value),
          detail_flags (native_.detail_flags),
          service_name (native_.service_name),
          endpoint (native_.endpoint),
          routing_id (native_.routing_id.size > 0
                        ? std::optional<routing_id_t> (
                            routing_id_t (native_.routing_id))
                        : std::nullopt),
          subject (native_.subject),
          subject_kind (
            static_cast<service_event_subject_kind> (native_.subject_kind))
    {
    }

    zlink::service_kind service_kind;
    uint32_t event_type;
    uint32_t status;
    uint32_t error_code;
    uint64_t value;
    uint32_t detail_flags;
    std::string service_name;
    std::string endpoint;
    std::optional<routing_id_t> routing_id;
    std::string subject;
    service_event_subject_kind subject_kind;
};

using monitor_event_handler_fn = void (*) (const monitor_event_t *event_,
                                           void *userdata_);
using service_event_handler_fn = void (*) (const service_event_t *event_,
                                           void *userdata_);

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

template<size_t N> inline std::string fixed_string_to_string (const char (&src_)[N]);

enum class subject_kind : uint32_t
{
    none = ZLINK_SERVICE_EVENT_SUBJECT_NONE,
    topic = ZLINK_SERVICE_EVENT_SUBJECT_TOPIC,
    pattern = ZLINK_SERVICE_EVENT_SUBJECT_PATTERN
};

struct member_peer_entry_t
{
    member_peer_entry_t ()
        : service_type (service_type::socket), service_role (service_role::invalid),
          service_name (), endpoint (), routing_id (std::nullopt), value (0)
    {
    }

    explicit member_peer_entry_t (const zlink_member_peer_entry_t &entry_)
        : service_type (static_cast<zlink::service_type> (entry_.service_type)),
          service_role (static_cast<zlink::service_role> (entry_.service_role)),
          service_name (fixed_string_to_string (entry_.service_name)),
          endpoint (fixed_string_to_string (entry_.endpoint)),
          routing_id (entry_.routing_id.size > 0
                        ? std::optional<routing_id_t> (
                            routing_id_t (entry_.routing_id))
                        : std::nullopt),
          value (entry_.value)
    {
    }

    zlink::service_type service_type;
    zlink::service_role service_role;
    std::string service_name;
    std::string endpoint;
    std::optional<routing_id_t> routing_id;
    int64_t value;
};

struct registry_topology_entry_t
{
    registry_topology_entry_t ()
        : routing_id (std::nullopt), service_kind (service_kind::socket),
          service_role (service_role::invalid), service_name (), endpoint (),
          source (topology_source::manual), state (topology_state::discovered),
          desired_count (0), ready_count (0), error_code (0),
          last_reported_ms (0)
    {
    }

    explicit registry_topology_entry_t (
      const zlink_registry_topology_entry_t &entry_)
        : routing_id (entry_.routing_id.size > 0
                        ? std::optional<routing_id_t> (
                            routing_id_t (entry_.routing_id))
                        : std::nullopt),
          service_kind (static_cast<zlink::service_kind> (entry_.service_kind)),
          service_role (static_cast<zlink::service_role> (entry_.service_role)),
          service_name (fixed_string_to_string (entry_.service_name)),
          endpoint (fixed_string_to_string (entry_.endpoint)),
          source (static_cast<topology_source> (entry_.source)),
          state (static_cast<topology_state> (entry_.state)),
          desired_count (entry_.desired_count),
          ready_count (entry_.ready_count),
          error_code (entry_.error_code),
          last_reported_ms (entry_.last_reported_ms)
    {
    }

    std::optional<routing_id_t> routing_id;
    zlink::service_kind service_kind;
    zlink::service_role service_role;
    std::string service_name;
    std::string endpoint;
    topology_source source;
    topology_state state;
    uint32_t desired_count;
    uint32_t ready_count;
    uint32_t error_code;
    uint64_t last_reported_ms;
};

struct spot_node_status_t
{
    spot_node_status_t ()
        : service_name (), local_endpoint (), node_routing_id (std::nullopt),
          state (spot_node_state::idle), configured_peer_count (0),
          active_peer_count (0), connected_peer_count (0), subject_count (0),
          ready_subject_count (0), last_error (0), last_changed_ms (0)
    {
    }

    explicit spot_node_status_t (const zlink_spot_node_status_t &status_)
        : service_name (fixed_string_to_string (status_.service_name)),
          local_endpoint (fixed_string_to_string (status_.local_endpoint)),
          node_routing_id (status_.node_routing_id.size > 0
                             ? std::optional<routing_id_t> (
                                 routing_id_t (status_.node_routing_id))
                             : std::nullopt),
          state (static_cast<spot_node_state> (status_.state)),
          configured_peer_count (status_.configured_peer_count),
          active_peer_count (status_.active_peer_count),
          connected_peer_count (status_.connected_peer_count),
          subject_count (status_.subject_count),
          ready_subject_count (status_.ready_subject_count),
          last_error (status_.last_error),
          last_changed_ms (status_.last_changed_ms)
    {
    }

    std::string service_name;
    std::string local_endpoint;
    std::optional<routing_id_t> node_routing_id;
    spot_node_state state;
    uint32_t configured_peer_count;
    uint32_t active_peer_count;
    uint32_t connected_peer_count;
    uint32_t subject_count;
    uint32_t ready_subject_count;
    int32_t last_error;
    uint64_t last_changed_ms;
};

struct registry_service_summary_entry_t
{
    registry_service_summary_entry_t ()
        : service_kind (service_kind::socket),
          service_role (service_role::invalid), service_name (),
          total_count (0), connecting_count (0), ready_count (0),
          error_count (0), stopped_count (0), last_reported_ms (0)
    {
    }

    explicit registry_service_summary_entry_t (
      const zlink_registry_service_summary_entry_t &entry_)
        : service_kind (static_cast<zlink::service_kind> (entry_.service_kind)),
          service_role (static_cast<zlink::service_role> (entry_.service_role)),
          service_name (fixed_string_to_string (entry_.service_name)),
          total_count (entry_.total_count),
          connecting_count (entry_.connecting_count),
          ready_count (entry_.ready_count),
          error_count (entry_.error_count),
          stopped_count (entry_.stopped_count),
          last_reported_ms (entry_.last_reported_ms)
    {
    }

    zlink::service_kind service_kind;
    zlink::service_role service_role;
    std::string service_name;
    uint32_t total_count;
    uint32_t connecting_count;
    uint32_t ready_count;
    uint32_t error_count;
    uint32_t stopped_count;
    uint64_t last_reported_ms;
};

struct registry_service_summary_filter_t
{
    zlink::service_kind service_kind = service_kind::socket;
    zlink::service_role service_role = service_role::invalid;
    std::string service_name;
};

struct registry_status_t
{
    registry_status_t ()
        : registry_id (0), bind_endpoint (), state (registry_state::idle),
          topology_entry_count (0), peer_registry_count (0),
          connected_peer_registry_count (0), list_seq (0), last_error (0),
          last_changed_ms (0)
    {
    }

    explicit registry_status_t (const zlink_registry_status_t &status_)
        : registry_id (status_.registry_id),
          bind_endpoint (fixed_string_to_string (status_.bind_endpoint)),
          state (static_cast<registry_state> (status_.state)),
          topology_entry_count (status_.topology_entry_count),
          peer_registry_count (status_.peer_registry_count),
          connected_peer_registry_count (
            status_.connected_peer_registry_count),
          list_seq (status_.list_seq),
          last_error (status_.last_error),
          last_changed_ms (status_.last_changed_ms)
    {
    }

    uint32_t registry_id;
    std::string bind_endpoint;
    registry_state state;
    uint32_t topology_entry_count;
    uint32_t peer_registry_count;
    uint32_t connected_peer_registry_count;
    uint64_t list_seq;
    int32_t last_error;
    uint64_t last_changed_ms;
};

struct spot_node_peer_entry_t
{
    spot_node_peer_entry_t ()
        : service_name (), local_endpoint (), peer_endpoint (),
          source (spot_peer_source::manual), state (spot_peer_state::configured),
          connected_since_ms (0), last_changed_ms (0)
    {
    }

    explicit spot_node_peer_entry_t (const zlink_spot_node_peer_entry_t &entry_)
        : service_name (fixed_string_to_string (entry_.service_name)),
          local_endpoint (fixed_string_to_string (entry_.local_endpoint)),
          peer_endpoint (fixed_string_to_string (entry_.peer_endpoint)),
          source (static_cast<spot_peer_source> (entry_.source)),
          state (static_cast<spot_peer_state> (entry_.state)),
          connected_since_ms (entry_.connected_since_ms),
          last_changed_ms (entry_.last_changed_ms)
    {
    }

    std::string service_name;
    std::string local_endpoint;
    std::string peer_endpoint;
    spot_peer_source source;
    spot_peer_state state;
    uint64_t connected_since_ms;
    uint64_t last_changed_ms;
};

struct spot_node_peer_filter_t
{
    std::string peer_endpoint;
    spot_peer_source source = spot_peer_source::manual;
    spot_peer_state state = spot_peer_state::configured;
};

struct spot_node_subject_entry_t
{
    spot_node_subject_entry_t ()
        : role (spot_socket_role::pub), subject (),
          subject_kind (zlink::subject_kind::none), ready_peer_count (0),
          active_peer_count (0), last_changed_ms (0)
    {
    }

    explicit spot_node_subject_entry_t (
      const zlink_spot_node_subject_entry_t &entry_)
        : role (static_cast<spot_socket_role> (entry_.role)),
          subject (fixed_string_to_string (entry_.subject)),
          subject_kind (static_cast<zlink::subject_kind> (entry_.subject_kind)),
          ready_peer_count (entry_.ready_peer_count),
          active_peer_count (entry_.active_peer_count),
          last_changed_ms (entry_.last_changed_ms)
    {
    }

    spot_socket_role role;
    std::string subject;
    zlink::subject_kind subject_kind;
    uint32_t ready_peer_count;
    uint32_t active_peer_count;
    uint64_t last_changed_ms;
};

struct spot_node_subject_filter_t
{
    spot_socket_role role = spot_socket_role::pub;
    std::string subject;
    zlink::subject_kind subject_kind = zlink::subject_kind::none;
};

template<size_t N> inline std::string fixed_string_to_string (const char (&src_)[N]);

enum class spot_service_attachment_role_t : int
{
    router = ZLINK_SPOT_SERVICE_ATTACHMENT_ROUTER,
    pub = ZLINK_SPOT_SERVICE_ATTACHMENT_PUB,
    sub = ZLINK_SPOT_SERVICE_ATTACHMENT_SUB
};

struct spot_service_attachment_stats_t
{
    spot_service_attachment_stats_t ()
        : service_name (), router_count (0), pub_count (0), sub_count (0),
          auto_router_count (0), auto_pub_count (0), auto_sub_count (0)
    {
    }

    explicit spot_service_attachment_stats_t (
      const zlink_spot_service_attachment_stats_t &entry_)
        : service_name (fixed_string_to_string (entry_.service_name)),
          router_count (entry_.router_count), pub_count (entry_.pub_count),
          sub_count (entry_.sub_count),
          auto_router_count (entry_.auto_router_count),
          auto_pub_count (entry_.auto_pub_count),
          auto_sub_count (entry_.auto_sub_count)
    {
    }

    std::string service_name;
    uint32_t router_count;
    uint32_t pub_count;
    uint32_t sub_count;
    uint32_t auto_router_count;
    uint32_t auto_pub_count;
    uint32_t auto_sub_count;
};

struct spot_service_monitor_event_t
{
    spot_service_monitor_event_t ()
        : service_name (), role (spot_service_attachment_role_t::router),
          event ()
    {
    }

    explicit spot_service_monitor_event_t (
      const zlink_spot_service_monitor_event_t &event_)
        : service_name (fixed_string_to_string (event_.service_name)),
          role (static_cast<spot_service_attachment_role_t> (event_.role)),
          event (event_.event)
    {
    }

    std::string service_name;
    spot_service_attachment_role_t role;
    monitor_event_t event;
};

struct registry_topology_filter_t
{
    zlink::service_kind service_kind = service_kind::socket;
    zlink::service_role service_role = service_role::invalid;
    std::string service_name;
    std::optional<routing_id_t> routing_id;
    topology_state state = topology_state::discovered;
    topology_source source = topology_source::manual;
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
        *out_ = routing_id_t::from_bytes (
          static_cast<const uint8_t *> (bytes_), size_);
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
        const routing_id_t routing_id = routing_id_t::from_bytes (
          static_cast<const uint8_t *> (bytes_), size_);
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
    return routing_id_t (routing_id_).to_string ();
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

namespace std
{

template<> struct hash<zlink::routing_id_t>
{
    size_t operator() (const zlink::routing_id_t &rid_) const noexcept
    {
        size_t seed = 1469598103934665603ull;
        const uint8_t *data = rid_.data ();
        for (size_t i = 0; i < rid_.size (); ++i) {
            seed ^= static_cast<size_t> (data[i]);
            seed *= 1099511628211ull;
        }
        return seed;
    }
};

} // namespace std

#endif
