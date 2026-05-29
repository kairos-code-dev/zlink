/* SPDX-License-Identifier: MPL-2.0 */
#include <zlink/Contracts/Messaging/received.hpp>
#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink/Contracts/Sockets/socket_options.hpp>
#include <Runtime/Core/routing_id_access.hpp>
#include <Runtime/Sockets/socket_access.hpp>
#include <Runtime/Options/option_ids.hpp>

#include <zlink.h>

namespace zlink
{
namespace detail
{

inline void ensure_config_handle (void *handle_)
{
    if (!handle_)
        throw config_error_t (config_result_t::invalid_handle, EINVAL);
}

inline void *native_option_handle (base_socket_t *socket_)
{
    if (!socket_)
        throw config_error_t (config_result_t::invalid_handle, EINVAL);
    return native_handle (*socket_);
}

std::unordered_map<void *, std::unordered_map<int, int>>
&dealer_option_store ()
{
    static std::unordered_map<void *, std::unordered_map<int, int>> values;
    return values;
}

template<typename T, typename NativeOption, typename Getter>
inline T get_option_value (void *handle_,
                           NativeOption option_,
                           Getter getter_)
{
    ensure_config_handle (handle_);
    T value {};
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        getter_ (handle_, option_, &value, &size)));
    return value;
}

template<typename T, typename NativeOption, typename Setter>
inline void set_option_value (void *handle_,
                              NativeOption option_,
                              const T &value_,
                              Setter setter_)
{
    ensure_config_handle (handle_);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        setter_ (handle_, option_, &value_, sizeof (value_))));
}

template<typename NativeOption, typename Getter>
std::string get_option_string_value (
  void *handle_,
  NativeOption option_,
  size_t initial_cap_,
  Getter getter_)
{
    ensure_config_handle (handle_);
    size_t cap = initial_cap_;
    const size_t max_cap = 64u * 1024u;
    while (cap <= max_cap) {
        std::vector<char> buffer (cap);
        size_t size = cap;
        const config_result_t result = static_cast<config_result_t> (
          getter_ (handle_, option_, buffer.data (), &size));
        if (result == config_result_t::ok) {
            const size_t bounded = size <= buffer.size () ? size : buffer.size ();
            size_t out_size = bounded;
            if (out_size > 0 && buffer[out_size - 1] == '\0')
                --out_size;
            return std::string (buffer.data (), out_size);
        }
        if (errno != EINVAL || cap == max_cap)
            throw config_error_t (result, zlink_errno ());
        cap *= 2u;
        if (cap > max_cap)
            cap = max_cap;
    }
    throw config_error_t (config_result_t::invalid_argument, EINVAL);
}

template<typename T>
T get_common_option_value (void *handle_, socket_option_id option_)
{
    return get_option_value<T> (
      handle_, static_cast<zlink_option_t> (option_), zlink_get_option);
}

template<typename T>
inline void set_common_option_value (void *handle_,
                                     socket_option_id option_,
                                     const T &value_)
{
    set_option_value<T> (
      handle_, static_cast<zlink_option_t> (option_), value_, zlink_set_option);
}

std::string get_common_option_string (void *handle_, socket_option_id option_)
{
    const size_t cap =
      option_ == detail::socket_option_id::last_endpoint ? 1024u : 256u;
    return get_option_string_value (
      handle_, static_cast<zlink_option_t> (option_), cap, zlink_get_option);
}

std::string get_pub_option_string (void *handle_,
                                          pub_option_id option_)
{
    return get_option_string_value (
      handle_, static_cast<zlink_pub_option_t> (option_), 256u,
      zlink_get_pub_option);
}

template<typename T>
T get_router_option_value (void *handle_, router_option_id option_)
{
    return get_option_value<T> (
      handle_, static_cast<zlink_router_option_t> (option_),
      zlink_get_router_option);
}

template<typename T>
inline void set_router_option_value (void *handle_,
                                     router_option_id option_,
                                     const T &value_)
{
    set_option_value<T> (
      handle_, static_cast<zlink_router_option_t> (option_), value_,
      zlink_set_router_option);
}

