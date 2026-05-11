/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_TYPES_HPP_INCLUDED
#define ZLINK_CPP_TYPES_HPP_INCLUDED

#include "common.hpp"
#include "message.hpp"

#include <chrono>
#include <cerrno>
#include <climits>
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
class actor_ref_t;
class received_t;
class topic_message_t;
class dealer_socket_t;
class timer_t;
namespace service
{
class spot_node_t;
class spot_t;
}

enum class socket_type : int
{
    any = ZLINK_SOCKET_ANY,
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
    blocky = ZLINK_CTX_OPT_BLOCKY,
    auto_hwm_enable = ZLINK_CTX_OPT_AUTO_HWM_ENABLE,
    auto_hwm_recalc_debounce_ms =
      ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS,
    auto_hwm_profile = ZLINK_CTX_OPT_AUTO_HWM_PROFILE
};

enum class auto_hwm_profile : int
{
    compact = ZLINK_AUTO_HWM_PROFILE_COMPACT,
    low_latency = ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY,
    balanced = ZLINK_AUTO_HWM_PROFILE_BALANCED,
    throughput = ZLINK_AUTO_HWM_PROFILE_THROUGHPUT
};

enum class thread_scheduling_policy_t : int
{
    default_policy = ZLINK_THREAD_SCHED_POLICY_DFLT,
    other = 0,
    fifo = 1,
    round_robin = 2
};

enum class rid_duplicate_policy_t : int
{
    reject = ZLINK_RID_DUPLICATE_REJECT,
    handover = ZLINK_RID_DUPLICATE_HANDOVER,
    replace = ZLINK_RID_DUPLICATE_HANDOVER
};

class io_thread_count_t
{
  public:
    static io_thread_count_t value (int value_) noexcept
    {
        return io_thread_count_t (value_);
    }

    int value () const noexcept { return _value; }

  private:
    explicit io_thread_count_t (int value_) noexcept : _value (value_) {}

    int _value;
};

class socket_count_t
{
  public:
    static socket_count_t value (int value_) noexcept
    {
        return socket_count_t (value_);
    }

    int value () const noexcept { return _value; }

  private:
    explicit socket_count_t (int value_) noexcept : _value (value_) {}

    int _value;
};

class worker_count_t
{
  public:
    static worker_count_t value (int value_) noexcept
    {
        return worker_count_t (value_);
    }

    int value () const noexcept { return _value; }

  private:
    explicit worker_count_t (int value_) noexcept : _value (value_) {}

    int _value;
};

class thread_priority_t
{
  public:
    static thread_priority_t value (int value_) noexcept
    {
        return thread_priority_t (value_);
    }

    int value () const noexcept { return _value; }

  private:
    explicit thread_priority_t (int value_) noexcept : _value (value_) {}

    int _value;
};

class cpu_index_t
{
  public:
    static cpu_index_t value (int value_) noexcept { return cpu_index_t (value_); }

    int value () const noexcept { return _value; }

  private:
    explicit cpu_index_t (int value_) noexcept : _value (value_) {}

    int _value;
};

class byte_size_t
{
  public:
    static byte_size_t bytes (int64_t value_) noexcept
    {
        return byte_size_t (value_);
    }

    int64_t bytes () const noexcept { return _bytes; }

  private:
    explicit byte_size_t (int64_t value_) noexcept : _bytes (value_) {}

    int64_t _bytes;
};

class peer_weight_t
{
  public:
    static peer_weight_t value (uint32_t value_)
    {
        if (value_ > 100u)
            throw std::invalid_argument ("peer_weight_t must be in range 0..100");
        return peer_weight_t (value_);
    }

    uint32_t value () const noexcept { return _value; }

  private:
    explicit peer_weight_t (uint32_t value_) noexcept : _value (value_) {}

    uint32_t _value;
};

class message_count_t
{
  public:
    static message_count_t value (int value_) noexcept
    {
        return message_count_t (value_);
    }

    int value () const noexcept { return _value; }

  private:
    explicit message_count_t (int value_) noexcept : _value (value_) {}

    int _value;
};

class socket_backlog_t
{
  public:
    static socket_backlog_t value (int value_) noexcept
    {
        return socket_backlog_t (value_);
    }

    int value () const noexcept { return _value; }

  private:
    explicit socket_backlog_t (int value_) noexcept : _value (value_) {}

    int _value;
};

enum class tcp_keepalive_mode_t : int
{
    os_default = -1,
    off = 0,
    on = 1
};

namespace compat
{
namespace options
{

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
    rid_duplicate_policy = ZLINK_OPT_RID_DUPLICATE_POLICY,
    route_value_max_size = ZLINK_OPT_ROUTE_VALUE_MAX_SIZE,
    auto_hwm_msg_unit_bytes = ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES
};

enum class router_option : int
{
    mandatory = ZLINK_ROUTER_OPT_MANDATORY,
    probe = ZLINK_ROUTER_OPT_PROBE,
    connect_routing_id = ZLINK_ROUTER_OPT_CONNECT_ROUTING_ID,
    request_timeout_ms = ZLINK_ROUTER_OPT_REQUEST_TIMEOUT_MS,
    weight = ZLINK_ROUTER_OPT_WEIGHT
};

enum class dealer_option : int
{
    probe = ZLINK_DEALER_OPT_PROBE,
    request_timeout_ms = ZLINK_DEALER_OPT_REQUEST_TIMEOUT_MS,
    weight = ZLINK_DEALER_OPT_WEIGHT
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

} // namespace options
} // namespace compat

namespace detail
{
inline routing_id_t native_routing_id (const zlink_routing_id_t &native_);
inline routing_id_t borrowed_routing_id (const zlink_routing_id_t &native_) noexcept;
inline routing_id_t unchecked_empty_routing_id () noexcept;
inline void assign_routing_id_native (routing_id_t &routing_id_,
                                      const zlink_routing_id_t &native_) noexcept;
inline zlink_routing_id_t *routing_id_native (routing_id_t &routing_id_) noexcept;
inline const zlink_routing_id_t *
routing_id_native (const routing_id_t &routing_id_) noexcept;
inline bool routing_id_empty (const routing_id_t &routing_id_) noexcept;
inline const zlink_actor_ref_t *
actor_ref_native (const actor_ref_t &actor_) noexcept;

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

} // namespace detail

namespace compat
{
namespace options
{

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
static const socket_option_key_t<int> rid_duplicate_policy (
  socket_option::rid_duplicate_policy);
} // namespace socket_options

namespace router_options
{
static const router_option_key_t<int> mandatory (router_option::mandatory);
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

} // namespace options
} // namespace compat

class common_socket_options_t
{
  public:
    explicit common_socket_options_t (void *handle_ = NULL) noexcept
        : _handle (handle_)
    {
    }

    std::chrono::milliseconds linger () const;
    void linger (std::chrono::milliseconds value);
    message_count_t send_hwm () const;
    void send_hwm (message_count_t value);
    message_count_t recv_hwm () const;
    void recv_hwm (message_count_t value);
    std::chrono::milliseconds send_timeout () const;
    void send_timeout (std::chrono::milliseconds value);
    std::chrono::milliseconds recv_timeout () const;
    void recv_timeout (std::chrono::milliseconds value);
    bool immediate () const;
    void immediate (bool value);
    std::chrono::milliseconds connect_timeout () const;
    void connect_timeout (std::chrono::milliseconds value);
    bool ipv6 () const;
    void ipv6 (bool value);
    bool tcp_no_delay () const;
    void tcp_no_delay (bool value);
    tcp_keepalive_mode_t tcp_keepalive () const;
    void tcp_keepalive (tcp_keepalive_mode_t value);
    std::chrono::milliseconds heartbeat_interval () const;
    void heartbeat_interval (std::chrono::milliseconds value);
    std::chrono::milliseconds heartbeat_ttl () const;
    void heartbeat_ttl (std::chrono::milliseconds value);
    std::chrono::milliseconds heartbeat_timeout () const;
    void heartbeat_timeout (std::chrono::milliseconds value);
    rid_duplicate_policy_t rid_duplicate_policy () const;
    void rid_duplicate_policy (rid_duplicate_policy_t value);
    byte_size_t max_message_size () const;
    void max_message_size (byte_size_t value);
    byte_size_t auto_hwm_msg_unit_bytes () const;
    void auto_hwm_msg_unit_bytes (byte_size_t value);
    socket_backlog_t backlog () const;
    void backlog (socket_backlog_t value);
    std::chrono::milliseconds reconnect_interval () const;
    void reconnect_interval (std::chrono::milliseconds value);
    std::chrono::milliseconds reconnect_interval_max () const;
    void reconnect_interval_max (std::chrono::milliseconds value);
    std::string last_endpoint () const;

