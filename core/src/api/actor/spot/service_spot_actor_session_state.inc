/* SPDX-License-Identifier: MPL-2.0 */

session_binding_key_t session_key (void *stream_,
                                   const zlink_routing_id_t *session_rid_)
{
    return session_binding_key_t (stream_, *session_rid_);
}

actor_session_state_t::binding_map_t::iterator
actor_session_state_t::find_binding (void *stream_,
                                     const zlink_routing_id_t *session_rid_)
{
    return bindings.find (session_key (stream_, session_rid_));
}

actor_session_state_t::binding_map_t::const_iterator
actor_session_state_t::find_binding (
  const void *stream_, const zlink_routing_id_t *session_rid_) const
{
    return bindings.find (
      session_key (const_cast<void *> (stream_), session_rid_));
}

actor_session_state_t::binding_map_t::iterator
actor_session_state_t::find_remote_binding (
  const zlink_routing_id_t &session_rid_,
  const char *actor_id_,
  uint64_t generation_)
{
    for (binding_map_t::iterator binding_it = bindings.begin ();
         binding_it != bindings.end (); ++binding_it) {
        if (!same_routing_id (binding_it->second.session_rid, session_rid_))
            continue;
        std::map<std::string, session_binding_t::actor_entry_t>::iterator
          actor_it = binding_it->second.actors.find (actor_id_);
        if (actor_it == binding_it->second.actors.end ())
            continue;
        if (generation_ != 0
            && actor_it->second.ref.generation != generation_)
            continue;
        return binding_it;
    }
    return bindings.end ();
}

actor_session_state_t::binding_map_t::iterator
actor_session_state_t::bindings_end ()
{
    return bindings.end ();
}

actor_session_state_t::binding_map_t::const_iterator
actor_session_state_t::bindings_end () const
{
    return bindings.end ();
}

session_binding_t &actor_session_state_t::ensure_binding (
  void *stream_, const zlink_routing_id_t &session_rid_)
{
    return bindings[session_key (stream_, &session_rid_)];
}

void actor_session_state_t::erase_binding (binding_map_t::iterator binding_it_)
{
    bindings.erase (binding_it_);
}

void actor_session_state_t::bind_actor (
  zlink::spot_node_t *stream_owner_,
  void *stream_,
  const zlink_routing_id_t &session_rid_,
  actor_handle_t *actor_,
  uint64_t changed_ms_,
  actor_handle_t **previous_actor_out_)
{
    if (previous_actor_out_)
        *previous_actor_out_ = NULL;

    session_binding_t &binding = ensure_binding (stream_, session_rid_);
    binding.stream = stream_;
    binding.session_rid = session_rid_;
    std::map<std::string, session_binding_t::actor_entry_t>::iterator
      previous = binding.actors.find (actor_->actor_id);
    if (previous != binding.actors.end () && previous->second.actor
        && previous->second.actor != actor_ && previous_actor_out_)
        *previous_actor_out_ = previous->second.actor;

    session_binding_t::actor_entry_t entry;
    entry.actor = actor_;
    fill_ref (actor_, &entry.ref);
    binding.actors[actor_->actor_id] = entry;
    actor_->bound_session_node = stream_owner_;
    memset (&actor_->bound_session_node_rid, 0,
            sizeof (actor_->bound_session_node_rid));
    if (stream_owner_)
        (void) stream_owner_->node_routing_id (&actor_->bound_session_node_rid);
    actor_->bound_stream = stream_;
    actor_->bound_session_rid = session_rid_;
    actor_->last_changed_ms = changed_ms_;
}

void actor_session_state_t::bind_actor_ref (
  void *stream_,
  const zlink_routing_id_t &session_rid_,
  const zlink_actor_ref_t &actor_ref_)
{
    session_binding_t &binding = ensure_binding (stream_, session_rid_);
    binding.stream = stream_;
    binding.session_rid = session_rid_;

    session_binding_t::actor_entry_t entry;
    entry.actor = NULL;
    entry.ref = actor_ref_;
    binding.actors[actor_ref_.actor_id] = entry;
}

actor_bound_session_transfer_t actor_session_state_t::capture_bound_session (
  const actor_handle_t *source_) const
{
    actor_bound_session_transfer_t transfer;
    if (!source_ || !source_->bound_stream || !source_->bound_session_node
        || !valid_routing_id (&source_->bound_session_rid))
        return transfer;

    transfer.valid = true;
    transfer.session_node = source_->bound_session_node;
    transfer.session_node_rid = source_->bound_session_node_rid;
    transfer.stream = source_->bound_stream;
    transfer.session_rid = source_->bound_session_rid;
    return transfer;
}

