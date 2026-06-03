/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector.hpp>

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

} // namespace zlink::samples
