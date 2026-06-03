/* SPDX-License-Identifier: MPL-2.0 */

#include "actor_gateway_runtime.hpp"

#include <utility>

namespace zlink::framework
{

actor_ref_t::actor_ref_t (node_rid_t node_rid,
                          std::string actor_type,
                          std::string actor_id,
                          std::uint64_t generation)
  : _node_rid (std::move (node_rid)),
    _actor_type (std::move (actor_type)),
    _actor_id (std::move (actor_id)),
    _generation (generation)
{
}

const node_rid_t &
actor_ref_t::node_rid () const noexcept
{
  return _node_rid;
}

std::string_view
actor_ref_t::actor_type () const noexcept
{
  return _actor_type;
}

std::string_view
actor_ref_t::actor_id () const noexcept
{
  return _actor_id;
}

std::uint64_t
actor_ref_t::generation () const noexcept
{
  return _generation;
}

bool
actor_ref_t::empty () const noexcept
{
  return _actor_id.empty () || _actor_type.empty ();
}

bound_session_t::bound_session_t ()
  : _state (std::make_shared<detail::actor_gateway_state_t> ())
{
}

bound_session_t::bound_session_t (
  std::shared_ptr<detail::actor_gateway_state_t> state,
  std::string actor_id)
  : _state (std::move (state)),
    _actor_id (std::move (actor_id))
{
}

bound_session_t::~bound_session_t () = default;
bound_session_t::bound_session_t (bound_session_t &&) noexcept = default;
bound_session_t &bound_session_t::operator= (bound_session_t &&) noexcept =
  default;

send_call_t
bound_session_t::send_raw (const zlink::message_t &payload)
{
  return send_erased (payload);
}

send_call_t
bound_session_t::send_erased (const zlink::message_t &payload)
{
  const auto found = _state->actors_by_id.find (_actor_id);
  if (found != _state->actors_by_id.end () && found->second.disconnected) {
    return send_call_t (result_t<void>::failure (
      framework_error_kind_t::disconnected,
      "actor session is disconnected"));
  }
  if (found == _state->actors_by_id.end () || !found->second.bound) {
    return send_call_t (result_t<void>::failure (
      framework_error_kind_t::actor_session_not_bound,
      "actor session is not bound"));
  }
  stream_header_t header (
    stream_message_kind_t::send,
    stream_codec_t::raw,
    stream_header_flags_t::none,
    std::nullopt,
    std::string ("actor.push"));
  _state->bound_session_pushes.push_back (
    detail::relayed_frame_t { found->second.ref, header, payload });
  return send_call_t (result_t<void>::success ());
}

actor_context_t::actor_context_t ()
  : _state (std::make_shared<detail::actor_gateway_state_t> ())
{
}

actor_context_t::actor_context_t (
  std::shared_ptr<detail::actor_gateway_state_t> state,
  actor_ref_t actor_ref)
  : _state (std::move (state)),
    _actor_ref (std::move (actor_ref))
{
}

actor_context_t::~actor_context_t () = default;
actor_context_t::actor_context_t (actor_context_t &&) noexcept = default;
actor_context_t &actor_context_t::operator= (actor_context_t &&) noexcept =
  default;

const actor_ref_t &
actor_context_t::actor_ref () const noexcept
{
  return _actor_ref;
}

bool
actor_context_t::is_joined () const noexcept
{
  return !_actor_ref.node_rid ().empty ();
}

bound_session_t
actor_context_t::bound_session () const
{
  return bound_session_t (_state, std::string (_actor_ref.actor_id ()));
}

result_t<detail::actor_join_reply_t>
actor_context_t::join_spot_erased (spot_rid_t spot_rid,
                                   const zlink::message_t &request)
{
  if (_actor_ref.empty ()) {
    return result_t<detail::actor_join_reply_t>::failure (
      framework_error_kind_t::actor_route_not_found,
      "actor ref is empty");
  }
  if (spot_rid.empty ()) {
    return result_t<detail::actor_join_reply_t>::failure (
      framework_error_kind_t::spot_route_not_found,
      "spot rid is empty");
  }
  if (!_state->join_spot_dispatcher) {
    return result_t<detail::actor_join_reply_t>::failure (
      framework_error_kind_t::actor_dispatch_handler_not_found,
      "actor join spot dispatcher is not configured");
  }
  auto joined = _state->join_spot_dispatcher (_actor_ref, spot_rid, request);
  if (!joined) {
    const auto *error = joined.error ();
    return result_t<detail::actor_join_reply_t>::failure (
      joined.error_kind (),
      error != nullptr ? error->what () : "actor join spot failed");
  }

  _actor_ref = joined.value ().actor;
  auto found = _state->actors_by_id.find (std::string (_actor_ref.actor_id ()));
  if (found != _state->actors_by_id.end ()) {
    found->second.ref = _actor_ref;
  }
  return joined;
}

actor_join_entry_spot_call_t
actor_context_t::join_entry_spot (node_rid_t spot_node_rid)
{
  if (_actor_ref.empty ()) {
    return actor_join_entry_spot_call_t (result_t<actor_ref_t>::failure (
      framework_error_kind_t::actor_route_not_found,
      "actor ref is empty"));
  }
  if (spot_node_rid.empty ()) {
    return actor_join_entry_spot_call_t (result_t<actor_ref_t>::failure (
      framework_error_kind_t::spot_route_not_found,
      "spot node rid is empty"));
  }
  if (!_state->join_entry_spot_dispatcher) {
    return actor_join_entry_spot_call_t (result_t<actor_ref_t>::failure (
      framework_error_kind_t::actor_dispatch_handler_not_found,
      "actor join entry spot dispatcher is not configured"));
  }

  auto joined = _state->join_entry_spot_dispatcher (_actor_ref, spot_node_rid);
  if (!joined) {
    const auto *error = joined.error ();
    return actor_join_entry_spot_call_t (result_t<actor_ref_t>::failure (
      joined.error_kind (),
      error != nullptr ? error->what () : "actor join entry spot failed"));
  }

  _actor_ref = joined.value ();
  auto found = _state->actors_by_id.find (std::string (_actor_ref.actor_id ()));
  if (found != _state->actors_by_id.end ()) {
    found->second.ref = _actor_ref;
  }
  return actor_join_entry_spot_call_t (
    result_t<actor_ref_t>::success (_actor_ref));
}

session_actor_t::session_actor_t ()
  : _state (std::make_shared<detail::actor_gateway_state_t> ())
{
}

session_actor_t::session_actor_t (
  std::shared_ptr<detail::actor_gateway_state_t> state,
  actor_ref_t ref)
  : _state (std::move (state)),
    _ref (std::move (ref))
{
}

session_actor_t::~session_actor_t () = default;
session_actor_t::session_actor_t (session_actor_t &&) noexcept = default;
session_actor_t &session_actor_t::operator= (session_actor_t &&) noexcept =
  default;

const actor_ref_t &
session_actor_t::ref () const noexcept
{
  return _ref;
}

std::string_view
session_actor_t::actor_id () const noexcept
{
  return _ref.actor_id ();
}

actor_context_t
session_actor_t::context () const
{
  return actor_context_t (_state, _ref);
}

bound_session_t
session_actor_t::bound_session () const
{
  return bound_session_t (_state, std::string (_ref.actor_id ()));
}

relay_call_t
session_actor_t::relay (const stream_header_t &header,
                        const zlink::message_t &payload)
{
  if (_ref.empty ()) {
    return relay_call_t (result_t<void>::failure (
      framework_error_kind_t::actor_route_not_found,
      "session actor is not bound"));
  }
  const auto found = _state->actors_by_id.find (std::string (_ref.actor_id ()));
  if (found != _state->actors_by_id.end () && found->second.disconnected) {
    return relay_call_t (result_t<void>::failure (
      framework_error_kind_t::disconnected,
      "actor session is disconnected"));
  }
  if (found == _state->actors_by_id.end () || !found->second.bound) {
    return relay_call_t (result_t<void>::failure (
      framework_error_kind_t::actor_session_not_bound,
      "actor session is not bound"));
  }
  _state->relayed_frames.push_back (
    detail::relayed_frame_t { _ref, header, payload });
  return relay_call_t (result_t<void>::success ());
}

relay_call_t
session_actor_t::notify_disconnected ()
{
  const auto found = _state->actors_by_id.find (std::string (_ref.actor_id ()));
  if (found != _state->actors_by_id.end ()) {
    found->second.bound = false;
    found->second.disconnected = true;
  }
  return relay_call_t (result_t<void>::success ());
}

session_actor_manager_t::session_actor_manager_t ()
  : _state (std::make_shared<detail::actor_gateway_state_t> ())
{
}

session_actor_manager_t::session_actor_manager_t (
  std::shared_ptr<detail::actor_gateway_state_t> state)
  : _state (std::move (state))
{
}

session_actor_manager_t::~session_actor_manager_t () = default;
session_actor_manager_t::session_actor_manager_t (
  session_actor_manager_t &&) noexcept = default;
session_actor_manager_t &session_actor_manager_t::operator= (
  session_actor_manager_t &&) noexcept = default;

result_t<session_actor_t>
session_actor_manager_t::create (std::string actor_type,
                                 std::string actor_id)
{
  if (actor_type.empty () || actor_id.empty ()) {
    return result_t<session_actor_t>::failure (
      framework_error_kind_t::request_protocol_error,
      "actor type and id are required");
  }
  if (_state->actors_by_id.find (actor_id) != _state->actors_by_id.end ()) {
    return result_t<session_actor_t>::failure (
      framework_error_kind_t::actor_already_exists,
      "actor already exists");
  }
  actor_ref_t ref (
    node_rid_t::from_string ("local"),
    std::move (actor_type),
    actor_id,
    1);
  _state->actors_by_id.emplace (
    actor_id,
    detail::actor_record_t { ref, false, false });
  return result_t<session_actor_t>::success (session_actor_t (_state, ref));
}

std::optional<session_actor_t>
session_actor_manager_t::find (std::string actor_id) const
{
  const auto found = _state->actors_by_id.find (actor_id);
  if (found == _state->actors_by_id.end ()) {
    return std::nullopt;
  }
  return session_actor_t (_state, found->second.ref);
}

result_t<session_actor_t>
session_actor_manager_t::get_or_create (std::string actor_type,
                                        std::string actor_id)
{
  if (auto actor = find (actor_id)) {
    if (actor->ref ().actor_type () != actor_type) {
      return result_t<session_actor_t>::failure (
        framework_error_kind_t::actor_type_mismatch,
        "actor id is already bound to another type");
    }
    return result_t<session_actor_t>::success (*actor);
  }
  return create (std::move (actor_type), std::move (actor_id));
}

request_call_t<session_actor_t>
session_actor_manager_t::bind (actor_ref_t actor_ref)
{
  if (actor_ref.empty ()) {
    return request_call_t<session_actor_t> (
      result_t<session_actor_t>::failure (
        framework_error_kind_t::actor_route_not_found,
        "actor ref is empty"));
  }
  auto found = _state->actors_by_id.find (std::string (actor_ref.actor_id ()));
  if (found == _state->actors_by_id.end ()) {
    _state->actors_by_id.emplace (
      std::string (actor_ref.actor_id ()),
      detail::actor_record_t { actor_ref, true, false });
  } else {
    if (found->second.ref.actor_type () != actor_ref.actor_type ()) {
      return request_call_t<session_actor_t> (
        result_t<session_actor_t>::failure (
          framework_error_kind_t::actor_type_mismatch,
          "actor id is already bound to another type"));
    }
    found->second.bound = true;
    found->second.disconnected = false;
  }
  return request_call_t<session_actor_t> (
    result_t<session_actor_t>::success (session_actor_t (_state, actor_ref)));
}

void
session_actor_manager_t::unbind_session (std::string actor_id) noexcept
{
  const auto found = _state->actors_by_id.find (actor_id);
  if (found != _state->actors_by_id.end ()) {
    found->second.bound = false;
    found->second.disconnected = true;
  }
}

} // namespace zlink::framework

