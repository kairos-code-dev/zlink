/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/transport/transport_connection.hpp"

#include <boost/asio/io_context.hpp>

#include <memory>
#include <optional>
#include <string>

namespace zlink::stream_connector::detail
{

struct websocket_endpoint_parts_t
{
  std::string host;
  std::string port;
  std::string target;
};

std::optional<websocket_endpoint_parts_t> parse_websocket_endpoint (
  const std::string &endpoint);
std::unique_ptr<stream_connection_t> connect_websocket (
  boost::asio::io_context &io_context,
  const websocket_endpoint_parts_t &endpoint);

} // namespace zlink::stream_connector::detail