  protected:
    void *_handle;
};

class router_socket_options_t : public common_socket_options_t
{
  public:
    explicit router_socket_options_t (void *handle_ = NULL) noexcept
        : common_socket_options_t (handle_)
    {
    }

    bool mandatory () const;
    void mandatory (bool value);
    bool handover () const;
    void handover (bool value);
    bool probe () const;
    void probe (bool value);
    std::optional<routing_id_t> connect_routing_id () const;
    void connect_routing_id (const routing_id_t &value);
    std::chrono::milliseconds request_timeout () const;
    void request_timeout (std::chrono::milliseconds value);
    peer_weight_t peer_weight () const;
    void peer_weight (peer_weight_t value);
};

class dealer_socket_options_t : public common_socket_options_t
{
  public:
    explicit dealer_socket_options_t (void *handle_ = NULL) noexcept
        : common_socket_options_t (handle_)
    {
    }

    bool probe () const;
    void probe (bool value);
    std::chrono::milliseconds request_timeout () const;
    void request_timeout (std::chrono::milliseconds value);
    peer_weight_t peer_weight () const;
    void peer_weight (peer_weight_t value);
};

class stream_socket_options_t : public common_socket_options_t
{
  public:
    explicit stream_socket_options_t (void *handle_ = NULL) noexcept
        : common_socket_options_t (handle_)
    {
    }

    bool notify () const;
    void notify (bool value);
};

class pub_socket_options_t : public common_socket_options_t
{
  public:
    explicit pub_socket_options_t (void *handle_ = NULL) noexcept
        : common_socket_options_t (handle_)
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
    bool manual_last_value () const;
    void manual_last_value (bool value);
    message_t welcome_message () const;
    void welcome_message (const message_t &message);
    void approve_subscribe (const routing_id_t &routing_id);
    void reject_subscribe (const routing_id_t &routing_id);
    int topics_count () const;
};

class sub_socket_options_t : public common_socket_options_t
{
  public:
    explicit sub_socket_options_t (void *handle_ = NULL) noexcept
        : common_socket_options_t (handle_)
    {
    }

    int topics_count () const;
};

class send_flags_t
{
  public:
    constexpr send_flags_t (int value_ = ZLINK_SEND_FLAGS_NONE) noexcept
        : _value (value_)
    {
    }

    constexpr operator int () const noexcept { return _value; }
    constexpr operator zlink_send_flags_t () const noexcept
    {
        return static_cast<zlink_send_flags_t> (_value);
    }

    static const send_flags_t none;
    static const send_flags_t dontwait;

  private:
    int _value;
};

inline constexpr bool operator== (send_flags_t a_, send_flags_t b_) noexcept
{
    return static_cast<int> (a_) == static_cast<int> (b_);
}

inline constexpr bool operator!= (send_flags_t a_, send_flags_t b_) noexcept
{
    return !(a_ == b_);
}

class recv_flags_t
{
  public:
    constexpr recv_flags_t (int value_ = ZLINK_RECV_FLAGS_NONE) noexcept
        : _value (value_)
    {
    }

    constexpr operator int () const noexcept { return _value; }
    constexpr operator zlink_recv_flags_t () const noexcept
    {
        return static_cast<zlink_recv_flags_t> (_value);
    }
    constexpr operator zlink_send_flags_t () const noexcept
    {
        return static_cast<zlink_send_flags_t> (_value);
    }

    static const recv_flags_t none;
    static const recv_flags_t dontwait;

  private:
    int _value;
};

inline constexpr bool operator== (recv_flags_t a_, recv_flags_t b_) noexcept
{
    return static_cast<int> (a_) == static_cast<int> (b_);
}

inline constexpr bool operator!= (recv_flags_t a_, recv_flags_t b_) noexcept
{
    return !(a_ == b_);
}

inline const send_flags_t send_flags_t::none {ZLINK_SEND_FLAGS_NONE};
inline const send_flags_t send_flags_t::dontwait {ZLINK_SEND_FLAGS_DONTWAIT};
inline const recv_flags_t recv_flags_t::none {ZLINK_RECV_FLAGS_NONE};
inline const recv_flags_t recv_flags_t::dontwait {ZLINK_RECV_FLAGS_DONTWAIT};

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
    internal_error = ZLINK_SUBMIT_INTERNAL_ERROR,
    not_admitted = ZLINK_SUBMIT_NOT_ADMITTED
};

enum class request_result_t : int
{
    ok = ZLINK_REQUEST_OK,
    timed_out = ZLINK_REQUEST_TIMED_OUT,
    not_found = ZLINK_REQUEST_NOT_FOUND,
    terminated = ZLINK_REQUEST_TERMINATED,
    protocol_error = ZLINK_REQUEST_PROTOCOL_ERROR,
    internal_error = ZLINK_REQUEST_INTERNAL_ERROR,
    rejected = ZLINK_REQUEST_REJECTED,
    conflict = ZLINK_REQUEST_CONFLICT,
    busy = ZLINK_REQUEST_BUSY,
    not_connected = ZLINK_REQUEST_NOT_CONNECTED,
    invalid_argument = ZLINK_REQUEST_INVALID_ARGUMENT,
    invalid_state = ZLINK_REQUEST_INVALID_STATE,
    not_supported = ZLINK_REQUEST_NOT_SUPPORTED
};

using request_callback_t =
  std::function<void(request_result_t, std::vector<message_t>)>;

enum class recv_result_t : int
{
    ok = ZLINK_RECV_OK,
    no_data = ZLINK_RECV_NO_DATA,
    busy = ZLINK_RECV_BUSY,
    terminated = ZLINK_RECV_TERMINATED,
    invalid_handle = ZLINK_RECV_INVALID_HANDLE,
    not_supported = ZLINK_RECV_NOT_SUPPORTED,
    internal_error = ZLINK_RECV_INTERNAL_ERROR
};

enum class handler_result_t : int
{
    ok = ZLINK_HANDLER_OK,
    invalid_argument = ZLINK_HANDLER_INVALID_ARGUMENT,
    busy = ZLINK_HANDLER_BUSY,
    not_supported = ZLINK_HANDLER_NOT_SUPPORTED,
    deadlock = ZLINK_HANDLER_DEADLOCK,
    invalid_handle = ZLINK_HANDLER_INVALID_HANDLE,
    internal_error = ZLINK_HANDLER_INTERNAL_ERROR
};

enum class close_result_t : int
{
    ok = ZLINK_CLOSE_OK,
    busy = ZLINK_CLOSE_BUSY,
    shutdown = ZLINK_CLOSE_SHUTDOWN,
    invalid_handle = ZLINK_CLOSE_INVALID_HANDLE,
    internal_error = ZLINK_CLOSE_INTERNAL_ERROR
};

enum class bind_result_t : int
{
    ok = ZLINK_BIND_OK,
    invalid_argument = ZLINK_BIND_INVALID_ARGUMENT,
    addr_in_use = ZLINK_BIND_ADDR_IN_USE,
    not_supported = ZLINK_BIND_NOT_SUPPORTED,
    invalid_handle = ZLINK_BIND_INVALID_HANDLE,
    internal_error = ZLINK_BIND_INTERNAL_ERROR
};

enum class connect_result_t : int
{
    ok = ZLINK_CONNECT_OK,
    invalid_argument = ZLINK_CONNECT_INVALID_ARGUMENT,
    not_supported = ZLINK_CONNECT_NOT_SUPPORTED,
    invalid_handle = ZLINK_CONNECT_INVALID_HANDLE,
    internal_error = ZLINK_CONNECT_INTERNAL_ERROR,
    not_found = ZLINK_CONNECT_NOT_FOUND,
    conflict = ZLINK_CONNECT_CONFLICT,
    busy = ZLINK_CONNECT_BUSY
};

enum class config_result_t : int
{
    ok = ZLINK_CONFIG_OK,
    invalid_handle = ZLINK_CONFIG_INVALID_HANDLE,
    invalid_argument = ZLINK_CONFIG_INVALID_ARGUMENT,
    not_supported = ZLINK_CONFIG_NOT_SUPPORTED,
    internal_error = ZLINK_CONFIG_INTERNAL_ERROR,
    invalid_state = ZLINK_CONFIG_INVALID_STATE,
    not_found = ZLINK_CONFIG_NOT_FOUND
};

class routing_id_t
{
  public:
    routing_id_t (const uint8_t *bytes_, size_t size_) : _native (), _view (NULL)
    {
        std::memset (&_native, 0, sizeof (_native));
        assign (bytes_, size_);
    }