namespace zlink::framework::detail
{

actor_gateway_runtime_t::actor_gateway_runtime_t ()
  : _state (std::make_shared<actor_gateway_state_t> ())
{
}

actor_gateway_runtime_t::actor_gateway_runtime_t (
  std::shared_ptr<actor_gateway_state_t> state)
  : _state (std::move (state))
{
}

session_actor_manager_t
actor_gateway_runtime_t::manager () const
{
  return session_actor_manager_t (_state);
}

std::vector<relayed_frame_t>
actor_gateway_runtime_t::relayed_frames () const
{
  return _state->relayed_frames;
}

std::vector<relayed_frame_t>
actor_gateway_runtime_t::bound_session_pushes () const
{
  return _state->bound_session_pushes;
}

bool
actor_gateway_runtime_t::actor_bound (std::string actor_id) const
{
  const auto found = _state->actors_by_id.find (actor_id);
  return found != _state->actors_by_id.end () && found->second.bound;
}

bool
actor_gateway_runtime_t::actor_disconnected (std::string actor_id) const
{
  const auto found = _state->actors_by_id.find (actor_id);
  return found != _state->actors_by_id.end () && found->second.disconnected;
}

void
actor_gateway_runtime_t::on_join_spot (
  actor_gateway_state_t::join_spot_dispatcher_t dispatcher)
{
  _state->join_spot_dispatcher = std::move (dispatcher);
}

void
actor_gateway_runtime_t::on_join_entry_spot (
  actor_gateway_state_t::join_entry_spot_dispatcher_t dispatcher)
{
  _state->join_entry_spot_dispatcher = std::move (dispatcher);
}

} // namespace zlink::framework::detail