template<typename T>
T get_dealer_option_value (void *handle_, dealer_option_id option_)
{
    ensure_config_handle (handle_);
    typename std::unordered_map<void *, std::unordered_map<int, int>>::const_iterator it =
      dealer_option_store ().find (handle_);
    if (it != dealer_option_store ().end ()) {
        typename std::unordered_map<int, int>::const_iterator value_it =
          it->second.find (static_cast<int> (option_));
        if (value_it != it->second.end ())
            return static_cast<T> (value_it->second);
    }
    switch (option_) {
    case detail::dealer_option_id::weight:
        return static_cast<T> (100);
    default:
        return T ();
    }
}

template<typename T>
inline void set_dealer_option_value (void *handle_,
                                     dealer_option_id option_,
                                     const T &value_)
{
    ensure_config_handle (handle_);
    set_option_value<T> (
      handle_, static_cast<zlink_dealer_option_t> (option_), value_,
      zlink_set_dealer_option);
    dealer_option_store ()[handle_][static_cast<int> (option_)] =
      static_cast<int> (value_);
}

template<typename T>
T get_pub_option_value (void *handle_, pub_option_id option_)
{
    return get_option_value<T> (
      handle_, static_cast<zlink_pub_option_t> (option_), zlink_get_pub_option);
}

template<typename T>
inline void set_pub_option_value (void *handle_,
                                  pub_option_id option_,
                                  const T &value_)
{
    set_option_value<T> (
      handle_, static_cast<zlink_pub_option_t> (option_), value_,
      zlink_set_pub_option);
}

template<typename T>
T get_sub_option_value (void *handle_, sub_option_id option_)
{
    return get_option_value<T> (
      handle_, static_cast<zlink_sub_option_t> (option_), zlink_get_sub_option);
}

template<typename T>
T get_stream_option_value (void *handle_, stream_option_id option_)
{
    return get_option_value<T> (
      handle_, static_cast<zlink_stream_option_t> (option_),
      zlink_get_stream_option);
}

template<typename T>
inline void set_stream_option_value (void *handle_,
                                     stream_option_id option_,
                                     const T &value_)
{
    set_option_value<T> (
      handle_, static_cast<zlink_stream_option_t> (option_), value_,
      zlink_set_stream_option);
}

inline recv_error_t invalid_single_part_error ()
{
    return recv_error_t (recv_result_t::not_supported, EMSGSIZE);
}

void close_parts (std::vector<message_t> &parts_)
{
    for (std::vector<message_t>::iterator it = parts_.begin ();
         it != parts_.end (); ++it)
        it->close ();
    parts_.clear ();
}

} // namespace detail

common_socket_options_t::common_socket_options_t () noexcept
    : _socket (nullptr)
{
}

common_socket_options_t::common_socket_options_t (
  base_socket_t &socket_) noexcept
    : _socket (&socket_)
{
}

router_socket_options_t::router_socket_options_t () noexcept = default;

router_socket_options_t::router_socket_options_t (
  base_socket_t &socket_) noexcept
    : common_socket_options_t (socket_)
{
}

dealer_socket_options_t::dealer_socket_options_t () noexcept = default;

dealer_socket_options_t::dealer_socket_options_t (
  base_socket_t &socket_) noexcept
    : common_socket_options_t (socket_)
{
}

stream_socket_options_t::stream_socket_options_t () noexcept = default;

stream_socket_options_t::stream_socket_options_t (
  base_socket_t &socket_) noexcept
    : common_socket_options_t (socket_)
{
}

pub_socket_options_t::pub_socket_options_t () noexcept = default;

pub_socket_options_t::pub_socket_options_t (base_socket_t &socket_) noexcept
    : common_socket_options_t (socket_)
{
}

sub_socket_options_t::sub_socket_options_t () noexcept = default;

sub_socket_options_t::sub_socket_options_t (base_socket_t &socket_) noexcept
    : common_socket_options_t (socket_)
{
}

std::chrono::milliseconds common_socket_options_t::linger () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::linger));
}

void common_socket_options_t::linger (std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::linger,
      static_cast<int> (value.count ()));
}