bool actor_session_state_t::transfer_bound_session (
  const actor_bound_session_transfer_t &transfer_,
  actor_handle_t *target_,
  uint64_t changed_ms_)
{
    if (!transfer_.valid || !target_)
        return false;

    binding_map_t::iterator binding_it =
      find_binding (transfer_.stream, &transfer_.session_rid);
    if (binding_it == bindings.end ())
        return false;

    session_binding_t::actor_entry_t &entry =
      binding_it->second.actors[target_->actor_id];
    entry.actor = target_;
    fill_ref (target_, &entry.ref);
    target_->bound_session_node = transfer_.session_node;
    target_->bound_session_node_rid = transfer_.session_node_rid;
    target_->bound_stream = transfer_.stream;
    target_->bound_session_rid = transfer_.session_rid;
    target_->last_changed_ms = changed_ms_;
    return true;
}

bool actor_session_state_t::detach_actor (actor_handle_t *actor_,
                                          bool erase_entry_,
                                          bool erase_owner_if_unused_)
{
    if (!actor_ || !actor_->bound_stream)
        return false;

    void *bound_stream = actor_->bound_stream;
    binding_map_t::iterator binding_it =
      find_binding (actor_->bound_stream, &actor_->bound_session_rid);
    if (binding_it == bindings.end ())
        return false;

    std::map<std::string, session_binding_t::actor_entry_t>::iterator entry_it =
      binding_it->second.actors.find (actor_->actor_id);
    if (entry_it == binding_it->second.actors.end ())
        return false;

    if (erase_entry_) {
        binding_it->second.actors.erase (entry_it);
        if (binding_it->second.actors.empty ()) {
            erase_binding (binding_it);
            if (erase_owner_if_unused_)
                erase_stream_owner_if_unused (bound_stream);
        }
    } else {
        entry_it->second.actor = NULL;
    }
    return true;
}

bool actor_session_state_t::has_binding_for_stream (void *stream_) const
{
    if (!stream_)
        return false;
    for (binding_map_t::const_iterator it = bindings.begin ();
         it != bindings.end (); ++it) {
        if (it->second.stream == stream_)
            return true;
    }
    return false;
}

void actor_session_state_t::erase_bindings_for_stream (void *stream_)
{
    if (!stream_)
        return;
    for (binding_map_t::iterator it = bindings.begin ();
         it != bindings.end ();) {
        if (it->second.stream != stream_) {
            ++it;
            continue;
        }
        for (std::map<std::string, session_binding_t::actor_entry_t>::iterator
               actor_it = it->second.actors.begin ();
             actor_it != it->second.actors.end (); ++actor_it) {
            actor_handle_t *actor = actor_it->second.actor;
            if (actor && actor->bound_stream == stream_)
                clear_actor_bound_session_locked (actor, true);
        }
        it = bindings.erase (it);
    }
}

zlink::spot_node_t *actor_session_state_t::stream_owner (
  void *stream_, const actor_node_registry_t &nodes_)
{
    zlink::socket_base_t *stream = try_as_socket (stream_);
    if (!stream)
        return NULL;
    std::map<void *, zlink::spot_node_t *>::const_iterator it =
      stream_owners.find (stream_);
    if (it != stream_owners.end ())
        return nodes_.known_node (it->second) ? it->second : NULL;
    zlink::spot_node_t *owner = nodes_.find_socket_owner (stream);
    if (owner) {
        stream_owners[stream_] = owner;
        return owner;
    }
    return NULL;
}

void actor_session_state_t::erase_stream_owner_if_unused (void *stream_)
{
    if (!stream_)
        return;
    if (explicit_stream_owners.count (stream_) != 0)
        return;
    if (has_binding_for_stream (stream_))
        return;
    stream_owners.erase (stream_);
}

void actor_session_state_t::set_explicit_stream_owner (
  void *stream_, zlink::spot_node_t *node_)
{
    stream_owners[stream_] = node_;
    explicit_stream_owners.insert (stream_);
}

void actor_session_state_t::clear_explicit_stream_owner (void *stream_)
{
    explicit_stream_owners.erase (stream_);
    stream_owners.erase (stream_);
}

void actor_session_state_t::clear_stream (void *stream_)
{
    erase_bindings_for_stream (stream_);
    clear_explicit_stream_owner (stream_);
}

void actor_session_state_t::erase_stream_owners_for_node (
  zlink::spot_node_t *node_)
{
    for (std::map<void *, zlink::spot_node_t *>::iterator it =
           stream_owners.begin ();
         it != stream_owners.end ();) {
        if (it->second == node_) {
            explicit_stream_owners.erase (it->first);
            it = stream_owners.erase (it);
        } else {
            ++it;
        }
    }
}

int actor_session_state_t::try_set_explicit_stream_owner (
  void *stream_, zlink::spot_node_t *node_)
{
    std::map<void *, zlink::spot_node_t *>::const_iterator previous =
      stream_owners.find (stream_);
    if (previous != stream_owners.end () && previous->second != node_) {
        errno = EBUSY;
        return -1;
    }
    set_explicit_stream_owner (stream_, node_);
    return 0;
}