    routing_id_t (const routing_id_t &other_) : _native (), _view (NULL)
    {
        assign_native (other_.native_ref ());
    }

    routing_id_t &operator= (const routing_id_t &other_)
    {
        if (this == &other_)
            return *this;
        assign_native (other_.native_ref ());
        _view = NULL;
        return *this;
    }

    routing_id_t (routing_id_t &&other_) noexcept
        : _native (), _view (NULL)
    {
        assign_native (other_.native_ref ());
    }

    routing_id_t &operator= (routing_id_t &&other_) noexcept
    {
        if (this == &other_)
            return *this;
        assign_native (other_.native_ref ());
        _view = NULL;
        return *this;
    }

    static routing_id_t from_bytes (const uint8_t *bytes_, size_t size_)
    {
        return routing_id_t (bytes_, size_);
    }

    static routing_id_t from_bytes (const std::vector<uint8_t> &bytes_)
    {
        return routing_id_t (
          bytes_.empty () ? NULL : bytes_.data (), bytes_.size ());
    }

    static routing_id_t from_string (const std::string &value_)
    {
        if (value_.empty () || (value_.size () % 2u) != 0u)
            throw std::invalid_argument ("routing id string must be hex");
        if (value_.size () > 510u)
            throw std::invalid_argument (
              "routing id string must decode to at most 255 bytes");

        std::vector<uint8_t> bytes;
        bytes.reserve (value_.size () / 2u);
        for (size_t i = 0; i < value_.size (); i += 2u) {
            const int high = hex_nibble (value_[i]);
            const int low = hex_nibble (value_[i + 1u]);
            if (high < 0 || low < 0)
                throw std::invalid_argument ("routing id string must be hex");
            bytes.push_back (static_cast<uint8_t> ((high << 4) | low));
        }
        return from_bytes (bytes);
    }

    const uint8_t *data () const noexcept { return native_ref ().data; }
    size_t size () const noexcept { return native_ref ().size; }

    std::vector<uint8_t> to_bytes () const
    {
        return std::vector<uint8_t> (
          native_ref ().data,
          native_ref ().data + static_cast<size_t> (native_ref ().size));
    }

    std::string to_string () const
    {
        return to_hex ();
    }

    std::string to_hex () const
    {
        static const char *digits = "0123456789abcdef";
        std::string hex;
        hex.resize (size () * 2u);
        for (size_t i = 0; i < size (); ++i) {
            const uint8_t byte = native_ref ().data[i];
            hex[(i * 2u)] = digits[(byte >> 4u) & 0x0fu];
            hex[(i * 2u) + 1u] = digits[byte & 0x0fu];
        }
        return hex;
    }

    friend bool operator== (const routing_id_t &a_,
                            const routing_id_t &b_) noexcept
    {
        const zlink_routing_id_t &a = a_.native_ref ();
        const zlink_routing_id_t &b = b_.native_ref ();
        return a.size == b.size
               && std::memcmp (
                    a.data, b.data, a.size)
                    == 0;
    }

    friend bool operator!= (const routing_id_t &a_,
                            const routing_id_t &b_) noexcept
    {
        return !(a_ == b_);
    }

  private:
    routing_id_t () noexcept : _native (), _view (NULL)
    {
        std::memset (&_native, 0, sizeof (_native));
    }

    explicit routing_id_t (const zlink_routing_id_t &native_) : _native (), _view (NULL)
    {
        assign_native (native_);
    }

    struct borrowed_t
    {
    };

    routing_id_t (borrowed_t, const zlink_routing_id_t &native_) noexcept
        : _native (), _view (&native_)
    {
        std::memset (&_native, 0, sizeof (_native));
    }

    void assign (const uint8_t *bytes_, size_t size_)
    {
        if (size_ == 0)
            throw std::invalid_argument ("routing id must not be empty");
        if (size_ > sizeof (_native.data))
            throw std::invalid_argument ("routing id exceeds 255 bytes");
        if (size_ > 0 && !bytes_)
            throw std::invalid_argument (
              "routing id bytes must not be null for non-empty input");

        std::memset (&_native, 0, sizeof (_native));
        _view = NULL;
        _native.size = static_cast<uint8_t> (size_);
        if (size_ > 0)
            std::memcpy (_native.data, bytes_, size_);
    }

    void assign_native (const zlink_routing_id_t &native_) noexcept
    {
        // Single struct assignment fits in two SSE moves and beats
        // memset+partial-memcpy for the typical short-rid case (zlink core
        // already zeros the unused tail when populating the source rid, so
        // we inherit that invariant). The previous memset-then-partial-copy
        // form actually wrote more bytes per call than a flat struct copy.
        _native = native_;
        _view = NULL;
    }

    const zlink_routing_id_t &native_ref () const noexcept
    {
        return _view ? *_view : _native;
    }

    zlink_routing_id_t *mutable_native () noexcept
    {
        if (_view) {
            _native = *_view;
            _view = NULL;
        }
        return &_native;
    }

    zlink_routing_id_t _native;
    const zlink_routing_id_t *_view;

    static int hex_nibble (char value_) noexcept
    {
        if (value_ >= '0' && value_ <= '9')
            return value_ - '0';
        if (value_ >= 'a' && value_ <= 'f')
            return value_ - 'a' + 10;
        if (value_ >= 'A' && value_ <= 'F')
            return value_ - 'A' + 10;
        return -1;
    }

    friend inline routing_id_t
    detail::native_routing_id (const zlink_routing_id_t &native_);
    friend inline routing_id_t
    detail::borrowed_routing_id (const zlink_routing_id_t &native_) noexcept;
    friend inline routing_id_t detail::unchecked_empty_routing_id () noexcept;
    friend inline void
    detail::assign_routing_id_native (routing_id_t &,
                                      const zlink_routing_id_t &) noexcept;
    friend inline zlink_routing_id_t *
    detail::routing_id_native (routing_id_t &) noexcept;
    friend inline const zlink_routing_id_t *
    detail::routing_id_native (const routing_id_t &) noexcept;
    friend inline bool
    detail::routing_id_empty (const routing_id_t &) noexcept;
};

namespace detail
{

inline routing_id_t native_routing_id (const zlink_routing_id_t &native_)
{
    return routing_id_t (native_);
}

inline routing_id_t borrowed_routing_id (const zlink_routing_id_t &native_) noexcept
{
    return routing_id_t (routing_id_t::borrowed_t (), native_);
}

inline routing_id_t unchecked_empty_routing_id () noexcept
{
    return routing_id_t ();
}

inline void assign_routing_id_native (routing_id_t &routing_id_,
                                      const zlink_routing_id_t &native_) noexcept
{
    routing_id_.assign_native (native_);
}

inline zlink_routing_id_t empty_routing_id () noexcept
{
    zlink_routing_id_t routing_id;
    std::memset (&routing_id, 0, sizeof (routing_id));
    return routing_id;
}

inline zlink_routing_id_t *routing_id_native (routing_id_t &routing_id_) noexcept
{
    return routing_id_.mutable_native ();
}

inline const zlink_routing_id_t *
routing_id_native (const routing_id_t &routing_id_) noexcept
{
    return &routing_id_.native_ref ();
}

inline bool routing_id_empty (const routing_id_t &routing_id_) noexcept
{
    return routing_id_.native_ref ().size == 0;
}

} // namespace detail

template<size_t N> inline std::string fixed_string_to_string (const char (&src_)[N]);

enum class actor_create_status_t : int
{
    created = ZLINK_ACTOR_CREATE_CREATED,
    existing = ZLINK_ACTOR_CREATE_EXISTING
};

enum class actor_admission_result_t : int
{
    accept = ZLINK_ACTOR_ADMISSION_ACCEPT,
    reject = ZLINK_ACTOR_ADMISSION_REJECT
};

class actor_ref_t
{
  public:
    actor_ref_t () noexcept : _native ()
    {
        std::memset (&_native, 0, sizeof (_native));
    }

    explicit actor_ref_t (const zlink_actor_ref_t &native_) : _native (native_) {}

    routing_id_t node_rid () const
    {
        return detail::native_routing_id (_native.node_rid);
    }

    std::string actor_id () const
    {
        return fixed_string_to_string (_native.actor_id);
    }

    std::string actor_id_string () const
    {
        return actor_id ();
    }

    uint64_t generation () const noexcept { return _native.generation; }

    bool unchecked () const noexcept { return _native.generation == 0u; }

  private:
    const zlink_actor_ref_t &native () const noexcept { return _native; }

    zlink_actor_ref_t _native;

    friend inline const zlink_actor_ref_t *
    detail::actor_ref_native (const actor_ref_t &actor_) noexcept;
};

