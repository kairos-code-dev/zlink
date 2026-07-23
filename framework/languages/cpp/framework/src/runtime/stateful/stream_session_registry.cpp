/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/stateful/stream_session_registry.hpp"

#include <stdexcept>
#include <utility>

namespace zlink::framework::runtime::stateful
{

stream_session_registry_t::stream_session_registry_t (
  authority_resolver_t resolver) :
    _resolver (std::move (resolver))
{
    if (!_resolver) {
        throw std::invalid_argument (
          "stream session authority resolver is empty");
    }
}

stream_connection_t stream_session_registry_t::open (
  std::string connection_id)
{
    if (connection_id.empty ()) {
        throw std::invalid_argument ("stream connection id is empty");
    }
    std::lock_guard lock (_mutex);
    const auto generation = ++_last_connection_generation[connection_id];
    stream_connection_t connection{std::move (connection_id), generation};
    _connections[connection.connection_id] =
      connection_state_t{connection, 1, 1, std::nullopt};
    return connection;
}

bool stream_session_registry_t::close (
  const stream_connection_t &connection)
{
    std::lock_guard lock (_mutex);
    const auto current = _connections.find (connection.connection_id);
    if (current == _connections.end ()
        || current->second.connection != connection) {
        return false;
    }
    _connections.erase (current);
    return true;
}

std::pair<stateful_error_t, stream_binding_t>
stream_session_registry_t::bind (
  const stream_connection_t &connection,
  const object_ref_t &actor)
{
    const auto authority = _resolver (actor.key);
    if (!authority || !exact_actor (*authority, actor)) {
        return {authority ? stateful_error_t::generation_stale
                          : stateful_error_t::not_found,
                {}};
    }

    std::lock_guard lock (_mutex);
    const auto current = _connections.find (connection.connection_id);
    if (current == _connections.end ()
        || current->second.connection != connection) {
        return {stateful_error_t::conflict, {}};
    }
    auto &state = current->second;
    stream_binding_t binding{
      connection, state.next_binding_generation++, actor};
    state.binding = binding;
    return {stateful_error_t::none, binding};
}

stateful_error_t stream_session_registry_t::unbind (
  const stream_binding_t &binding)
{
    std::lock_guard lock (_mutex);
    const auto current =
      _connections.find (binding.connection.connection_id);
    if (current == _connections.end ()
        || current->second.connection != binding.connection
        || !current->second.binding
        || *current->second.binding != binding) {
        return stateful_error_t::conflict;
    }
    current->second.binding.reset ();
    return stateful_error_t::none;
}

std::pair<stateful_error_t, std::optional<stream_dispatch_t>>
stream_session_registry_t::admit_inbound (
  const stream_binding_t &binding)
{
    const auto authority = _resolver (binding.actor.key);
    if (!authority || !exact_actor (*authority, binding.actor)) {
        return {authority ? stateful_error_t::generation_stale
                          : stateful_error_t::not_found,
                std::nullopt};
    }

    std::lock_guard lock (_mutex);
    const auto current =
      _connections.find (binding.connection.connection_id);
    if (current == _connections.end ()
        || current->second.connection != binding.connection
        || !current->second.binding
        || *current->second.binding != binding) {
        return {stateful_error_t::conflict, std::nullopt};
    }
    return {stateful_error_t::none,
            stream_dispatch_t{
              binding, current->second.next_inbound_sequence++}};
}

bool stream_session_registry_t::is_current (
  const stream_binding_t &binding) const
{
    std::lock_guard lock (_mutex);
    const auto current =
      _connections.find (binding.connection.connection_id);
    return current != _connections.end ()
           && current->second.connection == binding.connection
           && current->second.binding
           && *current->second.binding == binding;
}

bool stream_session_registry_t::exact_actor (
  const object_ref_t &left,
  const object_ref_t &right)
{
    return left.kind == object_kind_t::actor
           && right.kind == object_kind_t::actor
           && left == right;
}

} // namespace zlink::framework::runtime::stateful
