/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/channels/channel_runtime_bundle.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
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

bool channel_runtime_bundle_t::try_add_auto_connection (std::string endpoint, int weight)
{
    if (weight < 0 || weight > 10000) {
        throw std::invalid_argument (
          "connection weight must be in range 0..10000");
    }
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
    std::vector<std::pair<std::string, int>> weighted_connections;
    for (const auto &endpoint : _manual_connections) {
        weighted_connections.push_back ({endpoint, 100});
    }
    for (const auto &[endpoint, weight] : _auto_connections) {
        if (_manual_connections.find (endpoint) != _manual_connections.end () || weight == 0) {
            continue;
        }
        weighted_connections.push_back ({endpoint, weight});
    }
    std::uint64_t total_weight = 0;
    for (const auto &[_, weight] : weighted_connections)
        total_weight += static_cast<std::uint64_t> (weight);
    if (total_weight == 0) {
        _next_connection = 0;
        return {};
    }
    const auto selected_slot =
      _next_connection++ % total_weight;
    std::size_t selected_index = 0;
    auto remaining = selected_slot;
    for (std::size_t index = 0;
         index < weighted_connections.size (); ++index) {
        const auto weight = static_cast<std::uint64_t> (
          weighted_connections[index].second);
        if (remaining < weight) {
            selected_index = index;
            break;
        }
        remaining -= weight;
    }
    std::vector<std::string> ordered;
    ordered.reserve (weighted_connections.size ());
    for (std::size_t offset = 0;
         offset < weighted_connections.size (); ++offset) {
        ordered.push_back (
          weighted_connections[
            (selected_index + offset)
            % weighted_connections.size ()]
            .first);
    }
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
