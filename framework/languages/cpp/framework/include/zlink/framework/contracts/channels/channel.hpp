/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/call.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace zlink::framework
{

namespace detail
{
class capability_builder_state_t;
class channel_builder_state_t;
class channel_runtime_state_t;
class channel_runtime_t;
} // namespace detail

enum class channel_capability_t
{
  server,
  client,
  publisher,
  subscriber
};

struct channel_capability_snapshot_t
{
  bool enabled = false;
  bool discovery = false;
  std::vector<std::string> bind_endpoints;
  std::vector<std::string> connect_endpoints;
};

struct channel_snapshot_t
{
  std::string name;
  channel_capability_snapshot_t server;
  channel_capability_snapshot_t client;
  channel_capability_snapshot_t publisher;
  channel_capability_snapshot_t subscriber;
};

struct channel_reliability_event_t
{
  std::string channel_name;
  std::string idempotency_key;
  framework_error_kind_t error_kind;
  std::string message;
};

class capability_builder_t
{
public:
  capability_builder_t ();
  ~capability_builder_t ();

  capability_builder_t (capability_builder_t &&) noexcept;
  capability_builder_t &operator= (capability_builder_t &&) noexcept;
  capability_builder_t (const capability_builder_t &) = default;
  capability_builder_t &operator= (const capability_builder_t &) = default;

  capability_builder_t &bind (std::string endpoint);
  capability_builder_t &connect (std::string endpoint);
  capability_builder_t &use_discovery ();

  channel_capability_snapshot_t snapshot () const;

private:
  friend class channel_builder_t;
  explicit capability_builder_t (
    std::shared_ptr<detail::capability_builder_state_t> state);

  std::shared_ptr<detail::capability_builder_state_t> _state;
};

class channel_builder_t
{
public:
  channel_builder_t ();
  ~channel_builder_t ();

  channel_builder_t (channel_builder_t &&) noexcept;
  channel_builder_t &operator= (channel_builder_t &&) noexcept;
  channel_builder_t (const channel_builder_t &) = default;
  channel_builder_t &operator= (const channel_builder_t &) = default;

  channel_builder_t &enable_server (
    std::function<void (capability_builder_t &)> configure = {});
  channel_builder_t &enable_client (
    std::function<void (capability_builder_t &)> configure = {});
  channel_builder_t &enable_publisher (
    std::function<void (capability_builder_t &)> configure = {});
  channel_builder_t &enable_subscriber (
    std::function<void (capability_builder_t &)> configure = {});

  channel_snapshot_t snapshot () const;

private:
  friend class zlink_builder_t;
  explicit channel_builder_t (
    std::shared_ptr<detail::channel_builder_state_t> state);
  void enable_capability (
    channel_capability_snapshot_t &target,
    const std::function<void (capability_builder_t &)> &configure);

  std::shared_ptr<detail::channel_builder_state_t> _state;
};

class message_bus_t
{
public:
  message_bus_t ();
  ~message_bus_t ();

  message_bus_t (message_bus_t &&) noexcept;
  message_bus_t &operator= (message_bus_t &&) noexcept;
  message_bus_t (const message_bus_t &) = default;
  message_bus_t &operator= (const message_bus_t &) = default;

  template<typename TRequest, typename TReply>
  request_call_t<TReply> request (std::string channel_name,
                                  TRequest request)
  {
    (void) request;
    return request_call_t<TReply> (
      submit_request (std::move (channel_name)).template as<TReply> ());
  }

  template<typename TMessage>
  send_call_t send (std::string channel_name, TMessage message)
  {
    (void) message;
    return send_call_t (submit_send (std::move (channel_name)));
  }

  template<typename TEvent>
  send_call_t publish (std::string channel_name,
                       std::string topic,
                       TEvent event)
  {
    (void) event;
    return send_call_t (
      submit_publish (std::move (channel_name), std::move (topic)));
  }

  std::size_t pending_count () const noexcept;
  std::size_t pending_limit () const noexcept;

private:
  friend class zlink_builder_t;
  friend class request_client_t;
  friend class publisher_t;
  friend class detail::channel_runtime_t;

  class erased_request_result_t
  {
  public:
    explicit erased_request_result_t (framework_exception_t error);

    template<typename TReply>
    result_t<TReply> as () const
    {
      return result_t<TReply>::failure (
        _error.kind (), _error.what (), _error.is_retriable ());
    }

  private:
    framework_exception_t _error;
  };

  explicit message_bus_t (
    std::shared_ptr<detail::channel_runtime_state_t> state);

  erased_request_result_t submit_request (std::string channel_name);
  result_t<void> submit_send (std::string channel_name);
  result_t<void> submit_publish (std::string channel_name, std::string topic);

  std::shared_ptr<detail::channel_runtime_state_t> _state;
};

class request_client_t
{
public:
  request_client_t (message_bus_t bus, std::string channel_name);

  template<typename TRequest, typename TReply>
  request_call_t<TReply> request (TRequest request)
  {
    return _bus.request<TRequest, TReply> (_channel_name, std::move (request));
  }

private:
  message_bus_t _bus;
  std::string _channel_name;
};

class publisher_t
{
public:
  explicit publisher_t (message_bus_t bus);

  template<typename TEvent>
  send_call_t publish (std::string channel_name,
                       std::string topic,
                       TEvent event)
  {
    return _bus.publish (
      std::move (channel_name), std::move (topic), std::move (event));
  }

private:
  message_bus_t _bus;
};

using retry_hook_t = std::function<void (const channel_reliability_event_t &)>;
using dead_letter_hook_t =
  std::function<void (const channel_reliability_event_t &)>;

} // namespace zlink::framework
