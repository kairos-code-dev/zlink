/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/transport/websocket_connection.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace zlink::stream_connector::detail
{

namespace
{

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

class websocket_stream_connection_t final : public stream_connection_t
{
public:
  explicit websocket_stream_connection_t (websocket::stream<tcp::socket> stream)
    : _stream (std::move (stream))
  {
    _stream.binary (true);
  }

  bool is_open () const override { return _stream.is_open (); }

  std::size_t available (boost::system::error_code &error) override
  {
    if (!_read_buffer.empty ()) {
      return _read_buffer.size () - _read_offset;
    }

    const auto tcp_available = _stream.next_layer ().available (error);
    if (error || tcp_available == 0) {
      return 0;
    }

    beast::flat_buffer buffer;
    _stream.read (buffer, error);
    if (error) {
      return 0;
    }

    auto text = beast::buffers_to_string (buffer.data ());
    _read_buffer.assign (text.begin (), text.end ());
    _read_offset = 0;
    return _read_buffer.size ();
  }

  std::size_t read_some (std::uint8_t *buffer,
                         std::size_t size,
                         boost::system::error_code &error) override
  {
    if (available (error) == 0 || error) {
      return 0;
    }

    const auto remaining = _read_buffer.size () - _read_offset;
    const auto copied = std::min (remaining, size);
    std::copy_n (_read_buffer.data () + _read_offset, copied, buffer);
    _read_offset += copied;
    if (_read_offset == _read_buffer.size ()) {
      _read_buffer.clear ();
      _read_offset = 0;
    }
    return copied;
  }

  void write (const std::vector<std::uint8_t> &bytes) override
  {
    _stream.binary (true);
    _stream.write (asio::buffer (bytes));
  }

  void shutdown_and_close () override
  {
    boost::system::error_code ignored;
    _stream.close (websocket::close_code::normal, ignored);
  }

  void close (boost::system::error_code &error) override
  {
    _stream.next_layer ().close (error);
  }

private:
  websocket::stream<tcp::socket> _stream;
  std::vector<std::uint8_t> _read_buffer;
  std::size_t _read_offset = 0;
};

} // namespace

std::optional<websocket_endpoint_parts_t>
parse_websocket_endpoint (const std::string &endpoint)
{
  constexpr std::string_view prefix = "ws://";
  if (endpoint.rfind (std::string (prefix), 0) != 0) {
    return std::nullopt;
  }

  const auto host_start = prefix.size ();
  const auto path_start = endpoint.find ('/', host_start);
  const auto authority =
    endpoint.substr (
      host_start,
      path_start == std::string::npos ? std::string::npos
                                      : path_start - host_start);
  const auto colon = authority.rfind (':');
  if (colon == std::string::npos || colon == 0 ||
      colon + 1 >= authority.size ()) {
    return std::nullopt;
  }

  return websocket_endpoint_parts_t {
    authority.substr (0, colon),
    authority.substr (colon + 1),
    path_start == std::string::npos ? std::string ("/")
                                    : endpoint.substr (path_start) };
}

std::unique_ptr<stream_connection_t>
connect_websocket (boost::asio::io_context &io_context,
                   const websocket_endpoint_parts_t &endpoint)
{
  tcp::resolver resolver (io_context);
  auto endpoints = resolver.resolve (endpoint.host, endpoint.port);
  websocket::stream<tcp::socket> stream (io_context);
  asio::connect (stream.next_layer (), endpoints);
  stream.binary (true);
  stream.handshake (endpoint.host, endpoint.target);
  return std::make_unique<websocket_stream_connection_t> (std::move (stream));
}

} // namespace zlink::stream_connector::detail
