/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_runtime_bundle.hpp"

#include <utility>

namespace zlink::framework::detail
{

bool
channel_runtime_bundle_t::try_add_manual_connection (std::string endpoint)
{
  return _manual_connections.insert (std::move (endpoint)).second;
}

void
channel_runtime_bundle_t::remove_manual_connection (
  const std::string &endpoint)
{
  _manual_connections.erase (endpoint);
}

bool
channel_runtime_bundle_t::contains_manual_connection (
  const std::string &endpoint) const
{
  return _manual_connections.find (endpoint) != _manual_connections.end ();
}

std::vector<std::string>
channel_runtime_bundle_t::list_manual_connections () const
{
  return std::vector<std::string> (_manual_connections.begin (),
                                   _manual_connections.end ());
}

bool
channel_runtime_bundle_t::try_enter_receive () noexcept
{
  if (_receive_active) {
    return false;
  }
  _receive_active = true;
  return true;
}

void
channel_runtime_bundle_t::leave_receive () noexcept
{
  _receive_active = false;
}

bool
channel_runtime_bundle_t::receive_active () const noexcept
{
  return _receive_active;
}

channel_pending_requests_t &
channel_runtime_bundle_t::dealer_mesh_pending_requests () noexcept
{
  return _dealer_mesh_pending_requests;
}

const channel_pending_requests_t &
channel_runtime_bundle_t::dealer_mesh_pending_requests () const noexcept
{
  return _dealer_mesh_pending_requests;
}

} // namespace zlink::framework::detail
