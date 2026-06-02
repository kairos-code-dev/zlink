/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/route_connection_set.hpp"

#include <utility>

namespace zlink::framework::detail
{

bool
route_connection_set_t::connect (std::string endpoint)
{
  return _manual_connections.insert (std::move (endpoint)).second;
}

bool
route_connection_set_t::disconnect (const std::string &endpoint)
{
  return _manual_connections.erase (endpoint) != 0;
}

bool
route_connection_set_t::contains (const std::string &endpoint) const
{
  return _manual_connections.find (endpoint) != _manual_connections.end ();
}

std::vector<std::string>
route_connection_set_t::list () const
{
  return std::vector<std::string> (_manual_connections.begin (),
                                   _manual_connections.end ());
}

} // namespace zlink::framework::detail
