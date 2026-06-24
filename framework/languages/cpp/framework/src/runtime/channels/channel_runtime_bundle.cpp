/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_runtime_bundle.hpp"

#include <utility>

namespace zlink::framework::detail
{

bool channel_runtime_bundle_t::try_add_manual_connection (std::string endpoint)
{
    std::lock_guard lock (_mutex);
    return _manual_connections.insert (std::move (endpoint)).second;
}

void channel_runtime_bundle_t::remove_manual_connection (const std::string &endpoint)
{
    std::lock_guard lock (_mutex);
    _manual_connections.erase (endpoint);
    if (_next_manual_connection >= _manual_connections.size ()) {
        _next_manual_connection = 0;
    }
}

bool channel_runtime_bundle_t::contains_manual_connection (const std::string &endpoint) const
{
    std::lock_guard lock (_mutex);
    return _manual_connections.find (endpoint) != _manual_connections.end ();
}

std::vector<std::string> channel_runtime_bundle_t::list_manual_connections () const
{
    std::lock_guard lock (_mutex);
    return std::vector<std::string> (_manual_connections.begin (), _manual_connections.end ());
}

std::optional<std::string> channel_runtime_bundle_t::next_manual_connection ()
{
    std::lock_guard lock (_mutex);
    if (_manual_connections.empty ()) {
        return std::nullopt;
    }
    auto endpoints =
      std::vector<std::string> (_manual_connections.begin (), _manual_connections.end ());
    if (_next_manual_connection >= endpoints.size ()) {
        _next_manual_connection = 0;
    }
    auto endpoint = endpoints[_next_manual_connection];
    _next_manual_connection = (_next_manual_connection + 1) % endpoints.size ();
    return endpoint;
}

std::vector<std::string> channel_runtime_bundle_t::manual_connections_from_next ()
{
    std::lock_guard lock (_mutex);
    std::vector<std::string> endpoints (_manual_connections.begin (), _manual_connections.end ());
    if (endpoints.empty ()) {
        return endpoints;
    }
    if (_next_manual_connection >= endpoints.size ()) {
        _next_manual_connection = 0;
    }
    std::vector<std::string> ordered;
    ordered.reserve (endpoints.size ());
    for (std::size_t offset = 0; offset < endpoints.size (); ++offset) {
        ordered.push_back (endpoints[(_next_manual_connection + offset) % endpoints.size ()]);
    }
    _next_manual_connection = (_next_manual_connection + 1) % endpoints.size ();
    return ordered;
}

bool channel_runtime_bundle_t::try_enter_receive () noexcept
{
    bool expected = false;
    return _receive_active.compare_exchange_strong (expected, true);
}

void channel_runtime_bundle_t::leave_receive () noexcept
{
    _receive_active.store (false);
}

bool channel_runtime_bundle_t::receive_active () const noexcept
{
    return _receive_active.load ();
}

} // namespace zlink::framework::detail