message_count_t common_socket_options_t::send_hwm () const
{
    return message_count_t::value (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::sndhwm));
}

void common_socket_options_t::send_hwm (message_count_t value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::sndhwm, value.value ());
}

message_count_t common_socket_options_t::recv_hwm () const
{
    return message_count_t::value (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::rcvhwm));
}

void common_socket_options_t::recv_hwm (message_count_t value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::rcvhwm, value.value ());
}

std::chrono::milliseconds common_socket_options_t::send_timeout () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::sndtimeo));
}

void common_socket_options_t::send_timeout (std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::sndtimeo,
      static_cast<int> (value.count ()));
}

std::chrono::milliseconds common_socket_options_t::recv_timeout () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::rcvtimeo));
}

void common_socket_options_t::recv_timeout (std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::rcvtimeo,
      static_cast<int> (value.count ()));
}

bool common_socket_options_t::immediate () const
{
    return detail::get_common_option_value<int> (
             detail::native_option_handle (_socket), detail::socket_option_id::immediate)
           != 0;
}

void common_socket_options_t::immediate (bool value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::immediate, value ? 1 : 0);
}

std::chrono::milliseconds common_socket_options_t::connect_timeout () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::connect_timeout));
}

void common_socket_options_t::connect_timeout (std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::connect_timeout,
      static_cast<int> (value.count ()));
}

bool common_socket_options_t::ipv6 () const
{
    return detail::get_common_option_value<int> (detail::native_option_handle (_socket), detail::socket_option_id::ipv6)
           != 0;
}

void common_socket_options_t::ipv6 (bool value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::ipv6, value ? 1 : 0);
}

bool common_socket_options_t::tcp_no_delay () const
{
    return detail::get_common_option_value<int> (
             detail::native_option_handle (_socket), detail::socket_option_id::tcp_nodelay)
           != 0;
}

void common_socket_options_t::tcp_no_delay (bool value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::tcp_nodelay, value ? 1 : 0);
}

tcp_keepalive_mode_t common_socket_options_t::tcp_keepalive () const
{
    return static_cast<tcp_keepalive_mode_t> (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::tcp_keepalive));
}

void common_socket_options_t::tcp_keepalive (tcp_keepalive_mode_t value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::tcp_keepalive,
      static_cast<int> (value));
}

std::chrono::milliseconds common_socket_options_t::heartbeat_interval () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::heartbeat_ivl));
}

void common_socket_options_t::heartbeat_interval (
  std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::heartbeat_ivl,
      static_cast<int> (value.count ()));
}

std::chrono::milliseconds common_socket_options_t::heartbeat_ttl () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::heartbeat_ttl));
}

void common_socket_options_t::heartbeat_ttl (
  std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::heartbeat_ttl,
      static_cast<int> (value.count ()));
}

std::chrono::milliseconds common_socket_options_t::heartbeat_timeout () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::heartbeat_timeout));
}

void common_socket_options_t::heartbeat_timeout (
  std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::heartbeat_timeout,
      static_cast<int> (value.count ()));
}

rid_duplicate_policy_t common_socket_options_t::rid_duplicate_policy () const
{
    return static_cast<rid_duplicate_policy_t> (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::rid_duplicate_policy));
}

void common_socket_options_t::rid_duplicate_policy (
  rid_duplicate_policy_t value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::rid_duplicate_policy,
      static_cast<int> (value));
}

byte_size_t common_socket_options_t::max_message_size () const
{
    return byte_size_t::bytes (
      detail::get_common_option_value<int64_t> (
        detail::native_option_handle (_socket), detail::socket_option_id::maxmsgsize));
}

void common_socket_options_t::max_message_size (byte_size_t value)
{
    detail::set_common_option_value<int64_t> (
      detail::native_option_handle (_socket), detail::socket_option_id::maxmsgsize, value.bytes ());
}

socket_backlog_t common_socket_options_t::backlog () const
{
    return socket_backlog_t::value (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::backlog));
}

void common_socket_options_t::backlog (socket_backlog_t value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::backlog, value.value ());
}