namespace detail
{

inline const zlink_actor_ref_t *
actor_ref_native (const actor_ref_t &actor_) noexcept
{
    return &actor_.native ();
}

} // namespace detail

struct actor_recv_info_t
{
    actor_recv_info_t ()
        : actor (),
          source_node_rid (detail::unchecked_empty_routing_id ()),
          source_session_rid (detail::unchecked_empty_routing_id ()),
          flags (0)
    {
    }

    explicit actor_recv_info_t (const zlink_actor_recv_info_t &native_)
        : actor (native_.actor),
          source_node_rid (detail::native_routing_id (native_.source_node_rid)),
          source_session_rid (detail::native_routing_id (native_.source_session_rid)),
          flags (native_.flags)
    {
    }

    actor_ref_t actor;
    routing_id_t source_node_rid;
    routing_id_t source_session_rid;
    uint32_t flags;
};

struct actor_join_info_t
{
    actor_join_info_t ()
        : source_actor (),
          target_actor (),
          source_node_rid (detail::unchecked_empty_routing_id ()),
          source_spot_rid (detail::unchecked_empty_routing_id ()),
          target_node_rid (detail::unchecked_empty_routing_id ()),
          target_spot_rid (detail::unchecked_empty_routing_id ()),
          join_epoch (0),
          flags (0),
          _request (NULL)
    {
    }

    explicit actor_join_info_t (const zlink_actor_join_info_t &native_)
        : source_actor (native_.source_actor),
          target_actor (native_.target_actor),
          source_node_rid (detail::native_routing_id (native_.source_node_rid)),
          source_spot_rid (detail::native_routing_id (native_.source_spot_rid)),
          target_node_rid (detail::native_routing_id (native_.target_node_rid)),
          target_spot_rid (detail::native_routing_id (native_.target_spot_rid)),
          join_epoch (native_.join_epoch),
          flags (native_.flags),
          _request (native_.request)
    {
    }

    actor_ref_t source_actor;
    actor_ref_t target_actor;
    routing_id_t source_node_rid;
    routing_id_t source_spot_rid;
    routing_id_t target_node_rid;
    routing_id_t target_spot_rid;
    uint64_t join_epoch;
    uint32_t flags;

  private:
    zlink_actor_join_info_t native () const
    {
        zlink_actor_join_info_t out;
        std::memset (&out, 0, sizeof (out));
        out.source_actor = *detail::actor_ref_native (source_actor);
        out.target_actor = *detail::actor_ref_native (target_actor);
        out.source_node_rid = *detail::routing_id_native (source_node_rid);
        out.source_spot_rid = *detail::routing_id_native (source_spot_rid);
        out.target_node_rid = *detail::routing_id_native (target_node_rid);
        out.target_spot_rid = *detail::routing_id_native (target_spot_rid);
        out.join_epoch = join_epoch;
        out.request = _request;
        out.flags = flags;
        return out;
    }

    void *_request;
    friend class service::spot_t;
};

class actor_join_request_t
{
  public:
    actor_join_request_t (actor_join_info_t info_, message_t message_)
        : _info (std::move (info_)), _message (std::move (message_))
    {
    }

    const actor_join_info_t &info () const noexcept { return _info; }
    message_t &message () noexcept { return _message; }
    const message_t &message () const noexcept { return _message; }

  private:
    actor_join_info_t _info;
    message_t _message;
};

struct actor_create_result_t
{
    actor_create_result_t () : status (actor_create_status_t::created), actor () {}

    explicit actor_create_result_t (const zlink_actor_create_result_t &native_)
        : status (static_cast<actor_create_status_t> (native_.status)),
          actor (native_.actor)
    {
    }

    actor_create_status_t status;
    actor_ref_t actor;
};

struct actor_route_t
{
    actor_route_t () : actor (), joined (false), joined_spot_rid (std::nullopt) {}

    explicit actor_route_t (const zlink_actor_route_t &native_)
        : actor (native_.actor),
          joined (native_.joined != 0),
          joined_spot_rid (
            native_.joined_spot_rid.size > 0
              ? std::optional<routing_id_t> (
                  detail::native_routing_id (native_.joined_spot_rid))
              : std::nullopt)
    {
    }

    actor_ref_t actor;
    bool joined;
    std::optional<routing_id_t> joined_spot_rid;
};

struct actor_part_t
{
    actor_part_t () : info (), part (), has_more (false) {}

    actor_part_t (actor_recv_info_t info_, message_t part_, bool has_more_)
        : info (std::move (info_)),
          part (std::move (part_)),
          has_more (has_more_)
    {
    }

    actor_recv_info_t info;
    message_t part;
    bool has_more;
};

enum class spot_dispatch_event_t : int
{
    subscribe_readable = ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE,
    routed_readable = ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE,
    timer_readable = ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE,
    channel_reply_readable = ZLINK_SPOT_DISPATCH_EVENT_CHANNEL_REPLY_READABLE,
    actor_readable = ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE,
    actor_join_readable = ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE
};

enum class spot_dispatch_subject_kind_t : int
{
    spot = ZLINK_SPOT_DISPATCH_SUBJECT_SPOT,
    timer = ZLINK_SPOT_DISPATCH_SUBJECT_TIMER,
    channel_dealer = ZLINK_SPOT_DISPATCH_SUBJECT_CHANNEL_DEALER,
    actor = ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR
};

struct spot_dispatch_info_t
{
    spot_dispatch_info_t ()
        : event (spot_dispatch_event_t::subscribe_readable),
          subject_kind (spot_dispatch_subject_kind_t::spot),
          timer (NULL),
          channel_dealer (NULL),
          channel_name (std::nullopt),
          actor (std::nullopt),
          actor_parts ()
    {
    }

    explicit spot_dispatch_info_t (const zlink_spot_dispatch_info_t &native_)
        : event (static_cast<spot_dispatch_event_t> (native_.event)),
          subject_kind (
            static_cast<spot_dispatch_subject_kind_t> (native_.subject_kind)),
          timer (NULL),
          channel_dealer (NULL),
          channel_name (std::nullopt),
          actor (std::nullopt),
          actor_parts ()
    {
        if (subject_kind == spot_dispatch_subject_kind_t::actor
            && native_.subject) {
            actor =
              actor_ref_t (*static_cast<const zlink_actor_ref_t *> (
                native_.subject));
        }
    }

    spot_dispatch_event_t event;
    spot_dispatch_subject_kind_t subject_kind;
    timer_t *timer;
    dealer_socket_t *channel_dealer;
    std::optional<std::string> channel_name;
    std::optional<actor_ref_t> actor;
    std::vector<actor_part_t> actor_parts;
};

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
          _single_part (),
          _parts (std::move (parts_)),
          _reply_fn (std::move (reply_fn_))
    {
    }

    received_t (std::optional<routing_id_t> routing_id_,
                std::optional<routing_id_t> spot_rid_,
                std::optional<uint64_t> request_seq_,
                message_t part_,
                std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn_ =
                  std::function<void(std::vector<message_t> &, send_flags_t)> ())
        : _routing_id (std::move (routing_id_)),
          _spot_rid (std::move (spot_rid_)),
          _request_seq (std::move (request_seq_)),
          _single_part (std::move (part_)),
          _parts (),
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

    const std::vector<message_t> &parts () const;
    std::vector<message_t> &parts ();

    bool is_single_part () const noexcept
    {
        return _single_part.has_value () || _parts.size () == 1u;
    }
    message_t &first_part ();
    message_t single_part_or_throw ();
    void reply (message_t &part) const;
    void reply (message_t &part, send_flags_t flags) const;
    void reply (std::vector<message_t> &parts) const;
    void reply (std::vector<message_t> &parts, send_flags_t flags) const;
    void close ();

    void set_reply_fn (
      std::function<void(std::vector<message_t> &, send_flags_t)> reply_fn_)
    {
        _reply_fn = std::move (reply_fn_);
    }

  private:
    void materialize_parts () const;

    std::optional<routing_id_t> _routing_id;
    std::optional<routing_id_t> _spot_rid;
    std::optional<uint64_t> _request_seq;
    mutable std::optional<message_t> _single_part;
    mutable std::vector<message_t> _parts;
    std::function<void(std::vector<message_t> &, send_flags_t)> _reply_fn;
};

class topic_message_t
{
  public:
    topic_message_t () = default;

