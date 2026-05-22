/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

const uint32_t actor_handle_tag = 0xacc70001u;

struct queued_actor_part_t
{
    queued_actor_part_t () : owns (false), part_flag (ZLINK_PART_FINAL)
    {
        memset (&info, 0, sizeof (info));
        memset (&part, 0, sizeof (part));
    }

    ~queued_actor_part_t ()
    {
        if (owns)
            (void) zlink_msg_close (&part);
    }

    queued_actor_part_t (queued_actor_part_t &&other_) noexcept :
        owns (other_.owns),
        info (other_.info),
        part_flag (other_.part_flag)
    {
        part = other_.part;
        other_.owns = false;
        memset (&other_.part, 0, sizeof (other_.part));
    }

    queued_actor_part_t &operator= (queued_actor_part_t &&other_) noexcept
    {
        if (this == &other_)
            return *this;
        if (owns)
            (void) zlink_msg_close (&part);
        owns = other_.owns;
        info = other_.info;
        part_flag = other_.part_flag;
        part = other_.part;
        other_.owns = false;
        memset (&other_.part, 0, sizeof (other_.part));
        return *this;
    }

    queued_actor_part_t (const queued_actor_part_t &) = delete;
    queued_actor_part_t &operator= (const queued_actor_part_t &) = delete;

    bool owns;
    zlink_actor_recv_info_t info;
    zlink_msg_t part;
    zlink_part_flag_t part_flag;
};

struct actor_handle_t
{
    actor_handle_t () :
        tag (actor_handle_tag),
        node (NULL),
        generation (0),
        join_epoch (0),
        bound_session_node (NULL),
        bound_stream (NULL),
        last_changed_ms (0),
        pending_remote_join (false)
    {
        memset (&node_rid, 0, sizeof (node_rid));
        memset (&ref_cache, 0, sizeof (ref_cache));
        memset (&bound_session_rid, 0, sizeof (bound_session_rid));
    }

    bool check_tag () const { return tag == actor_handle_tag; }

    uint32_t tag;
    zlink::spot_node_t *node;
    zlink_routing_id_t node_rid;
    zlink_actor_ref_t ref_cache;
    std::string actor_id;
    uint64_t generation;
    uint64_t join_epoch;
    std::shared_ptr<spot_logical_state_t> joined_spot_state;
    zlink::spot_node_t *bound_session_node;
    void *bound_stream;
    zlink_routing_id_t bound_session_rid;
    uint64_t last_changed_ms;
    bool pending_remote_join;
    std::deque<queued_actor_part_t> queue;
};

namespace
{

struct queued_join_request_t
{
    queued_join_request_t () :
        actor (NULL),
        spot (NULL),
        target_node (NULL),
        remote (false),
        handler (NULL),
        userdata (NULL),
        replied (false),
        join_epoch (0),
        pending_target (NULL),
        indexed (false)
    {
        memset (&target_node_rid, 0, sizeof (target_node_rid));
        memset (&source_spot_rid, 0, sizeof (source_spot_rid));
        memset (&target_actor_ref, 0, sizeof (target_actor_ref));
    }

    ~queued_join_request_t ()
    {
        zlink::spot_clear_msg_parts (&message_parts);
        zlink::spot_clear_msg_parts (&reply_parts);
    }

    actor_handle_t *actor;
    spot_handle_t *spot;
    std::shared_ptr<spot_logical_state_t> spot_state;
    zlink::spot_node_t *target_node;
    bool remote;
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t source_spot_rid;
    zlink_actor_ref_t target_actor_ref;
    zlink_actor_join_handler_fn handler;
    void *userdata;
    bool replied;
    uint64_t join_epoch;
    actor_handle_t *pending_target;
    zlink::spot_owned_msg_parts_t message_parts;
    zlink::spot_owned_msg_parts_t reply_parts;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    bool indexed;
};

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

struct lifecycle_registration_t
{
    lifecycle_registration_t () :
        on_join (NULL),
        on_leave (NULL),
        userdata (NULL)
    {
    }

    zlink_spot_actor_lifecycle_handler_fn on_join;
    zlink_spot_actor_lifecycle_handler_fn on_leave;
    void *userdata;
};

struct lifecycle_event_t
{
    lifecycle_event_t () : join (false)
    {
        memset (&info, 0, sizeof (info));
    }

    bool join;
    zlink_spot_actor_lifecycle_info_t info;
};

struct actor_spot_snapshot_t
{
    actor_spot_snapshot_t () : facade (NULL) {}

    spot_handle_t *facade;
    std::shared_ptr<spot_logical_state_t> state;
};

struct actor_node_registry_t
{
    void register_spot (spot_handle_t *spot_);
    void erase_spot (spot_handle_t *spot_);
    spot_handle_t *find_spot_for_state (
      zlink::spot_node_t *node_,
      const std::shared_ptr<spot_logical_state_t> &state_) const;
    spot_handle_t *find_replacement_spot (
      spot_handle_t *spot_,
      const std::shared_ptr<spot_logical_state_t> &state_) const;
    bool has_peer_spot_facade (spot_handle_t *spot_) const;
    bool collect_spots_for_node (
      zlink::spot_node_t *node_,
      const std::shared_ptr<spot_logical_state_t> &entry_state_,
      std::vector<actor_spot_snapshot_t> *out_) const;
    void register_node (zlink::spot_node_t *node_,
                        const zlink_routing_id_t &node_rid_);
    void erase_node (zlink::spot_node_t *node_);
    void erase_node_routes (zlink::spot_node_t *node_);
    void erase_known_node (zlink::spot_node_t *node_);
    zlink::spot_node_t *resolve_node_by_rid (
      const zlink_routing_id_t &rid_) const;
    bool known_node (zlink::spot_node_t *node_) const;
    zlink::spot_node_t *find_socket_owner (zlink::socket_base_t *socket_) const;
    void collect_actor_handles (std::vector<actor_handle_t *> *out_) const;
    actor_handle_t *find_unique_actor_by_id (const char *actor_id_,
                                             bool include_pending_) const;