std::chrono::milliseconds common_socket_options_t::reconnect_interval () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::reconnect_ivl));
}

void common_socket_options_t::reconnect_interval (
  std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::reconnect_ivl,
      static_cast<int> (value.count ()));
}

std::chrono::milliseconds
common_socket_options_t::reconnect_interval_max () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket), detail::socket_option_id::reconnect_ivl_max));
}

void common_socket_options_t::reconnect_interval_max (
  std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::reconnect_ivl_max,
      static_cast<int> (value.count ()));
}

submit_retry_mode_t common_socket_options_t::submit_retry_mode () const
{
    return static_cast<submit_retry_mode_t> (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket),
        detail::socket_option_id::submit_retry_mode));
}

void common_socket_options_t::submit_retry_mode (submit_retry_mode_t value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket),
      detail::socket_option_id::submit_retry_mode, static_cast<int> (value));
}

std::chrono::milliseconds common_socket_options_t::submit_retry_timeout () const
{
    return std::chrono::milliseconds (
      detail::get_common_option_value<int> (
        detail::native_option_handle (_socket),
        detail::socket_option_id::submit_retry_timeout));
}

void common_socket_options_t::submit_retry_timeout (
  std::chrono::milliseconds value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket),
      detail::socket_option_id::submit_retry_timeout,
      static_cast<int> (value.count ()));
}

int common_socket_options_t::submit_retry_attempts () const
{
    return detail::get_common_option_value<int> (
      detail::native_option_handle (_socket),
      detail::socket_option_id::submit_retry_attempts);
}

void common_socket_options_t::submit_retry_attempts (int value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket),
      detail::socket_option_id::submit_retry_attempts, value);
}

std::string common_socket_options_t::last_endpoint () const
{
    return detail::get_common_option_string (detail::native_option_handle (_socket), detail::socket_option_id::last_endpoint);
}

bool router_socket_options_t::mandatory () const
{
    return detail::get_router_option_value<int> (detail::native_option_handle (_socket), detail::router_option_id::mandatory)
           != 0;
}

void router_socket_options_t::mandatory (bool value)
{
    detail::set_router_option_value<int> (
      detail::native_option_handle (_socket), detail::router_option_id::mandatory, value ? 1 : 0);
}

bool router_socket_options_t::handover () const
{
    return detail::get_common_option_value<int> (
             detail::native_option_handle (_socket), detail::socket_option_id::rid_duplicate_policy)
           == ZLINK_RID_DUPLICATE_HANDOVER;
}

void router_socket_options_t::handover (bool value)
{
    detail::set_common_option_value<int> (
      detail::native_option_handle (_socket), detail::socket_option_id::rid_duplicate_policy,
      value ? ZLINK_RID_DUPLICATE_HANDOVER : ZLINK_RID_DUPLICATE_REJECT);
}

bool router_socket_options_t::probe () const
{
    return detail::get_router_option_value<int> (detail::native_option_handle (_socket), detail::router_option_id::probe)
           != 0;
}

void router_socket_options_t::probe (bool value)
{
    detail::set_router_option_value<int> (
      detail::native_option_handle (_socket), detail::router_option_id::probe, value ? 1 : 0);
}

std::optional<routing_id_t> router_socket_options_t::connect_routing_id () const
{
    detail::ensure_config_handle (detail::native_option_handle (_socket));
    zlink_routing_id_t native;
    std::memset (&native, 0, sizeof (native));
    size_t size = sizeof (native);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_get_router_option (
          detail::native_option_handle (_socket),
          static_cast<zlink_router_option_t> (detail::router_option_id::connect_routing_id),
          &native, &size)));
    if (native.size == 0)
        return std::nullopt;
    return zlink::detail::native_routing_id (native);
}

void router_socket_options_t::connect_routing_id (const routing_id_t &value)
{
    detail::ensure_config_handle (detail::native_option_handle (_socket));
    const zlink_routing_id_t native =
      *zlink::detail::routing_id_native (value);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_router_option (
          detail::native_option_handle (_socket),
          static_cast<zlink_router_option_t> (detail::router_option_id::connect_routing_id),
          native.data, native.size)));
}