    topic_message_t (std::optional<routing_id_t> routing_id_,
                     std::string topic_,
                     std::vector<message_t> parts_)
        : _routing_id (std::move (routing_id_)),
          _topic (std::move (topic_)),
          _parts (std::move (parts_))
    {
    }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return _routing_id;
    }

    const std::string &topic () const noexcept { return _topic; }
    const std::vector<message_t> &parts () const;
    std::vector<message_t> &parts ();

    bool is_single_part () const noexcept
    {
        return _single_part.has_value () || _parts.size () == 1u;
    }
    message_t &first_part ();
    message_t single_part_or_throw ();
    void close ();

  private:
    topic_message_t (std::optional<routing_id_t> routing_id_,
                     std::string topic_,
                     message_t part_)
        : _routing_id (std::move (routing_id_)),
          _topic (std::move (topic_)),
          _single_part (std::move (part_)),
          _parts ()
    {
    }

    void materialize_parts () const;

    std::optional<routing_id_t> _routing_id;
    std::string _topic;
    mutable std::optional<message_t> _single_part;
    mutable std::vector<message_t> _parts;
    friend class service::spot_t;
};

struct subscription_event_t
{
    subscription_event_t ()
        : routing_id (std::nullopt), topic (), subscribed (false)
    {
    }

    std::optional<routing_id_t> routing_id;
    std::string topic;
    bool subscribed;
};

struct subscription_filter_t
{
    std::string filter;
    bool is_pattern = false;
};

using subscription_entry_t = subscription_filter_t;

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
    peer_weight_changed = ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED,
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

enum class poll_event_flag_t : short
{
    none = 0,
    pollin = ZLINK_POLLIN,
    pollout = ZLINK_POLLOUT,
    pollerr = ZLINK_POLLERR
};

inline poll_event_flag_t operator| (poll_event_flag_t a, poll_event_flag_t b)
{
    return static_cast<poll_event_flag_t> (static_cast<short> (a)
                                           | static_cast<short> (b));
}

enum class poll_source_kind_t : int
{
    socket = ZLINK_POLLER_SOURCE_SOCKET,
    fd = ZLINK_POLLER_SOURCE_FD,
    timer = ZLINK_POLLER_SOURCE_TIMER
};

enum class auto_connect_type : int
{
    invalid = ZLINK_AUTO_CONNECT_INVALID,
    route_mesh = ZLINK_AUTO_CONNECT_ROUTE_MESH,
    client_server = ZLINK_AUTO_CONNECT_CLIENT_SERVER,
    dealer_mesh = ZLINK_AUTO_CONNECT_DEALER_MESH,
    fanout = ZLINK_AUTO_CONNECT_FANOUT,
    spot_mesh = ZLINK_AUTO_CONNECT_SPOT_MESH
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
                            detail::native_routing_id (native_.routing_id))
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
          detail_flags (0), snd_pending_msgs (0), rcv_pending_msgs (0),
          auto_hwm_enabled (false), auto_hwm_profile (0),
          auto_hwm_role (0), auto_hwm_policy_class (0),
          auto_hwm_unit_budget_bytes (0), auto_hwm_size_cap (0),
          auto_hwm_socket_message_slots (0),
          auto_hwm_effective_message_bytes (0),
          auto_hwm_applied_sndhwm (0), auto_hwm_applied_rcvhwm (0),
          auto_hwm_effective_sndbuf (0), auto_hwm_effective_rcvbuf (0),
          auto_hwm_last_recalc_ms (0),
          auto_hwm_last_recalc_reason (0),
          auto_hwm_send_blocked_ratio_ppm (0),
          auto_hwm_deferred_sndhwm (-1), auto_hwm_deferred_rcvhwm (-1)
    {
    }

    explicit monitor_snapshot_t (const zlink_monitor_snapshot_t &native_)
        : source_kind (
            static_cast<monitor_source_kind> (native_.source_kind)),
          state_flags (native_.state_flags),
          detail_flags (native_.detail_flags),
          snd_pending_msgs (native_.snd_pending_msgs),
          rcv_pending_msgs (native_.rcv_pending_msgs),
          auto_hwm_enabled (native_.auto_hwm_enabled != 0),
          auto_hwm_profile (native_.auto_hwm_profile),
          auto_hwm_role (native_.auto_hwm_role),
          auto_hwm_policy_class (native_.auto_hwm_policy_class),
          auto_hwm_unit_budget_bytes (native_.auto_hwm_unit_budget_bytes),
          auto_hwm_size_cap (native_.auto_hwm_size_cap),
          auto_hwm_socket_message_slots (
            native_.auto_hwm_socket_message_slots),
          auto_hwm_effective_message_bytes (
            native_.auto_hwm_effective_message_bytes),
          auto_hwm_applied_sndhwm (native_.auto_hwm_applied_sndhwm),
          auto_hwm_applied_rcvhwm (native_.auto_hwm_applied_rcvhwm),
          auto_hwm_effective_sndbuf (native_.auto_hwm_effective_sndbuf),
          auto_hwm_effective_rcvbuf (native_.auto_hwm_effective_rcvbuf),
          auto_hwm_last_recalc_ms (native_.auto_hwm_last_recalc_ms),
          auto_hwm_last_recalc_reason (native_.auto_hwm_last_recalc_reason),
          auto_hwm_send_blocked_ratio_ppm (
            native_.auto_hwm_send_blocked_ratio_ppm),
          auto_hwm_deferred_sndhwm (native_.auto_hwm_deferred_sndhwm),
          auto_hwm_deferred_rcvhwm (native_.auto_hwm_deferred_rcvhwm)
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
    bool auto_hwm_enabled;
    uint32_t auto_hwm_profile;
    uint32_t auto_hwm_role;
    uint32_t auto_hwm_policy_class;
    uint64_t auto_hwm_unit_budget_bytes;
    uint32_t auto_hwm_size_cap;
    uint64_t auto_hwm_socket_message_slots;
    uint64_t auto_hwm_effective_message_bytes;
    int32_t auto_hwm_applied_sndhwm;
    int32_t auto_hwm_applied_rcvhwm;
    int32_t auto_hwm_effective_sndbuf;
    int32_t auto_hwm_effective_rcvbuf;
    uint64_t auto_hwm_last_recalc_ms;
    uint32_t auto_hwm_last_recalc_reason;
    uint32_t auto_hwm_send_blocked_ratio_ppm;
    int32_t auto_hwm_deferred_sndhwm;
    int32_t auto_hwm_deferred_rcvhwm;
};

using monitor_event_handler_fn = void (*) (const monitor_event_t *event_,
                                           void *userdata_);

enum class monitor_target_kind_t : int
{
    socket = 0,
    discovery = 1,
    spot = 2,
    spot_node = 3
};

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

enum class spot_node_socket_type_t : int
{
    pair = ZLINK_SOCKET_PAIR,
    dealer = ZLINK_SOCKET_DEALER,
    router = ZLINK_SOCKET_ROUTER,
    stream = ZLINK_SOCKET_STREAM,
    pub = ZLINK_SOCKET_PUB,
    xpub = ZLINK_SOCKET_XPUB,
    sub = ZLINK_SOCKET_SUB,
    xsub = ZLINK_SOCKET_XSUB
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

enum class spot_node_mode : int
{
    pubsub = ZLINK_SPOT_NODE_MODE_PUBSUB,
    routed = ZLINK_SPOT_NODE_MODE_ROUTED,
    all = ZLINK_SPOT_NODE_MODE_ALL
};

enum class spot_node_socket_owner : int
{
    any = ZLINK_SPOT_NODE_SOCKET_OWNER_ANY,
    node = ZLINK_SPOT_NODE_SOCKET_OWNER_NODE,
    spot = ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT
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

using monitor_event_type_t = monitor_event;
using monitor_source_kind_t = monitor_source_kind;
using registry_socket_role_t = registry_socket_role;
using discovery_socket_role_t = discovery_socket_role;
using spot_node_socket_role_t = spot_node_socket_role;
using auto_connect_type_t = auto_connect_type;
using service_role_t = service_role;
using service_kind_t = service_kind;
using spot_role_t = spot_socket_role;
using spot_node_state_t = spot_node_state;
using spot_node_mode_t = spot_node_mode;
using spot_node_socket_owner_t = spot_node_socket_owner;
using spot_peer_source_t = spot_peer_source;
using spot_peer_state_t = spot_peer_state;
using registry_state_t = registry_state;
using topology_source_t = topology_source;
using topology_state_t = topology_state;
using subject_kind_t = subject_kind;

class member_peer_entry_t
{
  public:
    member_peer_entry_t ()
        : auto_connect_type_ (zlink::auto_connect_type::invalid),
          service_role_ (service_role::invalid), channel_name_ (), endpoint_ (),
          routing_id_ (std::nullopt), weight_ (0),
          value_ (0)
    {
    }

