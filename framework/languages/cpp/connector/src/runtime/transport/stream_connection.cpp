/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/transport/stream_connection.hpp"

namespace zlink::stream_connector::detail
{

namespace
{

class tcp_stream_connection_t final : public stream_connection_t
{
public:
  explicit tcp_stream_connection_t (boost::asio::ip::tcp::socket socket)
    : _socket (std::move (socket))
  {
  }

  bool is_open () const override { return _socket.is_open (); }

  std::size_t available (boost::system::error_code &error) override
  {
    return _socket.available (error);
  }

  std::size_t read_some (std::uint8_t *buffer,
                         std::size_t size,
                         boost::system::error_code &error) override
  {
    return _socket.read_some (boost::asio::buffer (buffer, size), error);
  }

  void write (const std::vector<std::uint8_t> &bytes) override
  {
    boost::asio::write (_socket, boost::asio::buffer (bytes));
  }

  void shutdown_and_close () override
  {
    boost::system::error_code ignored;
    _socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both, ignored);
    _socket.close (ignored);
  }

  void close (boost::system::error_code &error) override
  {
    _socket.close (error);
  }

private:
  boost::asio::ip::tcp::socket _socket;
};

} // namespace

std::unique_ptr<stream_connection_t>
make_tcp_connection (boost::asio::ip::tcp::socket socket)
{
  return std::make_unique<tcp_stream_connection_t> (std::move (socket));
}

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
  return state.state == connection_state_t::connected && state.connection &&
         state.connection->is_open ();
}

std::vector<std::uint8_t>
read_exact (connector_state_t &state, std::size_t size)
{
  std::vector<std::uint8_t> bytes (size);
  std::size_t offset = 0;
  while (offset < size) {
    boost::system::error_code error;
    const auto read = state.connection->read_some (
      bytes.data () + offset, bytes.size () - offset, error);
    if (error) {
      throw boost::system::system_error (error);
    }
    offset += read;
  }
  return bytes;
}

void
write_bytes (connector_state_t &state,
             const std::vector<std::uint8_t> &bytes)
{
  state.connection->write (bytes);
}

} // namespace zlink::stream_connector::detail