std::chrono::milliseconds router_socket_options_t::request_timeout () const
{
    return std::chrono::milliseconds (
      detail::get_router_option_value<int> (
        detail::native_option_handle (_socket), detail::router_option_id::request_timeout_ms));
}

void router_socket_options_t::request_timeout (
  std::chrono::milliseconds value)
{
    detail::set_router_option_value<int> (
      detail::native_option_handle (_socket), detail::router_option_id::request_timeout_ms,
      static_cast<int> (value.count ()));
}

peer_weight_t router_socket_options_t::peer_weight () const
{
    return peer_weight_t::value (
      detail::get_router_option_value<uint32_t> (
        detail::native_option_handle (_socket), detail::router_option_id::weight));
}

void router_socket_options_t::peer_weight (peer_weight_t value)
{
    detail::set_router_option_value<uint32_t> (
      detail::native_option_handle (_socket), detail::router_option_id::weight, value.value ());
}

bool dealer_socket_options_t::probe () const
{
    return detail::get_dealer_option_value<int> (detail::native_option_handle (_socket), detail::dealer_option_id::probe)
           != 0;
}

void dealer_socket_options_t::probe (bool value)
{
    detail::set_dealer_option_value<int> (
      detail::native_option_handle (_socket), detail::dealer_option_id::probe, value ? 1 : 0);
}

std::chrono::milliseconds dealer_socket_options_t::request_timeout () const
{
    return std::chrono::milliseconds (
      detail::get_dealer_option_value<int> (
        detail::native_option_handle (_socket), detail::dealer_option_id::request_timeout_ms));
}

void dealer_socket_options_t::request_timeout (
  std::chrono::milliseconds value)
{
    detail::set_dealer_option_value<int> (
      detail::native_option_handle (_socket), detail::dealer_option_id::request_timeout_ms,
      static_cast<int> (value.count ()));
}

peer_weight_t dealer_socket_options_t::peer_weight () const
{
    return peer_weight_t::value (
      detail::get_dealer_option_value<uint32_t> (
        detail::native_option_handle (_socket), detail::dealer_option_id::weight));
}

void dealer_socket_options_t::peer_weight (peer_weight_t value)
{
    detail::set_dealer_option_value<uint32_t> (
      detail::native_option_handle (_socket), detail::dealer_option_id::weight, value.value ());
}

bool stream_socket_options_t::notify () const
{
    return detail::get_stream_option_value<int> (detail::native_option_handle (_socket), detail::stream_option_id::notify)
           != 0;
}

void stream_socket_options_t::notify (bool value)
{
    detail::set_stream_option_value<int> (
      detail::native_option_handle (_socket), detail::stream_option_id::notify, value ? 1 : 0);
}

bool pub_socket_options_t::verbose () const
{
    return detail::get_pub_option_value<int> (detail::native_option_handle (_socket), detail::pub_option_id::verbose) != 0;
}

void pub_socket_options_t::verbose (bool value)
{
    detail::set_pub_option_value<int> (
      detail::native_option_handle (_socket), detail::pub_option_id::verbose, value ? 1 : 0);
}

bool pub_socket_options_t::verboser () const
{
    return detail::get_pub_option_value<int> (detail::native_option_handle (_socket), detail::pub_option_id::verboser) != 0;
}

void pub_socket_options_t::verboser (bool value)
{
    detail::set_pub_option_value<int> (
      detail::native_option_handle (_socket), detail::pub_option_id::verboser, value ? 1 : 0);
}

bool pub_socket_options_t::no_drop () const
{
    return detail::get_pub_option_value<int> (detail::native_option_handle (_socket), detail::pub_option_id::nodrop) != 0;
}

void pub_socket_options_t::no_drop (bool value)
{
    detail::set_pub_option_value<int> (
      detail::native_option_handle (_socket), detail::pub_option_id::nodrop, value ? 1 : 0);
}

bool pub_socket_options_t::manual () const
{
    return detail::get_pub_option_value<int> (detail::native_option_handle (_socket), detail::pub_option_id::manual) != 0;
}

