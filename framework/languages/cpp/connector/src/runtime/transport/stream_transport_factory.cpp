/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/transport/stream_transport_factory.hpp"

namespace zlink::stream_connector::detail
{

bool
stream_transport_factory_t::is_supported (transport_t transport) noexcept
{
  switch (transport) {
  case transport_t::tcp:
  case transport_t::websocket:
    return true;
  case transport_t::tls:
  case transport_t::websocket_secure:
    return false;
  }
  return false;
}

} // namespace zlink::stream_connector::detail