    explicit member_peer_entry_t (const zlink_member_peer_entry_t &entry_)
        : auto_connect_type_ (
            static_cast<zlink::auto_connect_type> (entry_.auto_connect_type)),
          service_role_ (static_cast<zlink::service_role> (entry_.service_role)),
          channel_name_ (fixed_string_to_string (entry_.channel_name)),
          endpoint_ (fixed_string_to_string (entry_.endpoint)),
          routing_id_ (entry_.routing_id.size > 0
                        ? std::optional<routing_id_t> (
                            detail::native_routing_id (entry_.routing_id))
                        : std::nullopt),
          weight_ (entry_.weight),
          value_ (entry_.value)
    {
    }

    auto_connect_type_t auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    service_role_t service_role () const noexcept { return service_role_; }

    const std::string &channel_name () const noexcept { return channel_name_; }

    const std::string &endpoint () const noexcept { return endpoint_; }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return routing_id_;
    }

    peer_weight_t weight () const noexcept { return peer_weight_t::value (weight_); }

    int64_t value () const noexcept { return value_; }

  private:
    zlink::auto_connect_type auto_connect_type_;
    zlink::service_role service_role_;
    std::string channel_name_;
    std::string endpoint_;
    std::optional<routing_id_t> routing_id_;
    uint32_t weight_;
    int64_t value_;
};

class registry_topology_entry_t
{
  public:
    registry_topology_entry_t ()
        : auto_connect_type_ (zlink::auto_connect_type::invalid),
          routing_id_ (std::nullopt), service_kind_ (service_kind::socket),
          service_role_ (service_role::invalid), channel_name_ (), endpoint_ (),
          source_ (topology_source::manual), state_ (topology_state::discovered),
          desired_count_ (0), ready_count_ (0), error_code_ (0),
          last_reported_ms_ (0)
    {
    }

    explicit registry_topology_entry_t (
      const zlink_registry_topology_entry_t &entry_)
        : auto_connect_type_ (
            static_cast<zlink::auto_connect_type> (entry_.auto_connect_type)),
          routing_id_ (entry_.routing_id.size > 0
                        ? std::optional<routing_id_t> (
                            detail::native_routing_id (entry_.routing_id))
                        : std::nullopt),
          service_kind_ (static_cast<zlink::service_kind> (entry_.service_kind)),
          service_role_ (static_cast<zlink::service_role> (entry_.service_role)),
          channel_name_ (fixed_string_to_string (entry_.channel_name)),
          endpoint_ (fixed_string_to_string (entry_.endpoint)),
          source_ (static_cast<topology_source> (entry_.source)),
          state_ (static_cast<topology_state> (entry_.state)),
          desired_count_ (entry_.desired_count),
          ready_count_ (entry_.ready_count),
          error_code_ (entry_.error_code),
          last_reported_ms_ (entry_.last_reported_ms)
    {
    }

    auto_connect_type_t auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return routing_id_;
    }

    service_kind_t service_kind () const noexcept { return service_kind_; }

    service_role_t service_role () const noexcept { return service_role_; }

    const std::string &channel_name () const noexcept { return channel_name_; }

    const std::string &endpoint () const noexcept { return endpoint_; }

    topology_source_t source () const noexcept { return source_; }

    topology_state_t state () const noexcept { return state_; }

    uint32_t desired_count () const noexcept { return desired_count_; }

    uint32_t ready_count () const noexcept { return ready_count_; }

    uint32_t error_code () const noexcept { return error_code_; }

    std::chrono::milliseconds last_reported () const noexcept
    {
        return std::chrono::milliseconds (
          static_cast<int64_t> (last_reported_ms_));
    }

  private:
    zlink::auto_connect_type auto_connect_type_;
    std::optional<routing_id_t> routing_id_;
    zlink::service_kind service_kind_;
    zlink::service_role service_role_;
    std::string channel_name_;
    std::string endpoint_;
    topology_source source_;
    topology_state state_;
    uint32_t desired_count_;
    uint32_t ready_count_;
    uint32_t error_code_;
    uint64_t last_reported_ms_;
};

class spot_node_status_t
{
  public:
    spot_node_status_t ()
        : channel_name_ (), local_endpoint_ (), node_routing_id_ (std::nullopt),
          state_ (spot_node_state::idle), configured_peer_count_ (0),
          active_peer_count_ (0), connected_peer_count_ (0), subject_count_ (0),
          ready_subject_count_ (0), disconnected_sub_target_count_ (0),
          disconnected_routed_target_count_ (0), last_error_ (0),
          last_changed_ms_ (0)
    {
    }

    explicit spot_node_status_t (const zlink_spot_node_status_t &status_)
        : channel_name_ (fixed_string_to_string (status_.channel_name)),
          local_endpoint_ (fixed_string_to_string (status_.local_endpoint)),
          node_routing_id_ (status_.node_routing_id.size > 0
                             ? std::optional<routing_id_t> (
                                 detail::native_routing_id (status_.node_routing_id))
                             : std::nullopt),
          state_ (static_cast<spot_node_state> (status_.state)),
          configured_peer_count_ (status_.configured_peer_count),
          active_peer_count_ (status_.active_peer_count),
          connected_peer_count_ (status_.connected_peer_count),
          subject_count_ (status_.subject_count),
          ready_subject_count_ (status_.ready_subject_count),
          disconnected_sub_target_count_ (status_.disconnected_sub_target_count),
          disconnected_routed_target_count_ (
            status_.disconnected_routed_target_count),
          last_error_ (status_.last_error),
          last_changed_ms_ (status_.last_changed_ms)
    {
    }

    const std::string &channel_name () const noexcept { return channel_name_; }

    const std::string &local_endpoint () const noexcept { return local_endpoint_; }

    const std::optional<routing_id_t> &node_routing_id () const noexcept
    {
        return node_routing_id_;
    }

    spot_node_state_t state () const noexcept { return state_; }

    uint32_t configured_peer_count () const noexcept
    {
        return configured_peer_count_;
    }

    uint32_t active_peer_count () const noexcept { return active_peer_count_; }

    uint32_t connected_peer_count () const noexcept
    {
        return connected_peer_count_;
    }

    uint32_t subject_count () const noexcept { return subject_count_; }

    uint32_t ready_subject_count () const noexcept
    {
        return ready_subject_count_;
    }

    uint32_t disconnected_sub_target_count () const noexcept
    {
        return disconnected_sub_target_count_;
    }

    uint32_t disconnected_routed_target_count () const noexcept
    {
        return disconnected_routed_target_count_;
    }

    int32_t last_error () const noexcept { return last_error_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    std::string channel_name_;
    std::string local_endpoint_;
    std::optional<routing_id_t> node_routing_id_;
    spot_node_state state_;
    uint32_t configured_peer_count_;
    uint32_t active_peer_count_;
    uint32_t connected_peer_count_;
    uint32_t subject_count_;
    uint32_t ready_subject_count_;
    uint32_t disconnected_sub_target_count_;
    uint32_t disconnected_routed_target_count_;
    int32_t last_error_;
    uint64_t last_changed_ms_;
};

class registry_service_summary_entry_t
{
  public:
    registry_service_summary_entry_t ()
        : auto_connect_type_ (zlink::auto_connect_type::invalid),
          service_role_ (service_role::invalid), channel_name_ (),
          total_count_ (0), connecting_count_ (0), ready_count_ (0),
          error_count_ (0), stopped_count_ (0), last_reported_ms_ (0)
    {
    }

    explicit registry_service_summary_entry_t (
      const zlink_registry_service_summary_entry_t &entry_)
        : auto_connect_type_ (
            static_cast<zlink::auto_connect_type> (entry_.auto_connect_type)),
          service_role_ (static_cast<zlink::service_role> (entry_.service_role)),
          channel_name_ (fixed_string_to_string (entry_.channel_name)),
          total_count_ (entry_.total_count),
          connecting_count_ (entry_.connecting_count),
          ready_count_ (entry_.ready_count),
          error_count_ (entry_.error_count),
          stopped_count_ (entry_.stopped_count),
          last_reported_ms_ (entry_.last_reported_ms)
    {
    }

    auto_connect_type_t auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    service_role_t service_role () const noexcept { return service_role_; }

    const std::string &channel_name () const noexcept { return channel_name_; }

    uint32_t total_count () const noexcept { return total_count_; }

    uint32_t connecting_count () const noexcept { return connecting_count_; }

    uint32_t ready_count () const noexcept { return ready_count_; }

    uint32_t error_count () const noexcept { return error_count_; }

    uint32_t stopped_count () const noexcept { return stopped_count_; }

    std::chrono::milliseconds last_reported () const noexcept
    {
        return std::chrono::milliseconds (
          static_cast<int64_t> (last_reported_ms_));
    }