    std::map<std::string, zlink::spot_node_t *> nodes_by_rid;
    std::set<spot_handle_t *> known_spots;
    std::set<zlink::spot_node_t *> known_nodes;
};

struct actor_session_state_t
{
    typedef std::map<session_binding_key_t, session_binding_t> binding_map_t;

    binding_map_t::iterator find_binding (void *stream_,
                                          const zlink_routing_id_t *session_rid_);
    binding_map_t::const_iterator find_binding (
      const void *stream_, const zlink_routing_id_t *session_rid_) const;
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

struct actor_route_state_t
{
    bool is_disconnected (zlink::spot_node_t *source_node_,
                          const zlink_routing_id_t &target_rid_) const;
    bool active_matches (const actor_handle_t *actor_) const;
    bool active_exists (const actor_handle_t *actor_) const;
    bool find_active (const char *actor_id_, zlink_actor_route_t *route_out_)
      const;
    void publish_active (actor_handle_t *actor_, bool create_);
    void remove_matching_active (actor_handle_t *actor_);
    void erase_disconnected_for_node (zlink::spot_node_t *node_);
    void mark_disconnected (zlink::spot_node_t *node_,
                            const zlink_routing_id_t &target_node_rid_);

    std::map<std::string, zlink_actor_route_t> active;
    std::set<std::pair<zlink::spot_node_t *, std::string> > disconnected;
};

struct actor_join_state_t
{
    typedef std::map<spot_logical_state_t *,
                     std::deque<queued_join_request_t *> >
      queue_map_t;

    std::deque<queued_join_request_t *> &queue_for (
      spot_logical_state_t *key_);
    bool find_queue (spot_logical_state_t *key_, queue_map_t::iterator *it_);
    void erase_queue_if_empty (spot_logical_state_t *key_);
    void enqueue (queued_join_request_t *request_);
    bool peek_for_spot (spot_handle_t *spot_,
                        queued_join_request_t **request_out_);
    void remove_queued (queued_join_request_t *request_);
    void take_queue (spot_logical_state_t *key_,
                     std::deque<queued_join_request_t *> *pending_);
    void replace_live_spot (spot_handle_t *from_, spot_handle_t *to_);
    void collect_live_for_state (
      const std::shared_ptr<spot_logical_state_t> &state_,
      std::deque<queued_join_request_t *> *pending_) const;
    void drain_queued_for_stream (
      void *stream_, std::deque<queued_join_request_t *> *aborted_);
    void collect_live_for_stream (
      void *stream_,
      const std::deque<queued_join_request_t *> &already_aborted_,
      std::vector<queued_join_request_t *> *received_aborts_) const;
    bool spot_has_pending (spot_logical_state_t *key_) const;
    uint32_t pending_count_for_spot (spot_logical_state_t *key_) const;
    bool is_live (queued_join_request_t *request_) const;
    void mark_live (queued_join_request_t *request_);
    void unmark_live (queued_join_request_t *request_);
    bool actor_has_pending (const actor_handle_t *actor_) const;
    void increment_actor_pending (actor_handle_t *actor_);
    void decrement_actor_pending (actor_handle_t *actor_);
    void increment_spot_pending (spot_logical_state_t *key_);
    void decrement_spot_pending (spot_logical_state_t *key_);
    bool has_pending_remote_actor (zlink::spot_node_t *node_,
                                   const char *actor_id_) const;
    void track_pending_remote_actor (queued_join_request_t *request_);
    void untrack_pending_remote_actor (queued_join_request_t *request_);

    queue_map_t queues;
    std::set<queued_join_request_t *> live_requests;
    std::map<actor_handle_t *, size_t> pending_count_by_actor;
    std::set<std::pair<zlink::spot_node_t *, std::string> >
      pending_remote_actor_keys;
    std::map<spot_logical_state_t *, size_t> pending_count_by_spot;
};

struct actor_lifecycle_state_t
{
    lifecycle_registration_t *find_registration (spot_logical_state_t *key_);
    void clear (spot_logical_state_t *key_);
    lifecycle_registration_t &ensure_registration (spot_logical_state_t *key_);
    void enqueue (spot_logical_state_t *key_, const lifecycle_event_t &event_);
    bool pop (spot_logical_state_t *key_,
              lifecycle_event_t *event_out_,
              lifecycle_registration_t *registration_out_);

    std::map<spot_logical_state_t *, lifecycle_registration_t> handlers;
    std::map<spot_logical_state_t *, std::deque<lifecycle_event_t> > queues;
};

struct actor_runtime_t
{
    actor_runtime_t () : protocol_drop_count (0), next_join_epoch (1) {}

    std::timed_mutex mutex;
    actor_node_registry_t nodes;
    actor_session_state_t sessions;
    actor_route_state_t routes;
    actor_join_state_t joins;
    actor_lifecycle_state_t lifecycle;
    uint64_t protocol_drop_count;
    uint64_t next_join_epoch;
};

}
