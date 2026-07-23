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
    if (_all_sealed)
        return {stateful_error_t::moving, {}};
    const auto current = _connections.find (connection.connection_id);
    if (current == _connections.end ()
        || current->second.connection != connection) {
        return {stateful_error_t::conflict, {}};
    }
    auto &state = current->second;
    if (state.barrier_token)
        return {stateful_error_t::moving, {}};
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
    if (_all_sealed)
        return {stateful_error_t::moving, std::nullopt};
    const auto current =
      _connections.find (binding.connection.connection_id);
    if (current == _connections.end ()
        || current->second.connection != binding.connection
        || !current->second.binding
        || *current->second.binding != binding) {
        return {stateful_error_t::conflict, std::nullopt};
    }
    if (current->second.barrier_token)
        return {stateful_error_t::moving, std::nullopt};
    const auto sequence = current->second.next_inbound_sequence++;
    current->second.active_inbound.insert (sequence);
    return {stateful_error_t::none,
            stream_dispatch_t{
              binding, sequence}};
}

stateful_error_t stream_session_registry_t::complete_inbound (
  const stream_dispatch_t &dispatch)
{
    std::lock_guard lock (_mutex);
    const auto current =
      _connections.find (dispatch.binding.connection.connection_id);
    if (current == _connections.end ()
        || current->second.connection != dispatch.binding.connection)
        return stateful_error_t::conflict;
    return current->second.active_inbound.erase (
             dispatch.inbound_sequence)
             == 1
           ? stateful_error_t::none
           : stateful_error_t::not_found;
}

std::pair<stateful_error_t, stream_barrier_t>
stream_session_registry_t::try_seal_actor (const object_ref_t &actor)
{
    if (actor.kind != object_kind_t::actor)
        return {stateful_error_t::invalid, {}};
    std::lock_guard lock (_mutex);
    std::vector<connection_state_t *> affected;
    for (auto &[_, state] : _connections) {
        if (!state.binding || !exact_actor (state.binding->actor, actor))
            continue;
        if (state.barrier_token)
            return {stateful_error_t::moving, {}};
        if (!state.active_inbound.empty ())
            return {stateful_error_t::backpressured, {}};
        affected.push_back (&state);
    }
    if (_next_barrier_token == 0)
        return {stateful_error_t::conflict, {}};
    const auto token = _next_barrier_token++;
    for (auto *state : affected)
        state->barrier_token = token;
    _barriers.emplace (token, actor);
    return {
      stateful_error_t::none, stream_barrier_t{token, actor}};
}

stateful_error_t stream_session_registry_t::abort_barrier (
  const stream_barrier_t &barrier)
{
    std::lock_guard lock (_mutex);
    const auto found = _barriers.find (barrier.token);
    if (found == _barriers.end () || !exact_actor (found->second, barrier.actor))
        return stateful_error_t::not_found;
    for (auto &[_, state] : _connections) {
        if (state.barrier_token == barrier.token)
            state.barrier_token.reset ();
    }
    _barriers.erase (found);
    return stateful_error_t::none;
}

stateful_error_t stream_session_registry_t::commit_barrier (
  const stream_barrier_t &barrier,
  const object_ref_t &target)
{
    std::lock_guard lock (_mutex);
    const auto found = _barriers.find (barrier.token);
    if (found == _barriers.end () || !exact_actor (found->second, barrier.actor)
        || target.kind != object_kind_t::actor
        || target.key != barrier.actor.key
        || target.object_generation != barrier.actor.object_generation
        || target.authority_owner_generation
             <= barrier.actor.authority_owner_generation)
        return stateful_error_t::conflict;
    for (auto &[_, state] : _connections) {
        if (state.barrier_token != barrier.token)
            continue;
        auto next = *state.binding;
        next.actor = target;
        next.binding_generation = state.next_binding_generation++;
        state.binding = std::move (next);
        state.barrier_token.reset ();
    }
    _barriers.erase (found);
    return stateful_error_t::none;
}

bool stream_session_registry_t::try_seal_all ()
{
    std::lock_guard lock (_mutex);
    if (_all_sealed)
        return true;
    for (const auto &[_, state] : _connections) {
        if (!state.active_inbound.empty ())
            return false;
    }
    _all_sealed = true;
    return true;
}

void stream_session_registry_t::release_all () noexcept
{
    std::lock_guard lock (_mutex);
    _all_sealed = false;
}

void stream_session_registry_t::force_close_all () noexcept
{
    std::lock_guard lock (_mutex);
    _all_sealed = true;
    _barriers.clear ();
    _connections.clear ();
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