  private:
    zlink::auto_connect_type auto_connect_type_;
    zlink::service_role service_role_;
    std::string channel_name_;
    uint32_t total_count_;
    uint32_t connecting_count_;
    uint32_t ready_count_;
    uint32_t error_count_;
    uint32_t stopped_count_;
    uint64_t last_reported_ms_;
};

class registry_service_summary_filter_t
{
  public:
    registry_service_summary_filter_t () = default;

    registry_service_summary_filter_t &
    auto_connect_type (auto_connect_type_t value_)
    {
        auto_connect_type_ = value_;
        return *this;
    }

    registry_service_summary_filter_t &service_role (service_role_t value_)
    {
        service_role_ = value_;
        return *this;
    }

    registry_service_summary_filter_t &channel_name (std::string value_)
    {
        channel_name_ = std::move (value_);
        return *this;
    }

    const std::optional<auto_connect_type_t> &auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    const std::optional<service_role_t> &service_role () const noexcept
    {
        return service_role_;
    }

    const std::optional<std::string> &channel_name () const noexcept
    {
        return channel_name_;
    }

  private:
    std::optional<auto_connect_type_t> auto_connect_type_;
    std::optional<service_role_t> service_role_;
    std::optional<std::string> channel_name_;
};

class registry_status_t
{
  public:
    registry_status_t ()
        : registry_id_ (0), bind_endpoint_ (), state_ (registry_state::idle),
          topology_entry_count_ (0), peer_registry_count_ (0),
          connected_peer_registry_count_ (0), list_seq_ (0), last_error_ (0),
          last_changed_ms_ (0)
    {
    }

    explicit registry_status_t (const zlink_registry_status_t &status_)
        : registry_id_ (status_.registry_id),
          bind_endpoint_ (fixed_string_to_string (status_.bind_endpoint)),
          state_ (static_cast<registry_state> (status_.state)),
          topology_entry_count_ (status_.topology_entry_count),
          peer_registry_count_ (status_.peer_registry_count),
          connected_peer_registry_count_ (
            status_.connected_peer_registry_count),
          list_seq_ (status_.list_seq),
          last_error_ (status_.last_error),
          last_changed_ms_ (status_.last_changed_ms)
    {
    }

    uint32_t registry_id () const noexcept { return registry_id_; }

    const std::string &bind_endpoint () const noexcept { return bind_endpoint_; }

    registry_state_t state () const noexcept { return state_; }

    uint32_t topology_entry_count () const noexcept
    {
        return topology_entry_count_;
    }

    uint32_t peer_registry_count () const noexcept
    {
        return peer_registry_count_;
    }

    uint32_t connected_peer_registry_count () const noexcept
    {
        return connected_peer_registry_count_;
    }

    uint64_t list_seq () const noexcept { return list_seq_; }

    int32_t last_error () const noexcept { return last_error_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    uint32_t registry_id_;
    std::string bind_endpoint_;
    registry_state state_;
    uint32_t topology_entry_count_;
    uint32_t peer_registry_count_;
    uint32_t connected_peer_registry_count_;
    uint64_t list_seq_;
    int32_t last_error_;
    uint64_t last_changed_ms_;
};

class spot_node_peer_entry_t
{
  public:
    spot_node_peer_entry_t ()
        : channel_name_ (), local_endpoint_ (), peer_endpoint_ (),
          source_ (spot_peer_source::manual), state_ (spot_peer_state::configured),
          weight_ (0), connected_since_ms_ (0),
          last_changed_ms_ (0)
    {
    }

    explicit spot_node_peer_entry_t (const zlink_spot_node_peer_entry_t &entry_)
        : channel_name_ (fixed_string_to_string (entry_.channel_name)),
          local_endpoint_ (fixed_string_to_string (entry_.local_endpoint)),
          peer_endpoint_ (fixed_string_to_string (entry_.peer_endpoint)),
          source_ (static_cast<spot_peer_source> (entry_.source)),
          state_ (static_cast<spot_peer_state> (entry_.state)),
          weight_ (entry_.weight),
          connected_since_ms_ (entry_.connected_since_ms),
          last_changed_ms_ (entry_.last_changed_ms)
    {
    }

    const std::string &channel_name () const noexcept { return channel_name_; }

    const std::string &local_endpoint () const noexcept { return local_endpoint_; }

    const std::string &peer_endpoint () const noexcept { return peer_endpoint_; }

    spot_peer_source_t source () const noexcept { return source_; }

    spot_peer_state_t state () const noexcept { return state_; }

    peer_weight_t weight () const noexcept { return peer_weight_t::value (weight_); }

    std::chrono::milliseconds connected_since () const noexcept
    {
        return std::chrono::milliseconds (
          static_cast<int64_t> (connected_since_ms_));
    }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    std::string channel_name_;
    std::string local_endpoint_;
    std::string peer_endpoint_;
    spot_peer_source source_;
    spot_peer_state state_;
    uint32_t weight_;
    uint64_t connected_since_ms_;
    uint64_t last_changed_ms_;
};

class spot_node_peer_filter_t
{
  public:
    spot_node_peer_filter_t () = default;

    spot_node_peer_filter_t &peer_endpoint (std::string value_)
    {
        peer_endpoint_ = std::move (value_);
        return *this;
    }

    spot_node_peer_filter_t &source (spot_peer_source_t value_)
    {
        source_ = value_;
        return *this;
    }

    spot_node_peer_filter_t &state (spot_peer_state_t value_)
    {
        state_ = value_;
        return *this;
    }

    const std::optional<std::string> &peer_endpoint () const noexcept
    {
        return peer_endpoint_;
    }

    const std::optional<spot_peer_source_t> &source () const noexcept
    {
        return source_;
    }

    const std::optional<spot_peer_state_t> &state () const noexcept
    {
        return state_;
    }

  private:
    std::optional<std::string> peer_endpoint_;
    std::optional<spot_peer_source_t> source_;
    std::optional<spot_peer_state_t> state_;
};

class spot_node_subject_entry_t
{
  public:
    spot_node_subject_entry_t ()
        : role_ (spot_socket_role::pub), subject_ (),
          subject_kind_ (zlink::subject_kind::none), ready_peer_count_ (0),
          active_peer_count_ (0), last_changed_ms_ (0)
    {
    }

    explicit spot_node_subject_entry_t (
      const zlink_spot_node_subject_entry_t &entry_)
        : role_ (static_cast<spot_socket_role> (entry_.role)),
          subject_ (fixed_string_to_string (entry_.subject)),
          subject_kind_ (static_cast<zlink::subject_kind> (entry_.subject_kind)),
          ready_peer_count_ (entry_.ready_peer_count),
          active_peer_count_ (entry_.active_peer_count),
          last_changed_ms_ (entry_.last_changed_ms)
    {
    }

    spot_role_t role () const noexcept { return role_; }

    const std::string &subject () const noexcept { return subject_; }

    subject_kind_t subject_kind () const noexcept { return subject_kind_; }

    uint32_t ready_peer_count () const noexcept { return ready_peer_count_; }

    uint32_t active_peer_count () const noexcept { return active_peer_count_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    spot_socket_role role_;
    std::string subject_;
    zlink::subject_kind subject_kind_;
    uint32_t ready_peer_count_;
    uint32_t active_peer_count_;
    uint64_t last_changed_ms_;
};

class spot_node_subject_filter_t
{
  public:
    spot_node_subject_filter_t () = default;

    spot_node_subject_filter_t &role (spot_role_t value_)
    {
        role_ = value_;
        return *this;
    }

    spot_node_subject_filter_t &subject (std::string value_)
    {
        subject_ = std::move (value_);
        return *this;
    }

    spot_node_subject_filter_t &subject_kind (subject_kind_t value_)
    {
        subject_kind_ = value_;
        return *this;
    }

    const std::optional<spot_role_t> &role () const noexcept { return role_; }

    const std::optional<std::string> &subject () const noexcept
    {
        return subject_;
    }

    const std::optional<subject_kind_t> &subject_kind () const noexcept
    {
        return subject_kind_;
    }

  private:
    std::optional<spot_role_t> role_;
    std::optional<std::string> subject_;
    std::optional<subject_kind_t> subject_kind_;
};

class spot_node_socket_snapshot_filter_t
{
  public:
    spot_node_socket_snapshot_filter_t () = default;

    spot_node_socket_snapshot_filter_t &owner (spot_node_socket_owner_t value_)
    {
        owner_ = value_;
        return *this;
    }

    spot_node_socket_snapshot_filter_t &socket_type (
      spot_node_socket_type_t value_)
    {
        socket_type_ = value_;
        return *this;
    }

