/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/connector_runtime.hpp"
#include "runtime/transport/transport_connection.hpp"

#include <boost/asio/ip/tcp.hpp>

#include <optional>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zlink::stream_connector::detail
{

struct endpoint_parts_t
{
  std::string host;
  std::string port;
};

std::unique_ptr<stream_connection_t> make_tcp_connection (
  boost::asio::ip::tcp::socket socket);
std::optional<endpoint_parts_t> parse_tcp_endpoint (const std::string &endpoint);
bool is_transport_connected (const connector_state_t &state);
std::vector<std::uint8_t> read_exact (connector_state_t &state,
                                      std::size_t size);
void write_bytes (connector_state_t &state,
                  const std::vector<std::uint8_t> &bytes);

} // namespace zlink::stream_connector::detail
