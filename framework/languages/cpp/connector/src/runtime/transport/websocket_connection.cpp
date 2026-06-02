/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/transport/websocket_connection.hpp"

#include <utility>

namespace zlink::stream_connector::detail
{

websocket_connection_t::websocket_connection_t (std::string endpoint)
  : _endpoint (std::move (endpoint))
{
}

const std::string &
websocket_connection_t::endpoint () const noexcept
{
  return _endpoint;
}

} // namespace zlink::stream_connector::detail
