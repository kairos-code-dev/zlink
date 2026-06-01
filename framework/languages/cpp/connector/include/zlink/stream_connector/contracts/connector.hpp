/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace zlink::stream_connector
{

namespace detail
{
class connector_state_t;
class connector_runtime_t;
} // namespace detail

enum class transport_t
{
  tcp,
  tls,
  websocket,
  websocket_secure
};

enum class codec_t : std::uint8_t
{
  raw = 0,
  json = 1,
  message_pack = 2,
  protobuf = 3
};

enum class compression_t
{
  none,
  lz4
};

enum class dispatch_mode_t
{
  manual,
  immediate
};

enum class message_kind_t : std::uint8_t
{
  send = 1,
  request = 2,
  response = 3,
  error = 4,
  control = 5
};

enum class header_flags_t : std::uint8_t
{
  none = 0,
  has_request_seq = 0x01,
  has_metadata = 0x02,
  payload_compressed = 0x04
};

constexpr header_flags_t operator| (header_flags_t lhs,
                                    header_flags_t rhs) noexcept
{
  return static_cast<header_flags_t> (
    static_cast<std::uint8_t> (lhs) | static_cast<std::uint8_t> (rhs));
}

enum class error_code_t
{
  disconnected,
  configuration_error,
  validation_failed,
  request_timeout,
  connect_timeout,
  frame_decode_failed,
  frame_too_large,
  send_failed,
  unsupported_codec,
  compression_failed,
  tls_validation_failed,
  decompression_failed,
  user_callback_failed,
  remote_error
};

enum class connection_state_t
{
  created,
  connecting,
  connected,
  reconnecting,
  disconnected,
  closed
};

struct error_t
{
  error_code_t code = error_code_t::disconnected;
  std::string message;
};

template<typename T>
class result_t
{
public:
  static result_t success (T value) { return result_t (std::move (value)); }
  static result_t failure (error_code_t code, std::string message)
  {
    return result_t (error_t { code, std::move (message) });
  }

  explicit operator bool () const noexcept { return _value.has_value (); }
  const T &value () const { return *_value; }
  T &value () { return *_value; }
  const std::optional<error_t> &error () const noexcept { return _error; }
  error_code_t error_code () const { return _error->code; }

private:
  explicit result_t (T value) : _value (std::move (value)) {}
  explicit result_t (error_t error) : _error (std::move (error)) {}

  std::optional<T> _value;
  std::optional<error_t> _error;
};

template<>
class result_t<void>
{
public:
  static result_t success () { return result_t (); }
  static result_t failure (error_code_t code, std::string message)
  {
    return result_t (error_t { code, std::move (message) });
  }

  explicit operator bool () const noexcept { return !_error.has_value (); }
  const std::optional<error_t> &error () const noexcept { return _error; }
  error_code_t error_code () const { return _error->code; }

private:
  result_t () = default;
  explicit result_t (error_t error) : _error (std::move (error)) {}

  std::optional<error_t> _error;
};

template<typename T>
class task_t
{
public:
  struct promise_type
  {
    std::optional<result_t<T>> result;

    task_t get_return_object ()
    {
      return task_t (
        std::coroutine_handle<promise_type>::from_promise (*this));
    }
    std::suspend_never initial_suspend () noexcept { return {}; }
    std::suspend_always final_suspend () noexcept { return {}; }
    void unhandled_exception ()
    {
      result = result_t<T>::failure (
        error_code_t::user_callback_failed,
        "unhandled connector coroutine exception");
    }
    template<typename U>
    void return_value (U &&value)
    {
      result = result_t<T>::success (T (std::forward<U> (value)));
    }
  };

  explicit task_t (result_t<T> result) : _result (std::move (result)) {}
  task_t (task_t &&other) noexcept
    : _handle (other._handle), _result (std::move (other._result))
  {
    other._handle = {};
  }
  task_t &operator= (task_t &&other) noexcept
  {
    if (this != &other) {
      if (_handle) {
        _handle.destroy ();
      }
      _handle = other._handle;
      _result = std::move (other._result);
      other._handle = {};
    }
    return *this;
  }
  task_t (const task_t &) = delete;
  task_t &operator= (const task_t &) = delete;
  ~task_t ()
  {
    if (_handle) {
      _handle.destroy ();
    }
  }

  bool await_ready () const noexcept { return true; }
  void await_suspend (std::coroutine_handle<>) const noexcept {}
  T await_resume () { return result ().value (); }

  const result_t<T> &result () const
  {
    if (_handle) {
      return *_handle.promise ().result;
    }
    return *_result;
  }

private:
  explicit task_t (std::coroutine_handle<promise_type> handle)
    : _handle (handle)
  {
  }

  std::coroutine_handle<promise_type> _handle;
  std::optional<result_t<T>> _result;
};

template<>
class task_t<void>
{
public:
  struct promise_type
  {
    result_t<void> result = result_t<void>::success ();

    task_t get_return_object ()
    {
      return task_t (
        std::coroutine_handle<promise_type>::from_promise (*this));
    }
    std::suspend_never initial_suspend () noexcept { return {}; }
    std::suspend_always final_suspend () noexcept { return {}; }
    void unhandled_exception ()
    {
      result = result_t<void>::failure (
        error_code_t::user_callback_failed,
        "unhandled connector coroutine exception");
    }
    void return_void () noexcept {}
  };

  explicit task_t (result_t<void> result) : _result (std::move (result)) {}
  task_t (task_t &&other) noexcept
    : _handle (other._handle), _result (std::move (other._result))
  {
    other._handle = {};
  }
  task_t &operator= (task_t &&other) noexcept
  {
    if (this != &other) {
      if (_handle) {
        _handle.destroy ();
      }
      _handle = other._handle;
      _result = std::move (other._result);
      other._handle = {};
    }
    return *this;
  }
  task_t (const task_t &) = delete;
  task_t &operator= (const task_t &) = delete;
  ~task_t ()
  {
    if (_handle) {
      _handle.destroy ();
    }
  }

  bool await_ready () const noexcept { return true; }
  void await_suspend (std::coroutine_handle<>) const noexcept {}
  void await_resume () { result ().operator bool (); }

  const result_t<void> &result () const
  {
    if (_handle) {
      return _handle.promise ().result;
    }
    return *_result;
  }

private:
  explicit task_t (std::coroutine_handle<promise_type> handle)
    : _handle (handle)
  {
  }

  std::coroutine_handle<promise_type> _handle;
  std::optional<result_t<void>> _result;
};

struct metadata_t
{
  std::map<std::string, std::string> values;

  metadata_t &with (std::string key, std::string value)
  {
    values[std::move (key)] = std::move (value);
    return *this;
  }
};

struct heartbeat_options_t
{
  bool enabled = true;
  std::chrono::milliseconds interval { 1000 };
  std::chrono::milliseconds timeout { 5000 };
};

struct reconnect_options_t
{
  bool enabled = true;
  std::chrono::milliseconds initial_delay { 250 };
  std::chrono::milliseconds max_delay { 5000 };
  double backoff_factor = 2.0;
  std::optional<int> max_attempts = 3;
};

struct connector_options_t
{
  std::string endpoint;
  transport_t transport = transport_t::tcp;
  std::chrono::milliseconds connect_timeout { 5000 };
  std::chrono::milliseconds request_timeout { 30000 };
  heartbeat_options_t heartbeat;
  reconnect_options_t reconnect;
  std::size_t max_send_payload_size = 64 * 1024;
  std::size_t max_metadata_size = 8 * 1024;
  bool skip_server_certificate_validation = false;
  dispatch_mode_t dispatch_mode = dispatch_mode_t::manual;
  compression_t compression = compression_t::none;
};

struct packet_t
{
  std::string name;
  metadata_t metadata;
  codec_t codec = codec_t::raw;
  bool compressed = false;
  zlink::message_t payload;
};

namespace detail
{
result_t<zlink::message_t> submit_request (
  std::shared_ptr<connector_state_t> state,
  packet_t packet,
  std::chrono::milliseconds timeout);
} // namespace detail

struct connection_state_changed_t
{
  connection_state_t previous = connection_state_t::created;
  connection_state_t current = connection_state_t::created;
  std::optional<error_t> error;
};

class codec_registry_t
{
public:
  codec_registry_t ();
  ~codec_registry_t ();

  codec_registry_t (codec_registry_t &&) noexcept;
  codec_registry_t &operator= (codec_registry_t &&) noexcept;
  codec_registry_t (const codec_registry_t &) = default;
  codec_registry_t &operator= (const codec_registry_t &) = default;

  template<typename T>
  codec_registry_t &add_json ()
  {
    return add_erased (std::type_index (typeid (T)), codec_t::json);
  }

  template<typename T>
  codec_registry_t &add_message_pack ()
  {
    return add_erased (std::type_index (typeid (T)), codec_t::message_pack);
  }

  template<typename T>
  codec_registry_t &add_protobuf ()
  {
    return add_erased (std::type_index (typeid (T)), codec_t::protobuf);
  }

  bool supports (codec_t codec) const;

private:
  friend class connector_t;
  explicit codec_registry_t (std::shared_ptr<detail::connector_state_t> state);
  codec_registry_t &add_erased (std::type_index type, codec_t codec);

  std::shared_ptr<detail::connector_state_t> _state;
};

class send_call_t
{
public:
  send_call_t ();
  ~send_call_t ();

  send_call_t (send_call_t &&) noexcept;
  send_call_t &operator= (send_call_t &&) noexcept;
  send_call_t (const send_call_t &) = default;
  send_call_t &operator= (const send_call_t &) = default;

  send_call_t &packet_name (std::string name);
  send_call_t &metadata (std::string key, std::string value);
  send_call_t &metadata (metadata_t metadata);
  send_call_t &compress ();

  task_t<void> submit ();
  void submit (std::function<void (result_t<void>)> callback);

private:
  friend class connector_t;
  send_call_t (std::shared_ptr<detail::connector_state_t> state,
               packet_t packet);

  std::shared_ptr<detail::connector_state_t> _state;
  packet_t _packet;
};

template<typename TReply>
class request_call_t
{
public:
  request_call_t () = default;

  request_call_t &packet_name (std::string name)
  {
    _packet.name = std::move (name);
    return *this;
  }

  request_call_t &metadata (std::string key, std::string value)
  {
    _packet.metadata.with (std::move (key), std::move (value));
    return *this;
  }

  request_call_t &metadata (metadata_t metadata)
  {
    _packet.metadata = std::move (metadata);
    return *this;
  }

  request_call_t &timeout (std::chrono::milliseconds timeout)
  {
    _timeout = timeout;
    return *this;
  }

  request_call_t &compress ()
  {
    _packet.compressed = true;
    return *this;
  }

  task_t<TReply> submit ()
  {
    (void) _timeout;
    if (!_state) {
      return task_t<TReply> (result_t<TReply>::failure (
        error_code_t::configuration_error, "request call has no connector"));
    }
    return task_t<TReply> (submit_erased ().template as<TReply> ());
  }

  void submit (std::function<void (result_t<TReply>)> callback)
  {
    auto result = submit ().result ();
    if (callback) {
      callback (std::move (result));
    }
  }

private:
  friend class connector_t;

  class erased_result_t
  {
  public:
    explicit erased_result_t (result_t<zlink::message_t> result)
      : _result (std::move (result))
    {
    }

    template<typename T>
    result_t<T> as () const
    {
      if (!_result) {
        return result_t<T>::failure (
          _result.error_code (),
          _result.error () ? _result.error ()->message
                           : "request failed");
      }
      if constexpr (std::is_same_v<T, zlink::message_t>) {
        return result_t<T>::success (_result.value ());
      } else {
        return result_t<T>::success (T {});
      }
    }

  private:
    result_t<zlink::message_t> _result;
  };

  request_call_t (std::shared_ptr<detail::connector_state_t> state,
                  packet_t packet,
                  std::chrono::milliseconds default_timeout)
    : _state (std::move (state)),
      _packet (std::move (packet)),
      _timeout (default_timeout)
  {
  }

  erased_result_t submit_erased ()
  {
    return erased_result_t (
      detail::submit_request (_state, std::move (_packet), _timeout));
  }

  std::shared_ptr<detail::connector_state_t> _state;
  packet_t _packet;
  std::chrono::milliseconds _timeout { 0 };
};

class connector_t
{
public:
  connector_t ();
  ~connector_t ();

  connector_t (connector_t &&) noexcept;
  connector_t &operator= (connector_t &&) noexcept;
  connector_t (const connector_t &) = default;
  connector_t &operator= (const connector_t &) = default;

  bool is_connected () const;
  connection_state_t state () const;
  connector_options_t options () const;
  std::size_t pending_dispatch_count () const;
  codec_registry_t &codecs ();

  task_t<void> connect ();
  task_t<void> close ();
  task_t<void> dispatch ();

  connector_t &on_connection_state_changed (
    std::function<void (const connection_state_changed_t &)> handler);
  connector_t &on_error (std::function<void (const error_t &)> handler);
  connector_t &on_disconnected (std::function<void ()> handler);

  template<typename TMessage>
  send_call_t send (const TMessage &message)
  {
    (void) message;
    return send_call_t (_state, make_packet (std::type_index (typeid (TMessage))));
  }

  template<typename TReply, typename TRequest>
  request_call_t<TReply> request (const TRequest &request)
  {
    (void) request;
    return request_call_t<TReply> (
      _state,
      make_packet (std::type_index (typeid (TRequest))),
      options ().request_timeout);
  }

  template<typename TMessage>
  connector_t &on (std::string packet_name,
                   std::function<void (const TMessage &)> callback)
  {
    return on_packet_erased (
      std::move (packet_name),
      [callback = std::move (callback)](const packet_t &packet) {
        if constexpr (std::is_same_v<TMessage, packet_t>) {
          callback (packet);
        } else {
          callback (TMessage {});
        }
      });
  }

private:
  friend class connector_factory_t;
  friend class detail::connector_runtime_t;

  explicit connector_t (connector_options_t options);
  packet_t make_packet (std::type_index type) const;
  connector_t &on_packet_erased (
    std::string packet_name,
    std::function<void (const packet_t &)> handler);

  std::shared_ptr<detail::connector_state_t> _state;
  codec_registry_t _codecs;
};

class connector_factory_t
{
public:
  static connector_t create (connector_options_t options);
};

} // namespace zlink::stream_connector
