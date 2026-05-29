/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

namespace zlink
{
namespace spot_actor_api_internal
{

struct session_binding_key_t
{
    session_binding_key_t () : stream (NULL)
    {
        memset (&session_rid, 0, sizeof (session_rid));
    }

    session_binding_key_t (void *stream_,
                           const zlink_routing_id_t &session_rid_) :
        stream (stream_),
        session_rid (session_rid_)
    {
    }

    bool operator< (const session_binding_key_t &other_) const
    {
        const uintptr_t lhs_stream = reinterpret_cast<uintptr_t> (stream);
        const uintptr_t rhs_stream = reinterpret_cast<uintptr_t> (other_.stream);
        if (lhs_stream != rhs_stream)
            return lhs_stream < rhs_stream;
        if (session_rid.size != other_.session_rid.size)
            return session_rid.size < other_.session_rid.size;
        return memcmp (session_rid.data, other_.session_rid.data,
                       session_rid.size)
               < 0;
    }

    void *stream;
    zlink_routing_id_t session_rid;
};

struct session_binding_t
{
    struct actor_entry_t
    {
        actor_entry_t () : actor (NULL) { memset (&ref, 0, sizeof (ref)); }
        actor_handle_t *actor;
        zlink_actor_ref_t ref;
    };

    void *stream;
    zlink_routing_id_t session_rid;
    std::map<std::string, actor_entry_t> actors;
    std::string in_progress_actor_id;
    bool in_progress;

    session_binding_t () : stream (NULL), in_progress (false)
    {
        memset (&session_rid, 0, sizeof (session_rid));
    }
};

struct actor_bound_session_transfer_t
{
    actor_bound_session_transfer_t () :
        valid (false),
        session_node (NULL),
        stream (NULL)
    {
        memset (&session_node_rid, 0, sizeof (session_node_rid));
        memset (&session_rid, 0, sizeof (session_rid));
    }

    bool valid;
    zlink::spot_node_t *session_node;
    zlink_routing_id_t session_node_rid;
    void *stream;
    zlink_routing_id_t session_rid;
};

struct actor_session_state_t
{
    typedef std::map<session_binding_key_t, session_binding_t> binding_map_t;

    binding_map_t::iterator find_binding (void *stream_,
                                          const zlink_routing_id_t *session_rid_);
    binding_map_t::const_iterator find_binding (
      const void *stream_, const zlink_routing_id_t *session_rid_) const;
    binding_map_t::iterator find_remote_binding (
      const zlink_routing_id_t &session_rid_,
      const char *actor_id_,
      uint64_t generation_);
    binding_map_t::iterator bindings_end ();
    binding_map_t::const_iterator bindings_end () const;
    session_binding_t &ensure_binding (void *stream_,
                                       const zlink_routing_id_t &session_rid_);
    void erase_binding (binding_map_t::iterator binding_it_);
    void bind_actor (zlink::spot_node_t *stream_owner_,
                     void *stream_,
                     const zlink_routing_id_t &session_rid_,
                     actor_handle_t *actor_,
                     uint64_t changed_ms_,
                     actor_handle_t **previous_actor_out_);
    void bind_actor_ref (void *stream_,
                         const zlink_routing_id_t &session_rid_,
                         const zlink_actor_ref_t &actor_ref_);
    actor_bound_session_transfer_t capture_bound_session (
      const actor_handle_t *source_) const;
    bool transfer_bound_session (const actor_bound_session_transfer_t &transfer_,
                                 actor_handle_t *target_,
                                 uint64_t changed_ms_);
    bool detach_actor (actor_handle_t *actor_,
                       bool erase_entry_,
                       bool erase_owner_if_unused_);
    bool has_binding_for_stream (void *stream_) const;
    void erase_bindings_for_stream (void *stream_);
    zlink::spot_node_t *stream_owner (void *stream_,
                                      const actor_node_registry_t &nodes_);
    void erase_stream_owner_if_unused (void *stream_);
    void set_explicit_stream_owner (void *stream_, zlink::spot_node_t *node_);
    void clear_explicit_stream_owner (void *stream_);
    void clear_stream (void *stream_);
    void erase_stream_owners_for_node (zlink::spot_node_t *node_);
    int try_set_explicit_stream_owner (void *stream_, zlink::spot_node_t *node_);

    binding_map_t bindings;
    std::map<void *, zlink::spot_node_t *> stream_owners;
    std::set<void *> explicit_stream_owners;
};

}
}
