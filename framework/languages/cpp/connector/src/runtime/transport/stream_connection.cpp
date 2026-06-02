/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/transport/stream_connection.hpp"

#include <istream>

namespace zlink::stream_connector::detail
{

std::optional<endpoint_parts_t>
parse_tcp_endpoint (const std::string &endpoint)
{
  constexpr std::string_view prefix = "tcp://";
  if (endpoint.rfind (std::string (prefix), 0) != 0) {
    return std::nullopt;
  }
  const auto host_start = prefix.size ();
  const auto colon = endpoint.rfind (':');
  if (colon == std::string::npos || colon <= host_start ||
      colon + 1 >= endpoint.size ()) {
    return std::nullopt;
  }
  return endpoint_parts_t {
    endpoint.substr (host_start, colon - host_start),
    endpoint.substr (colon + 1) };
}

bool
is_transport_connected (const connector_state_t &state)
{
  return state.state == connection_state_t::connected && state.socket &&
         state.socket->is_open ();
}

std::string
read_line (connector_state_t &state)
{
  boost::asio::read_until (*state.socket, state.inbound_buffer, '\n');
  std::istream input (&state.inbound_buffer);
  std::string line;
  std::getline (input, line);
  if (!line.empty () && line.back () == '\r') {
    line.pop_back ();
  }
  return line;
}

void
write_line (connector_state_t &state, const std::string &line)
{
  boost::asio::write (*state.socket, boost::asio::buffer (line));
}

std::vector<std::uint8_t>
read_exact (connector_state_t &state, std::size_t size)
{
  std::vector<std::uint8_t> bytes (size);
  boost::asio::read (*state.socket, boost::asio::buffer (bytes));
  return bytes;
}

void
write_bytes (connector_state_t &state,
             const std::vector<std::uint8_t> &bytes)
{
  boost::asio::write (*state.socket, boost::asio::buffer (bytes));
}

} // namespace zlink::stream_connector::detail
