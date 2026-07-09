/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_runtime_bundle.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace zlink::framework::detail
{

bool channel_runtime_bundle_t::try_add_manual_connection (std::string endpoint)
{
    std::lock_guard lock (_mutex);
    const bool inserted = _manual_connections.insert (std::move (endpoint)).second;
    if (inserted) {
        ++_connection_version;
    }
    return inserted;
}

void channel_runtime_bundle_t::remove_manual_connection (const std::string &endpoint)
{
    std::lock_guard lock (_mutex);
    if (_manual_connections.erase (endpoint) != 0) {
        ++_connection_version;
    }
    if (_next_manual_connection >= _manual_connections.size ()) {
        _next_manual_connection = 0;
    }
    if (_next_connection >= _manual_connections.size () + _auto_connections.size ()) {
        _next_connection = 0;
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

bool channel_runtime_bundle_t::try_add_auto_connection (std::string endpoint, std::uint32_t weight)
{
    std::lock_guard lock (_mutex);
    if (_manual_connections.find (endpoint) != _manual_connections.end ()) {
        return false;
    }
    auto [it, inserted] = _auto_connections.insert ({std::move (endpoint), weight});
    if (inserted) {
        ++_connection_version;
        return true;
    }
    if (it->second == weight) {
        return false;
    }
    it->second = weight;
    ++_connection_version;
    return true;
}

void channel_runtime_bundle_t::remove_auto_connection (const std::string &endpoint)
{
    std::lock_guard lock (_mutex);
    if (_auto_connections.erase (endpoint) != 0) {
        ++_connection_version;
    }
    if (_next_connection >= _manual_connections.size () + _auto_connections.size ()) {
        _next_connection = 0;
    }
}

bool channel_runtime_bundle_t::contains_auto_connection (const std::string &endpoint) const
{
    std::lock_guard lock (_mutex);
    return _auto_connections.find (endpoint) != _auto_connections.end ();
}

std::vector<std::string> channel_runtime_bundle_t::list_auto_connections () const
{
    std::lock_guard lock (_mutex);
    std::vector<std::string> endpoints;
    endpoints.reserve (_auto_connections.size ());
    for (const auto &[endpoint, _] : _auto_connections) {
        endpoints.push_back (endpoint);
    }
    return endpoints;
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

std::vector<std::string> channel_runtime_bundle_t::connections_from_next ()
{
    std::lock_guard lock (_mutex);
    std::vector<std::pair<std::string, std::uint32_t>> weighted_connections;
    std::uint32_t max_weight = 0;
    for (const auto &endpoint : _manual_connections) {
        weighted_connections.push_back ({endpoint, 100});
        max_weight = std::max (max_weight, 100u);
    }
    for (const auto &[endpoint, weight] : _auto_connections) {
        if (_manual_connections.find (endpoint) != _manual_connections.end () || weight == 0) {
            continue;
        }
        weighted_connections.push_back ({endpoint, weight});
        max_weight = std::max (max_weight, weight);
    }
    std::vector<std::string> weighted_slots;
    for (std::uint32_t slot = 0; slot < max_weight; ++slot) {
        for (const auto &[endpoint, weight] : weighted_connections) {
            if (slot < weight) {
                weighted_slots.push_back (endpoint);
            }
        }
    }
    if (weighted_slots.empty ()) {
        _next_connection = 0;
        return {};
    }
    if (_next_connection >= weighted_slots.size ()) {
        _next_connection = 0;
    }
    std::vector<std::string> ordered;
    ordered.reserve (_manual_connections.size () + _auto_connections.size ());
    std::set<std::string> seen;
    for (std::size_t offset = 0; offset < weighted_slots.size (); ++offset) {
        const auto &endpoint = weighted_slots[(_next_connection + offset) % weighted_slots.size ()];
        if (seen.insert (endpoint).second) {
            ordered.push_back (endpoint);
        }
    }
    _next_connection = (_next_connection + 1) % weighted_slots.size ();
    return ordered;
}

std::uint64_t channel_runtime_bundle_t::connection_version () const
{
    std::lock_guard lock (_mutex);
    return _connection_version;
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