void pub_socket_options_t::manual (bool value)
{
    detail::set_pub_option_value<int> (
      detail::native_option_handle (_socket), detail::pub_option_id::manual, value ? 1 : 0);
}

bool pub_socket_options_t::manual_last_value () const
{
    return detail::get_pub_option_value<int> (
             detail::native_option_handle (_socket), detail::pub_option_id::manual_last_value)
           != 0;
}

void pub_socket_options_t::manual_last_value (bool value)
{
    detail::set_pub_option_value<int> (
      detail::native_option_handle (_socket), detail::pub_option_id::manual_last_value, value ? 1 : 0);
}

message_t pub_socket_options_t::welcome_message () const
{
    const std::string value =
      detail::get_pub_option_string (
        detail::native_option_handle (_socket), detail::pub_option_id::welcome_msg);
    return message_t::from_bytes (
      std::as_bytes (std::span<const char> (value.data (), value.size ())));
}

void pub_socket_options_t::welcome_message (const message_t &message)
{
    detail::ensure_config_handle (detail::native_option_handle (_socket));
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_pub_option (
          detail::native_option_handle (_socket), ZLINK_PUB_OPT_WELCOME_MSG, message.data (),
          message.size ())));
}

void pub_socket_options_t::approve_subscribe (
  const routing_id_t &routing_id)
{
    detail::ensure_config_handle (detail::native_option_handle (_socket));
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_pub_option (
          detail::native_option_handle (_socket), ZLINK_PUB_OPT_APPROVE_SUBSCRIBE, routing_id.data (),
          routing_id.size ())));
}

void pub_socket_options_t::reject_subscribe (
  const routing_id_t &routing_id)
{
    detail::ensure_config_handle (detail::native_option_handle (_socket));
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (
        zlink_set_pub_option (
          detail::native_option_handle (_socket), ZLINK_PUB_OPT_REJECT_SUBSCRIBE, routing_id.data (),
          routing_id.size ())));
}

int pub_socket_options_t::topics_count () const
{
    return detail::get_pub_option_value<int> (
      detail::native_option_handle (_socket), detail::pub_option_id::topics_count);
}

int sub_socket_options_t::topics_count () const
{
    return detail::get_sub_option_value<int> (detail::native_option_handle (_socket), detail::sub_option_id::topics_count);
}

message_t &received_t::first_part ()
{
    if (!is_single_part ())
        throw detail::invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

void received_t::materialize_parts () const
{
    if (!_single_part.has_value ())
        return;
    message_t part = std::move (*_single_part);
    _single_part.reset ();
    _parts.push_back (std::move (part));
}

const std::vector<message_t> &received_t::parts () const
{
    materialize_parts ();
    return _parts;
}

std::vector<message_t> &received_t::parts ()
{
    materialize_parts ();
    return _parts;
}

message_t topic_message_t::single_part_or_throw ()
{
    if (!is_single_part ())
        throw detail::invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

message_t &topic_message_t::first_part ()
{
    if (!is_single_part ())
        throw detail::invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

void topic_message_t::materialize_parts () const
{
    if (!_single_part.has_value ())
        return;
    message_t part = std::move (*_single_part);
    _single_part.reset ();
    _parts.push_back (std::move (part));
}

const std::vector<message_t> &topic_message_t::parts () const
{
    materialize_parts ();
    return _parts;
}

std::vector<message_t> &topic_message_t::parts ()
{
    materialize_parts ();
    return _parts;
}

message_t received_t::single_part_or_throw ()
{
    if (!is_single_part ())
        throw detail::invalid_single_part_error ();
    if (_single_part.has_value ())
        return *_single_part;
    return _parts.front ();
}

// received_t::send() and received_t::reply() are defined in
// zlink/services/spot.hpp, after send_op_t and reply_op_t are fully defined.

void received_t::close ()
{
    if (_single_part.has_value ()) {
        _single_part->close ();
        _single_part.reset ();
    }
    detail::close_parts (_parts);
}

void topic_message_t::close ()
{
    if (_single_part.has_value ()) {
        _single_part->close ();
        _single_part.reset ();
    }
    detail::close_parts (_parts);
}

} // namespace zlink