    spot_node_socket_snapshot_filter_t &socket_name (std::string value_)
    {
        socket_name_ = std::move (value_);
        return *this;
    }

    const std::optional<spot_node_socket_owner_t> &owner () const noexcept
    {
        return owner_;
    }

    const std::optional<spot_node_socket_type_t> &socket_type () const noexcept
    {
        return socket_type_;
    }

    const std::optional<std::string> &socket_name () const noexcept
    {
        return socket_name_;
    }

  private:
    std::optional<spot_node_socket_owner_t> owner_;
    std::optional<spot_node_socket_type_t> socket_type_;
    std::optional<std::string> socket_name_;
};

class spot_node_socket_snapshot_entry_t
{
  public:
    spot_node_socket_snapshot_entry_t ()
        : owner_ (spot_node_socket_owner::any), owner_id_ (0), owner_name_ (),
          socket_name_ (), socket_type_ (spot_node_socket_type_t::pair),
          auto_hwm_visible_ (false), snapshot_ ()
    {
    }

    explicit spot_node_socket_snapshot_entry_t (
      const zlink_spot_node_socket_snapshot_entry_t &entry_)
        : owner_ (static_cast<spot_node_socket_owner> (entry_.owner)),
          owner_id_ (entry_.owner_id),
          owner_name_ (fixed_string_to_string (entry_.owner_name)),
          socket_name_ (fixed_string_to_string (entry_.socket_name)),
          socket_type_ (
            static_cast<spot_node_socket_type_t> (entry_.socket_type)),
          auto_hwm_visible_ (entry_.auto_hwm_visible != 0),
          snapshot_ (entry_.snapshot)
    {
    }

    spot_node_socket_owner_t owner () const noexcept { return owner_; }

    uint64_t owner_id () const noexcept { return owner_id_; }

    const std::string &owner_name () const noexcept { return owner_name_; }

    const std::string &socket_name () const noexcept { return socket_name_; }

    spot_node_socket_type_t socket_type () const noexcept
    {
        return socket_type_;
    }

    bool auto_hwm_visible () const noexcept { return auto_hwm_visible_; }

    const monitor_snapshot_t &snapshot () const noexcept { return snapshot_; }

  private:
    spot_node_socket_owner owner_;
    uint64_t owner_id_;
    std::string owner_name_;
    std::string socket_name_;
    spot_node_socket_type_t socket_type_;
    bool auto_hwm_visible_;
    monitor_snapshot_t snapshot_;
};

class spot_node_spot_entry_t
{
  public:
    spot_node_spot_entry_t ()
        : spot_rid_ (detail::unchecked_empty_routing_id ()),
          dispatch_handler_attached_ (false),
          joined_actor_count_ (0), pending_actor_join_count_ (0),
          route_synced_ (false), last_changed_ms_ (0)
    {
    }

    explicit spot_node_spot_entry_t (
      const zlink_spot_node_spot_entry_t &entry_)
        : spot_rid_ (detail::native_routing_id (entry_.spot_rid)),
          dispatch_handler_attached_ (entry_.dispatch_handler_attached != 0),
          joined_actor_count_ (entry_.joined_actor_count),
          pending_actor_join_count_ (entry_.pending_actor_join_count),
          route_synced_ (entry_.route_synced != 0),
          last_changed_ms_ (entry_.last_changed_ms)
    {
    }

    const routing_id_t &spot_rid () const noexcept { return spot_rid_; }

    bool dispatch_handler_attached () const noexcept
    {
        return dispatch_handler_attached_;
    }

    uint32_t joined_actor_count () const noexcept
    {
        return joined_actor_count_;
    }

    uint32_t pending_actor_join_count () const noexcept
    {
        return pending_actor_join_count_;
    }

    bool route_synced () const noexcept { return route_synced_; }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    routing_id_t spot_rid_;
    bool dispatch_handler_attached_;
    uint32_t joined_actor_count_;
    uint32_t pending_actor_join_count_;
    bool route_synced_;
    uint64_t last_changed_ms_;
};

class spot_node_actor_entry_t
{
  public:
    spot_node_actor_entry_t ()
        : actor_ (), joined_ (false), joined_spot_rid_ (std::nullopt),
          route_synced_ (false), pending_message_count_ (0),
          last_changed_ms_ (0)
    {
    }

    explicit spot_node_actor_entry_t (
      const zlink_spot_node_actor_entry_t &entry_)
        : actor_ (entry_.actor),
          joined_ (entry_.joined != 0),
          joined_spot_rid_ (
            entry_.joined_spot_rid.size > 0
              ? std::optional<routing_id_t> (
                  detail::native_routing_id (entry_.joined_spot_rid))
              : std::nullopt),
          route_synced_ (entry_.route_synced != 0),
          pending_message_count_ (entry_.pending_message_count),
          last_changed_ms_ (entry_.last_changed_ms)
    {
    }

    const actor_ref_t &actor () const noexcept { return actor_; }

    bool joined () const noexcept { return joined_; }

    const std::optional<routing_id_t> &joined_spot_rid () const noexcept
    {
        return joined_spot_rid_;
    }

    bool route_synced () const noexcept { return route_synced_; }

    uint32_t pending_message_count () const noexcept
    {
        return pending_message_count_;
    }

    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    actor_ref_t actor_;
    bool joined_;
    std::optional<routing_id_t> joined_spot_rid_;
    bool route_synced_;
    uint32_t pending_message_count_;
    uint64_t last_changed_ms_;
};

enum class spot_service_attachment_role_t
{
    router = 1,
    pub = 2,
    sub = 3
};

struct spot_service_attachment_stats_t
{
    std::string channel_name;
    uint32_t router_count = 0;
    uint32_t pub_count = 0;
    uint32_t sub_count = 0;
    uint32_t auto_router_count = 0;
    uint32_t auto_pub_count = 0;
    uint32_t auto_sub_count = 0;
};

template<size_t N> inline std::string fixed_string_to_string (const char (&src_)[N]);

class registry_topology_filter_t
{
  public:
    registry_topology_filter_t () = default;

    registry_topology_filter_t &auto_connect_type (auto_connect_type_t value_)
    {
        auto_connect_type_ = value_;
        return *this;
    }

    registry_topology_filter_t &service_kind (service_kind_t value_)
    {
        service_kind_ = value_;
        return *this;
    }

    registry_topology_filter_t &service_role (service_role_t value_)
    {
        service_role_ = value_;
        return *this;
    }

    registry_topology_filter_t &channel_name (std::string value_)
    {
        channel_name_ = std::move (value_);
        return *this;
    }

    registry_topology_filter_t &routing_id (routing_id_t value_)
    {
        routing_id_ = std::move (value_);
        return *this;
    }

    registry_topology_filter_t &state (topology_state_t value_)
    {
        state_ = value_;
        return *this;
    }

    registry_topology_filter_t &source (topology_source_t value_)
    {
        source_ = value_;
        return *this;
    }

    const std::optional<auto_connect_type_t> &auto_connect_type () const noexcept
    {
        return auto_connect_type_;
    }

    const std::optional<service_kind_t> &service_kind () const noexcept
    {
        return service_kind_;
    }

    const std::optional<service_role_t> &service_role () const noexcept
    {
        return service_role_;
    }

    const std::optional<std::string> &channel_name () const noexcept
    {
        return channel_name_;
    }

    const std::optional<routing_id_t> &routing_id () const noexcept
    {
        return routing_id_;
    }

    const std::optional<topology_state_t> &state () const noexcept
    {
        return state_;
    }

    const std::optional<topology_source_t> &source () const noexcept
    {
        return source_;
    }

  private:
    std::optional<auto_connect_type_t> auto_connect_type_;
    std::optional<service_kind_t> service_kind_;
    std::optional<service_role_t> service_role_;
    std::optional<std::string> channel_name_;
    std::optional<routing_id_t> routing_id_;
    std::optional<topology_state_t> state_;
    std::optional<topology_source_t> source_;
};

template<size_t N> inline std::string fixed_string_to_string (const char (&src_)[N])
{
    size_t len = 0;
    while (len < N && src_[len] != '\0')
        ++len;
    return std::string (src_, len);
}

inline std::string
channel_name (const zlink_member_peer_entry_t &entry_)
{
    return fixed_string_to_string (entry_.channel_name);
}

inline std::string endpoint (const zlink_member_peer_entry_t &entry_)
{
    return fixed_string_to_string (entry_.endpoint);
}

inline std::string
channel_name (const zlink_registry_topology_entry_t &entry_)
{
    return fixed_string_to_string (entry_.channel_name);
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
