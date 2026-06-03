/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector.hpp>
#include <zlink/stream_connector/codecs/auto_codec.hpp>

#include <chrono>
#include <string>
#include <utility>

namespace zlink::samples
{

inline stream_connector::connector_options_t
make_immediate_connector_options (
  std::string endpoint,
  std::chrono::milliseconds connect_timeout,
  std::chrono::milliseconds request_timeout)
{
  stream_connector::connector_options_t options;
  options.endpoint = std::move (endpoint);
  options.connect_timeout = connect_timeout;
  options.request_timeout = request_timeout;
  options.dispatch_mode = stream_connector::dispatch_mode_t::immediate;
  return options;
}

template<typename TCallResult, typename TValue>
TCallResult
make_client_call_result (
  std::string packet_name,
  const stream_connector::result_t<TValue> &result,
  const char *fallback_error)
{
  return {
    std::move (packet_name),
    static_cast<bool> (result),
    result ? std::string {}
           : result.error () ? result.error ()->message
                             : std::string (fallback_error)
  };
}

inline bool
connect_client_connector (stream_connector::connector_t &connector)
{
  return static_cast<bool> (connector.connect ().result ());
}

inline void
close_client_connector (stream_connector::connector_t &connector)
{
  (void) connector.close ().result ();
}

inline void
dispatch_client_connector (stream_connector::connector_t &connector)
{
  (void) connector.dispatch ().result ();
}

template<typename TCallResult, typename TReply, typename TRequest>
TCallResult
request_client_packet (
  stream_connector::connector_t &connector,
  const TRequest &request,
  const char *fallback_error = "request failed")
{
  auto result =
    stream_connector::codecs::request<TReply> (connector, request)
      .submit ()
      .result ();
  return make_client_call_result<TCallResult> (
    TRequest::packet_name,
    result,
    fallback_error);
}

template<typename TCallResult, typename TRequest>
TCallResult
send_client_packet (
  stream_connector::connector_t &connector,
  TRequest request,
  const char *fallback_error = "send failed")
{
  auto result =
    stream_connector::codecs::send (connector, std::move (request))
      .submit ()
      .result ();
  return make_client_call_result<TCallResult> (
    TRequest::packet_name,
    result,
    fallback_error);
}

} // namespace zlink::samples
