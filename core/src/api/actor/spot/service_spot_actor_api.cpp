/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service/service_handle_internal.hpp"
#include "api/service/service_mode_internal.hpp"
#include "api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"
#include "api/spot/dispatch/service_spot_dispatch_context_internal.hpp"
#include "services/actor/async/service_spot_actor_async_internal.hpp"
#include "services/actor/service_spot_actor_internal.hpp"
#include "services/actor/multipart/service_spot_actor_multipart_internal.hpp"
#include "services/actor/packet/service_spot_actor_packet_internal.hpp"
#include "services/actor/result/service_spot_actor_result_internal.hpp"
#include "services/actor/validation/service_spot_actor_validation_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_utils_internal.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "api/message/recv_result_internal.hpp"
#include "api/core/config_result_internal.hpp"
#include "api/message/handler_result_internal.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_subject_access.hpp"
#include "core/recv_tls_view.hpp"
#include "utils/clock.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
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

struct actor_node_registry_t
{
    std::map<std::string, zlink::spot_node_t *> nodes_by_rid;
    std::set<spot_handle_t *> known_spots;
    std::set<zlink::spot_node_t *> known_nodes;
};

struct actor_session_state_t
{
    typedef std::map<session_binding_key_t, session_binding_t> binding_map_t;

    binding_map_t bindings;
    std::map<void *, zlink::spot_node_t *> stream_owners;
    std::set<void *> explicit_stream_owners;
};

struct actor_route_state_t
{
    std::map<std::string, zlink_actor_route_t> active;
    std::set<std::pair<zlink::spot_node_t *, std::string> > disconnected;
};

struct actor_join_state_t
{
    std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >
      queues;
    std::set<queued_join_request_t *> live_requests;
    std::map<actor_handle_t *, size_t> pending_count_by_actor;
    std::set<std::pair<zlink::spot_node_t *, std::string> >
      pending_remote_actor_keys;
    std::map<spot_logical_state_t *, size_t> pending_count_by_spot;
};

struct actor_lifecycle_state_t
{
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

actor_runtime_t &actor_runtime ()
{
    static actor_runtime_t runtime;
    return runtime;
}

struct actor_lifecycle_callback_context_t
{
    actor_lifecycle_callback_context_t () :
        active (false),
        spot (NULL)
    {
        memset (&previous_actor, 0, sizeof (previous_actor));
        memset (&current_actor, 0, sizeof (current_actor));
    }

    bool active;
    spot_handle_t *spot;
    zlink_actor_ref_t previous_actor;
    zlink_actor_ref_t current_actor;
};

actor_lifecycle_callback_context_t &actor_lifecycle_context ()
{
    static thread_local actor_lifecycle_callback_context_t context;
    return context;
}

class actor_lifecycle_handler_scope_t
{
  public:
    actor_lifecycle_handler_scope_t (
      spot_handle_t *spot_, const zlink_spot_actor_lifecycle_info_t &info_) :
        _previous (actor_lifecycle_context ())
    {
        actor_lifecycle_callback_context_t &context =
          actor_lifecycle_context ();
        context.active = true;
        context.spot = spot_;
        context.previous_actor = info_.previous_actor;
        context.current_actor = info_.current_actor;
    }

    ~actor_lifecycle_handler_scope_t ()
    {
        actor_lifecycle_context () = _previous;
    }

  private:
    actor_lifecycle_callback_context_t _previous;
};

uint64_t now_ms ();
uint64_t next_generation_for_node_locked (zlink::spot_node_t *node_);
void update_active_route_locked (actor_handle_t *actor_);
uint64_t next_commit_epoch_locked ();
bool known_node_locked (zlink::spot_node_t *node_);
std::set<actor_handle_t *> &actor_handles_locked (zlink::spot_node_t *node_);
std::map<std::string, actor_handle_t *> &actors_by_id_locked (
  zlink::spot_node_t *node_);
using zlink::spot_actor_internal::adopt_multipart_payload;
using zlink::spot_actor_internal::actor_missing_request_result_from_errno;
using zlink::spot_actor_internal::actor_missing_submit_result_from_errno;
using zlink::spot_actor_internal::consume_multipart_payload;
using zlink::spot_actor_internal::copy_msg_for_stream_send;
using zlink::spot_actor_internal::errno_to_request_result;
using zlink::spot_actor_internal::errno_to_submit_result;
using zlink::spot_actor_internal::request_result_to_submit_result;
using zlink::spot_actor_internal::same_actor_ref_identity;
using zlink::spot_actor_internal::same_routing_id;
using zlink::spot_actor_internal::send_copied_msg_to_bound_stream;
using zlink::spot_actor_internal::valid_actor_id;
using zlink::spot_actor_internal::valid_multipart_payload;
using zlink::spot_actor_internal::valid_routing_id;

bool same_logical_spot (const spot_handle_t *lhs_, const spot_handle_t *rhs_)
{
    if (!lhs_ || !rhs_)
        return false;
    if (lhs_ == rhs_)
        return true;
    return lhs_->logical_state && lhs_->logical_state == rhs_->logical_state;
}

spot_logical_state_t *join_queue_key (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    return state_.get ();
}

spot_logical_state_t *join_queue_key (const queued_join_request_t *request_)
{
    if (!request_)
        return NULL;
    if (request_->spot_state)
        return request_->spot_state.get ();
    return request_->spot && request_->spot->logical_state
             ? request_->spot->logical_state.get ()
             : NULL;
}

spot_handle_t *find_spot_facade_for_state_locked (
  zlink::spot_node_t *node_,
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!node_ || !state_)
        return NULL;
    for (std::set<spot_handle_t *>::iterator it = actor_runtime().nodes.known_spots.begin ();
         it != actor_runtime().nodes.known_spots.end (); ++it) {
        if ((*it)->node == node_ && (*it)->logical_state == state_)
            return *it;
    }
    return NULL;
}

bool actor_in_entry_spot_locked (const actor_handle_t *actor_)
{
    return actor_ && actor_->joined_spot_state
           && actor_->joined_spot_state->entry;
}

bool actor_in_user_spot_locked (const actor_handle_t *actor_)
{
    return actor_ && actor_->joined_spot_state
           && !actor_->joined_spot_state->entry;
}

zlink_spot_kind_t spot_kind_for_state (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_)
        return ZLINK_SPOT_KIND_INVALID;
    return state_->entry ? ZLINK_SPOT_KIND_ENTRY : ZLINK_SPOT_KIND_USER;
}

bool actor_route_is_current_location (const zlink_actor_route_t &route_,
                                      const char *actor_id_)
{
    if (!actor_id_ || route_.actor.node_rid.size == 0
        || route_.current_spot_rid.size == 0
        || (route_.current_spot_kind != ZLINK_SPOT_KIND_ENTRY
            && route_.current_spot_kind != ZLINK_SPOT_KIND_USER)) {
        return false;
    }
    return strncmp (route_.actor.actor_id, actor_id_, ZLINK_ACTOR_ID_MAX) == 0;
}

bool actor_has_pending_join_locked (const actor_handle_t *actor_)
{
    if (!actor_)
        return false;
    return actor_runtime().joins.pending_count_by_actor.count (
             const_cast<actor_handle_t *> (actor_))
           != 0;
}

bool node_has_pending_join_actor_locked (zlink::spot_node_t *node_,
                                         const char *actor_id_)
{
    if (!node_ || !actor_id_)
        return false;
    std::map<std::string, actor_handle_t *> &actors =
      actors_by_id_locked (node_);
    std::map<std::string, actor_handle_t *>::const_iterator actor_it =
      actors.find (actor_id_);
    if (actor_it != actors.end () && actor_it->second->pending_remote_join)
        return true;
    return actor_runtime().joins.pending_remote_actor_keys.count (
             std::make_pair (node_, std::string (actor_id_)))
           != 0;
}

bool join_request_live_locked (queued_join_request_t *request_)
{
    return request_
           && actor_runtime().joins.live_requests.count (request_) != 0;
}

void clear_actor_bound_session_locked (actor_handle_t *actor_,
                                       bool update_changed_time_);
void clear_actor_joined_spot_locked (actor_handle_t *actor_);
void retire_join_request_locked (queued_join_request_t *request_);

std::deque<queued_join_request_t *> &join_queue_for_state_locked (
  spot_logical_state_t *key_)
{
    return actor_runtime().joins.queues[key_];
}

std::deque<queued_join_request_t *> &join_queue_for_state_locked (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    return join_queue_for_state_locked (join_queue_key (state_));
}

bool find_join_queue_locked (
  spot_logical_state_t *key_,
  std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >::
    iterator *it_)
{
    if (!key_ || !it_)
        return false;
    *it_ = actor_runtime().joins.queues.find (key_);
    return *it_ != actor_runtime().joins.queues.end ();
}

void erase_join_queue_if_empty_locked (spot_logical_state_t *key_)
{
    if (!key_)
        return;
    std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >::
      iterator it = actor_runtime().joins.queues.find (key_);
    if (it != actor_runtime().joins.queues.end () && it->second.empty ())
        actor_runtime().joins.queues.erase (it);
}

bool spot_has_pending_join_locked (spot_logical_state_t *key_)
{
    if (!key_)
        return false;
    std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >::
      const_iterator queue_it = actor_runtime().joins.queues.find (key_);
    if (queue_it != actor_runtime().joins.queues.end ()
        && !queue_it->second.empty ())
        return true;
    return actor_runtime().joins.pending_count_by_spot.count (key_) != 0;
}

void enqueue_join_request_locked (queued_join_request_t *request_)
{
    if (!request_)
        return;
    join_queue_for_state_locked (join_queue_key (request_)).push_back (request_);
}

bool peek_join_request_for_spot_locked (spot_handle_t *spot_,
                                        queued_join_request_t **request_out_)
{
    if (!spot_ || !spot_->logical_state || !request_out_)
        return false;
    spot_logical_state_t *key = join_queue_key (spot_->logical_state);
    std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >::
      iterator queue_it;
    if (!find_join_queue_locked (key, &queue_it) || queue_it->second.empty ())
        return false;
    *request_out_ = queue_it->second.front ();
    return true;
}

void take_join_queue_locked (spot_logical_state_t *key_,
                             std::deque<queued_join_request_t *> *pending_)
{
    if (!key_ || !pending_)
        return;
    std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >::
      iterator queue_it;
    if (!find_join_queue_locked (key_, &queue_it))
        return;
    pending_->swap (queue_it->second);
    actor_runtime().joins.queues.erase (queue_it);
}

void replace_live_join_spot_locked (spot_handle_t *from_, spot_handle_t *to_)
{
    for (std::set<queued_join_request_t *>::iterator it =
           actor_runtime().joins.live_requests.begin ();
         it != actor_runtime().joins.live_requests.end (); ++it) {
        if ((*it)->spot == from_)
            (*it)->spot = to_;
    }
}

void collect_live_join_requests_for_state_locked (
  const std::shared_ptr<spot_logical_state_t> &state_,
  std::deque<queued_join_request_t *> *pending_)
{
    if (!state_ || !pending_)
        return;
    for (std::set<queued_join_request_t *>::iterator it =
           actor_runtime().joins.live_requests.begin ();
         it != actor_runtime().joins.live_requests.end (); ++it) {
        queued_join_request_t *request = *it;
        if (request->spot_state == state_ && !request->replied
            && std::find (pending_->begin (), pending_->end (), request)
                 == pending_->end ())
            pending_->push_back (request);
    }
}

void drain_queued_join_requests_for_stream_locked (
  void *stream_,
  std::deque<queued_join_request_t *> *aborted_)
{
    if (!stream_ || !aborted_)
        return;
    for (std::map<spot_logical_state_t *,
                  std::deque<queued_join_request_t *> >::iterator join_it =
           actor_runtime().joins.queues.begin ();
         join_it != actor_runtime().joins.queues.end (); ++join_it) {
        std::deque<queued_join_request_t *> &queue = join_it->second;
        for (std::deque<queued_join_request_t *>::iterator it =
               queue.begin ();
             it != queue.end ();) {
            queued_join_request_t *request = *it;
            if (request->actor && request->actor->bound_stream == stream_
                && !request->replied) {
                it = queue.erase (it);
                clear_actor_bound_session_locked (request->actor, true);
                clear_actor_joined_spot_locked (request->actor);
                retire_join_request_locked (request);
                aborted_->push_back (request);
                continue;
            }
            ++it;
        }
    }
}

void collect_received_join_requests_for_stream_locked (
  void *stream_,
  const std::deque<queued_join_request_t *> &already_aborted_,
  std::vector<queued_join_request_t *> *received_aborts_)
{
    if (!stream_ || !received_aborts_)
        return;
    for (std::set<queued_join_request_t *>::iterator it =
           actor_runtime().joins.live_requests.begin ();
         it != actor_runtime().joins.live_requests.end (); ++it) {
        queued_join_request_t *request = *it;
        if (request->actor && request->actor->bound_stream == stream_
            && !request->replied
            && std::find (already_aborted_.begin (), already_aborted_.end (),
                          request)
                 == already_aborted_.end ())
            received_aborts_->push_back (request);
    }
}

uint32_t pending_join_count_for_spot_locked (
  const std::shared_ptr<spot_logical_state_t> &state_)
{
    std::map<spot_logical_state_t *, size_t>::const_iterator pending_it =
      actor_runtime().joins.pending_count_by_spot.find (state_.get ());
    if (pending_it == actor_runtime().joins.pending_count_by_spot.end ())
        return 0;
    return static_cast<uint32_t> (pending_it->second);
}

void mark_join_request_live_locked (queued_join_request_t *request_)
{
    actor_runtime().joins.live_requests.insert (request_);
}

void unmark_join_request_live_locked (queued_join_request_t *request_)
{
    actor_runtime().joins.live_requests.erase (request_);
}

void track_pending_remote_actor_locked (queued_join_request_t *request_)
{
    if (request_ && request_->remote && request_->target_node
        && request_->target_actor_ref.actor_id[0] != '\0') {
        actor_runtime().joins.pending_remote_actor_keys.insert (
          std::make_pair (request_->target_node,
                          std::string (request_->target_actor_ref.actor_id)));
    }
}

void untrack_pending_remote_actor_locked (queued_join_request_t *request_)
{
    if (request_ && request_->remote && request_->target_node
        && request_->target_actor_ref.actor_id[0] != '\0') {
        actor_runtime().joins.pending_remote_actor_keys.erase (
          std::make_pair (request_->target_node,
                          std::string (request_->target_actor_ref.actor_id)));
    }
}

lifecycle_registration_t *find_lifecycle_registration_locked (
  spot_logical_state_t *key_)
{
    std::map<spot_logical_state_t *, lifecycle_registration_t>::iterator it =
      actor_runtime().lifecycle.handlers.find (key_);
    if (it == actor_runtime().lifecycle.handlers.end ())
        return NULL;
    return &it->second;
}

void clear_lifecycle_state_locked (spot_logical_state_t *key_)
{
    actor_runtime().lifecycle.handlers.erase (key_);
    actor_runtime().lifecycle.queues.erase (key_);
}

lifecycle_registration_t &ensure_lifecycle_registration_locked (
  spot_logical_state_t *key_)
{
    return actor_runtime().lifecycle.handlers[key_];
}

void enqueue_lifecycle_event_locked (spot_logical_state_t *key_,
                                     const lifecycle_event_t &event_)
{
    actor_runtime().lifecycle.queues[key_].push_back (event_);
}

bool pop_lifecycle_event_locked (spot_handle_t *spot_,
                                 lifecycle_event_t *event_out_,
                                 lifecycle_registration_t *registration_out_)
{
    if (!spot_ || !spot_->logical_state || !event_out_ || !registration_out_)
        return false;
    spot_logical_state_t *key = spot_->logical_state.get ();
    std::map<spot_logical_state_t *, std::deque<lifecycle_event_t> >::iterator
      queue_it = actor_runtime().lifecycle.queues.find (key);
    if (queue_it == actor_runtime().lifecycle.queues.end ()
        || queue_it->second.empty ())
        return false;
    *event_out_ = queue_it->second.front ();
    queue_it->second.pop_front ();
    if (queue_it->second.empty ())
        actor_runtime().lifecycle.queues.erase (queue_it);

    lifecycle_registration_t *registration =
      find_lifecycle_registration_locked (key);
    if (registration)
        *registration_out_ = *registration;
    else
        *registration_out_ = lifecycle_registration_t ();
    return true;
}

zlink_routing_id_t actor_current_spot_rid_locked (const actor_handle_t *actor_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    if (!actor_)
        return rid;
    if (actor_->joined_spot_state)
        return actor_->joined_spot_state->routing_id;
    return rid;
}

void set_actor_spot_locked (actor_handle_t *actor_, spot_handle_t *spot_)
{
    if (!actor_)
        return;
    actor_->joined_spot_state = spot_ ? spot_->logical_state
                                      : std::shared_ptr<spot_logical_state_t> ();
    actor_->last_changed_ms = now_ms ();
    update_active_route_locked (actor_);
}

void set_actor_entry_spot_locked (actor_handle_t *actor_)
{
    if (!actor_)
        return;
    actor_->joined_spot_state =
      zlink::spot_node_access_t::entry_spot_state (actor_->node);
    actor_->last_changed_ms = now_ms ();
    update_active_route_locked (actor_);
}

std::string routing_id_key (const zlink_routing_id_t &rid_)
{
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

bool actor_lifecycle_reenters_same_actor (
  const zlink_actor_ref_t *actor_ref_)
{
    if (!actor_ref_)
        return false;
    const actor_lifecycle_callback_context_t &context =
      actor_lifecycle_context ();
    return context.active
           && (same_actor_ref_identity (*actor_ref_, context.previous_actor)
               || same_actor_ref_identity (*actor_ref_,
                                           context.current_actor));
}

bool actor_lifecycle_reenters_same_spot (const spot_handle_t *spot_)
{
    const actor_lifecycle_callback_context_t &context =
      actor_lifecycle_context ();
    return context.active && same_logical_spot (spot_, context.spot);
}

bool is_stream_socket (void *stream_)
{
    zlink::socket_base_t *stream = try_as_socket (stream_);
    return stream && stream->socket_type () == ZLINK_CORE_SOCKET_STREAM;
}

void fill_ref (const actor_handle_t *actor_, zlink_actor_ref_t *out_)
{
    memset (out_, 0, sizeof (*out_));
    out_->node_rid = actor_->node_rid;
    strncpy (out_->actor_id, actor_->actor_id.c_str (), ZLINK_ACTOR_ID_MAX - 1);
    out_->generation = actor_->generation;
}

actor_handle_t *create_actor_locked_with_generation (
  zlink::spot_node_t *node_,
  const zlink_routing_id_t &node_rid_,
  const char *actor_id_,
  uint64_t generation_,
  bool pending_remote_join_ = false)
{
    std::map<std::string, actor_handle_t *> &actors =
      actors_by_id_locked (node_);
    if (actors.count (actor_id_) != 0) {
        errno = EBUSY;
        return NULL;
    }

    std::unique_ptr<actor_handle_t> actor (new (std::nothrow) actor_handle_t ());
    if (!actor) {
        errno = ENOMEM;
        return NULL;
    }

    actor->node = node_;
    actor->node_rid = node_rid_;
    actor->actor_id = actor_id_;
    actor->generation =
      generation_ == 0 ? next_generation_for_node_locked (node_) : generation_;
    actor->pending_remote_join = pending_remote_join_;
    actor->last_changed_ms = zlink::clock_t ().now_ms ();
    fill_ref (actor.get (), &actor->ref_cache);

    actor_handle_t *raw = actor.release ();
    actors[raw->actor_id] = raw;
    actor_handles_locked (node_).insert (raw);
    actor_runtime().nodes.nodes_by_rid[routing_id_key (node_rid_)] = node_;
    if (!pending_remote_join_)
        set_actor_entry_spot_locked (raw);
    return raw;
}

actor_handle_t *create_actor_locked (zlink::spot_node_t *node_,
                                     const zlink_routing_id_t &node_rid_,
                                     const char *actor_id_)
{
    return create_actor_locked_with_generation (node_, node_rid_, actor_id_,
                                                0);
}

actor_handle_t *as_actor_locked (void *actor_)
{
    actor_handle_t *actor = static_cast<actor_handle_t *> (actor_);
    if (!actor || !actor->check_tag () || !known_node_locked (actor->node)
        || actor_handles_locked (actor->node).count (actor) == 0)
        return NULL;
    return actor;
}

zlink::spot_node_t *resolve_node_by_rid_locked (
  const zlink_routing_id_t &rid_)
{
    const std::map<std::string, zlink::spot_node_t *>::iterator it =
      actor_runtime().nodes.nodes_by_rid.find (routing_id_key (rid_));
    return it == actor_runtime().nodes.nodes_by_rid.end () ? NULL : it->second;
}

bool known_node_locked (zlink::spot_node_t *node_)
{
    return node_ && actor_runtime().nodes.known_nodes.count (node_) != 0;
}

std::set<actor_handle_t *> &actor_handles_locked (zlink::spot_node_t *node_)
{
    return zlink::spot_node_access_t::actor_handles (node_);
}

std::map<std::string, actor_handle_t *> &actors_by_id_locked (
  zlink::spot_node_t *node_)
{
    return zlink::spot_node_access_t::actors_by_id (node_);
}

void collect_actor_handles_locked (std::vector<actor_handle_t *> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    for (std::set<zlink::spot_node_t *>::const_iterator node_it =
           actor_runtime().nodes.known_nodes.begin ();
         node_it != actor_runtime().nodes.known_nodes.end (); ++node_it) {
        const std::set<actor_handle_t *> &node_actors =
          actor_handles_locked (*node_it);
        out_->insert (out_->end (), node_actors.begin (), node_actors.end ());
    }
}

bool actor_route_disconnected_locked (zlink::spot_node_t *source_node_,
                                      const zlink_routing_id_t &target_rid_)
{
    if (!source_node_)
        return true;
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    if (source_node_->node_routing_id (&source_rid) == 0
        && same_routing_id (source_rid, target_rid_))
        return false;
    return actor_runtime().routes.disconnected.count (
             std::make_pair (source_node_, routing_id_key (target_rid_)))
           != 0;
}

actor_handle_t *resolve_actor_ref_locked (const zlink_actor_ref_t *ref_,
                                          bool include_pending_ = false)
{
    if (!ref_ || !valid_actor_id (ref_->actor_id)
        || !valid_routing_id (&ref_->node_rid))
        return NULL;

    zlink::spot_node_t *node = resolve_node_by_rid_locked (ref_->node_rid);
    if (!node) {
        errno = ENOENT;
        return NULL;
    }

    std::map<std::string, actor_handle_t *> &actors =
      actors_by_id_locked (node);
    const std::map<std::string, actor_handle_t *>::iterator actor_it =
      actors.find (ref_->actor_id);
    if (actor_it == actors.end ()
        || (!include_pending_ && actor_it->second->pending_remote_join)) {
        errno = ENOENT;
        return NULL;
    }

    actor_handle_t *actor = actor_it->second;
    if (ref_->generation != 0 && actor->generation != ref_->generation) {
        errno = ESTALE;
        return NULL;
    }
    return actor;
}

struct actor_resolution_t
{
    actor_resolution_t () :
        result (ZLINK_REQUEST_INTERNAL_ERROR),
        actor (NULL),
        owner (NULL)
    {
    }

    zlink_request_result_t result;
    actor_handle_t *actor;
    zlink::spot_node_t *owner;
};

actor_resolution_t resolve_actor_for_request_locked (
  zlink::spot_node_t *request_node_,
  const zlink_actor_ref_t *ref_,
  bool include_pending_ = false)
{
    actor_resolution_t resolved;
    if (!known_node_locked (request_node_)) {
        resolved.result = ZLINK_REQUEST_NOT_CONNECTED;
        return resolved;
    }

    resolved.owner =
      ref_ && valid_routing_id (&ref_->node_rid)
        ? resolve_node_by_rid_locked (ref_->node_rid)
        : NULL;
    if (!resolved.owner
        || actor_route_disconnected_locked (request_node_, ref_->node_rid)) {
        errno = ENOTCONN;
        resolved.result = ZLINK_REQUEST_NOT_CONNECTED;
        return resolved;
    }

    resolved.actor = resolve_actor_ref_locked (ref_, include_pending_);
    if (!resolved.actor) {
        resolved.result =
          actor_missing_request_result_from_errno ();
        return resolved;
    }

    resolved.result = ZLINK_REQUEST_OK;
    return resolved;
}

session_binding_key_t session_key (void *stream_,
                                   const zlink_routing_id_t *session_rid_)
{
    return session_binding_key_t (stream_, *session_rid_);
}

actor_session_state_t::binding_map_t::iterator find_session_binding_locked (
  void *stream_, const zlink_routing_id_t *session_rid_)
{
    return actor_runtime().sessions.bindings.find (
      session_key (stream_, session_rid_));
}

actor_session_state_t::binding_map_t::const_iterator
find_session_binding_locked (const void *stream_,
                             const zlink_routing_id_t *session_rid_)
{
    return actor_runtime().sessions.bindings.find (
      session_key (const_cast<void *> (stream_), session_rid_));
}

actor_session_state_t::binding_map_t::iterator session_bindings_end_locked ()
{
    return actor_runtime().sessions.bindings.end ();
}

actor_session_state_t::binding_map_t::const_iterator
session_bindings_end_const_locked ()
{
    return actor_runtime().sessions.bindings.end ();
}

session_binding_t &ensure_session_binding_locked (
  void *stream_, const zlink_routing_id_t &session_rid_)
{
    return actor_runtime().sessions.bindings[session_key (stream_,
                                                          &session_rid_)];
}

void erase_session_binding_locked (
  actor_session_state_t::binding_map_t::iterator binding_it_)
{
    actor_runtime().sessions.bindings.erase (binding_it_);
}

bool stream_has_session_binding_locked (void *stream_)
{
    if (!stream_)
        return false;
    for (actor_session_state_t::binding_map_t::const_iterator it =
           actor_runtime().sessions.bindings.begin ();
         it != session_bindings_end_const_locked (); ++it) {
        if (it->second.stream == stream_)
            return true;
    }
    return false;
}

void erase_session_bindings_for_stream_locked (void *stream_)
{
    if (!stream_)
        return;
    for (actor_session_state_t::binding_map_t::iterator it =
           actor_runtime().sessions.bindings.begin ();
         it != session_bindings_end_locked ();) {
        if (it->second.stream != stream_) {
            ++it;
            continue;
        }
        for (std::map<std::string, session_binding_t::actor_entry_t>::iterator
               actor_it = it->second.actors.begin ();
             actor_it != it->second.actors.end (); ++actor_it) {
            actor_handle_t *actor = actor_it->second.actor;
            if (actor && actor->bound_stream == stream_) {
                clear_actor_bound_session_locked (actor, true);
            }
        }
        it = actor_runtime().sessions.bindings.erase (it);
    }
}

zlink::spot_node_t *stream_owner_locked (void *stream_)
{
    zlink::socket_base_t *stream = try_as_socket (stream_);
    if (!stream)
        return NULL;
    std::map<void *, zlink::spot_node_t *>::const_iterator it =
      actor_runtime().sessions.stream_owners.find (stream_);
    if (it != actor_runtime().sessions.stream_owners.end ())
        return known_node_locked (it->second) ? it->second : NULL;
    for (std::set<zlink::spot_node_t *>::const_iterator node_it =
           actor_runtime().nodes.known_nodes.begin ();
         node_it != actor_runtime().nodes.known_nodes.end (); ++node_it) {
        if (zlink::spot_node_access_t::owns_socket (*node_it, stream)) {
            actor_runtime().sessions.stream_owners[stream_] = *node_it;
            return *node_it;
        }
    }
    return NULL;
}

void erase_stream_owner_if_unused_locked (void *stream_)
{
    if (!stream_)
        return;
    if (actor_runtime().sessions.explicit_stream_owners.count (stream_) != 0)
        return;
    if (stream_has_session_binding_locked (stream_))
        return;
    actor_runtime().sessions.stream_owners.erase (stream_);
}

uint64_t now_ms ()
{
    return zlink::clock_t ().now_ms ();
}

uint64_t next_generation_for_node_locked (zlink::spot_node_t *node_)
{
    uint64_t &next = zlink::spot_node_access_t::next_actor_generation (node_);
    if (next == 0)
        next = 1;
    return next++;
}

uint64_t next_commit_epoch_locked ()
{
    uint64_t epoch = actor_runtime().next_join_epoch++;
    if (actor_runtime().next_join_epoch == 0)
        actor_runtime().next_join_epoch = 1;
    return epoch == 0 ? actor_runtime().next_join_epoch++ : epoch;
}

bool active_route_matches_locked (const actor_handle_t *actor_)
{
    if (!actor_)
        return false;
    std::map<std::string, zlink_actor_route_t>::const_iterator it =
      actor_runtime().routes.active.find (actor_->actor_id);
    return it != actor_runtime().routes.active.end ()
           && same_routing_id (it->second.actor.node_rid, actor_->node_rid)
           && it->second.actor.generation == actor_->generation;
}

bool active_route_exists_locked (const actor_handle_t *actor_)
{
    return actor_
           && actor_runtime().routes.active.count (actor_->actor_id) != 0;
}

bool find_active_route_locked (const char *actor_id_,
                               zlink_actor_route_t *route_out_)
{
    std::map<std::string, zlink_actor_route_t>::const_iterator it =
      actor_runtime().routes.active.find (actor_id_);
    if (it == actor_runtime().routes.active.end ())
        return false;
    if (route_out_)
        *route_out_ = it->second;
    return true;
}

void publish_active_route_locked (actor_handle_t *actor_, bool create_)
{
    if (!actor_ || !actor_->node->actor_route_sync_enabled ())
        return;
    if (!create_ && !active_route_matches_locked (actor_))
        return;
    zlink_actor_route_t route;
    memset (&route, 0, sizeof (route));
    fill_ref (actor_, &route.actor);
    if (actor_->joined_spot_state) {
        route.current_spot_rid = actor_->joined_spot_state->routing_id;
        route.current_spot_kind =
          spot_kind_for_state (actor_->joined_spot_state);
    }
    actor_runtime().routes.active[actor_->actor_id] = route;
    (void) actor_->node->bind_actor_route (actor_->actor_id.c_str (),
                                           &route, sizeof (route));
}

void create_active_route_locked (actor_handle_t *actor_)
{
    publish_active_route_locked (actor_, true);
}

void update_active_route_locked (actor_handle_t *actor_)
{
    publish_active_route_locked (actor_, false);
}

void remove_matching_active_route_locked (actor_handle_t *actor_)
{
    if (!actor_)
        return;
    std::map<std::string, zlink_actor_route_t>::iterator it =
      actor_runtime().routes.active.find (actor_->actor_id);
    if (it == actor_runtime().routes.active.end ())
        return;
    if (same_routing_id (it->second.actor.node_rid, actor_->node_rid)
        && it->second.actor.generation == actor_->generation) {
        actor_runtime().routes.active.erase (it);
        if (actor_->node->actor_route_sync_enabled ())
            (void) actor_->node->unbind_actor_route (actor_->actor_id.c_str ());
    }
}

void clear_actor_bound_session_locked (actor_handle_t *actor_,
                                       bool update_changed_time_)
{
    if (!actor_)
        return;
    actor_->bound_session_node = NULL;
    actor_->bound_stream = NULL;
    memset (&actor_->bound_session_rid, 0, sizeof (actor_->bound_session_rid));
    if (update_changed_time_)
        actor_->last_changed_ms = now_ms ();
}

void clear_actor_joined_spot_locked (actor_handle_t *actor_)
{
    if (!actor_)
        return;
    set_actor_entry_spot_locked (actor_);
}

std::unique_ptr<actor_handle_t> remove_actor_locked (
  actor_handle_t *actor_, bool erase_session_binding_ = true)
{
    if (!actor_)
        return std::unique_ptr<actor_handle_t> ();

    if (actor_->bound_stream) {
        void *bound_stream = actor_->bound_stream;
        actor_session_state_t::binding_map_t::iterator binding_it =
          find_session_binding_locked (actor_->bound_stream,
                                       &actor_->bound_session_rid);
        if (binding_it != session_bindings_end_locked ()) {
            if (erase_session_binding_) {
                binding_it->second.actors.erase (actor_->actor_id);
                if (binding_it->second.actors.empty ()) {
                    erase_session_binding_locked (binding_it);
                    erase_stream_owner_if_unused_locked (bound_stream);
                }
            } else {
                std::map<std::string, session_binding_t::actor_entry_t>::iterator
                  entry_it = binding_it->second.actors.find (actor_->actor_id);
                if (entry_it != binding_it->second.actors.end ())
                    entry_it->second.actor = NULL;
            }
        }
        clear_actor_bound_session_locked (actor_, false);
    }

    if (known_node_locked (actor_->node))
        actors_by_id_locked (actor_->node).erase (actor_->actor_id);
    remove_matching_active_route_locked (actor_);
    if (known_node_locked (actor_->node))
        actor_handles_locked (actor_->node).erase (actor_);
    actor_->tag = 0;
    return std::unique_ptr<actor_handle_t> (actor_);
}

void notify_actor_readable (actor_handle_t *actor_)
{
    spot_handle_t *dispatch_spot =
      actor_ && actor_->joined_spot_state
        ? find_spot_facade_for_state_locked (actor_->node,
                                             actor_->joined_spot_state)
        : NULL;
    if (dispatch_spot)
        zlink_spot_notify_dispatch_info (
          dispatch_spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE,
          ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR, &actor_->ref_cache);
}

void complete_join_request (queued_join_request_t *request_,
                            zlink_request_result_t result_)
{
    if (!request_ || !request_->handler)
        return;

    zlink_actor_join_result_t result;
    memset (&result, 0, sizeof (result));
    result.result = result_;
    if (result_ == ZLINK_REQUEST_OK) {
        if (request_->remote)
            result.actor = request_->target_actor_ref;
        else if (request_->actor)
            fill_ref (request_->actor, &result.actor);
        if (request_->spot_state)
            result.joined_spot_rid = request_->spot_state->routing_id;
        result.join_epoch = request_->join_epoch;
    }

    if (!request_->reply_parts.empty ()) {
        std::vector<zlink_msg_t> reply_parts;
        if (zlink::spot_move_msg_parts (&request_->reply_parts, &reply_parts)
            != 0) {
            result.result = ZLINK_REQUEST_INTERNAL_ERROR;
            request_->handler (&result, NULL, 0,
                               request_->userdata);
            return;
        }
        request_->handler (
          &result, reply_parts.empty () ? NULL : &reply_parts[0],
          reply_parts.size (), request_->userdata);
    } else {
        request_->handler (&result, NULL, 0, request_->userdata);
    }
}

zlink_submit_result_t complete_immediate_join_result (
  zlink_msg_t *parts_, size_t part_count_, zlink_actor_join_handler_fn handler_,
  void *userdata_,
  zlink_request_result_t result_)
{
    consume_multipart_payload (parts_, part_count_);
    zlink_actor_join_result_t result;
    memset (&result, 0, sizeof (result));
    result.result = result_;
    handler_ (&result, NULL, 0, userdata_);
    return ZLINK_SUBMIT_OK;
}

struct idempotent_join_completion_t
{
    zlink_actor_join_handler_fn handler;
    void *userdata;
    zlink_actor_join_result_t result;
};

void complete_idempotent_join_scheduled (void *userdata_)
{
    idempotent_join_completion_t *completion =
      static_cast<idempotent_join_completion_t *> (userdata_);
    if (!completion)
        return;
    completion->handler (&completion->result, NULL, 0, completion->userdata);
    delete completion;
}

void cleanup_idempotent_join_completion (void *userdata_)
{
    delete static_cast<idempotent_join_completion_t *> (userdata_);
}

void schedule_lifecycle_event_locked (
  const std::shared_ptr<spot_logical_state_t> &spot_state_,
  bool join_,
  const zlink_spot_actor_lifecycle_info_t &info_)
{
    if (!spot_state_)
        return;
    lifecycle_registration_t *registration =
      find_lifecycle_registration_locked (spot_state_.get ());
    if (!registration)
        return;
    zlink_spot_actor_lifecycle_handler_fn handler =
      join_ ? registration->on_join : registration->on_leave;
    if (!handler)
        return;
    spot_handle_t *spot =
      find_spot_facade_for_state_locked (spot_state_->node, spot_state_);
    if (!spot)
        return;
    lifecycle_event_t event;
    event.join = join_;
    event.info = info_;
    enqueue_lifecycle_event_locked (spot_state_.get (), event);
    zlink::spot_runtime_t *runtime =
      zlink::spot_reqrep_internal::resolve_spot_runtime (spot);
    if (runtime)
        (void) runtime->post_dispatch_event (spot);
}

zlink_spot_actor_lifecycle_info_t make_lifecycle_info (
  const zlink_actor_ref_t &previous_actor_,
  const zlink_actor_ref_t &current_actor_,
  const zlink_routing_id_t &previous_spot_rid_,
  const zlink_routing_id_t &current_spot_rid_,
  uint64_t join_epoch_)
{
    zlink_spot_actor_lifecycle_info_t info;
    memset (&info, 0, sizeof (info));
    info.previous_actor = previous_actor_;
    info.current_actor = current_actor_;
    info.previous_spot_rid = previous_spot_rid_;
    info.current_spot_rid = current_spot_rid_;
    info.join_epoch = join_epoch_;
    return info;
}

zlink_request_result_t bind_actor_to_session_locked (
  zlink::spot_node_t *stream_owner_,
  void *stream_,
  const zlink_routing_id_t &session_rid_,
  actor_handle_t *actor_)
{
    if (!stream_owner_ || !stream_ || !actor_) {
        errno = EFSM;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    if (actor_->bound_stream
        && (actor_->bound_stream != stream_
            || !same_routing_id (actor_->bound_session_rid, session_rid_))) {
        errno = EBUSY;
        return ZLINK_REQUEST_BUSY;
    }

    session_binding_t &binding =
      ensure_session_binding_locked (stream_, session_rid_);
    binding.stream = stream_;
    binding.session_rid = session_rid_;
    std::map<std::string, session_binding_t::actor_entry_t>::iterator
      previous = binding.actors.find (actor_->actor_id);
    if (previous != binding.actors.end () && previous->second.actor
        && previous->second.actor != actor_)
        clear_actor_bound_session_locked (previous->second.actor, true);

    session_binding_t::actor_entry_t entry;
    entry.actor = actor_;
    fill_ref (actor_, &entry.ref);
    binding.actors[actor_->actor_id] = entry;
    actor_->bound_session_node = stream_owner_;
    actor_->bound_stream = stream_;
    actor_->bound_session_rid = session_rid_;
    actor_->last_changed_ms = now_ms ();
    return ZLINK_REQUEST_OK;
}

zlink_request_result_t unbind_actor_from_session_locked (
  zlink::spot_node_t *stream_owner_,
  void *stream_,
  const zlink_routing_id_t &session_rid_,
  const char *actor_id_)
{
    if (!stream_owner_ || !stream_ || !actor_id_) {
        errno = EFSM;
        return ZLINK_REQUEST_INVALID_STATE;
    }

    actor_session_state_t::binding_map_t::iterator binding_it =
      find_session_binding_locked (stream_, &session_rid_);
    if (binding_it == session_bindings_end_locked ())
        return ZLINK_REQUEST_OK;
    session_binding_t &binding = binding_it->second;
    std::map<std::string, session_binding_t::actor_entry_t>::iterator it =
      binding.actors.find (actor_id_);
    if (it == binding.actors.end ())
        return ZLINK_REQUEST_OK;

    if (valid_routing_id (&it->second.ref.node_rid)
        && actor_route_disconnected_locked (stream_owner_,
                                            it->second.ref.node_rid)) {
        errno = ENOTCONN;
        return ZLINK_REQUEST_NOT_CONNECTED;
    }
    if (it->second.actor)
        clear_actor_bound_session_locked (it->second.actor, true);
    binding.actors.erase (it);
    if (binding.actors.empty ()) {
        erase_session_binding_locked (binding_it);
        erase_stream_owner_if_unused_locked (stream_);
    }
    return ZLINK_REQUEST_OK;
}

zlink_request_result_t commit_accepted_join_locked (
  queued_join_request_t *request_, actor_handle_t **readable_actor_out_)
{
    if (readable_actor_out_)
        *readable_actor_out_ = NULL;
    if (!request_)
        return ZLINK_REQUEST_INTERNAL_ERROR;

    zlink_spot_actor_lifecycle_info_t source_leave_info;
    zlink_spot_actor_lifecycle_info_t target_join_info;
    std::shared_ptr<spot_logical_state_t> source_leave_spot;
    std::shared_ptr<spot_logical_state_t> target_join_spot;

    if (request_->remote) {
        actor_handle_t *source = request_->actor;
        actor_handle_t *target = request_->pending_target;
        if (!source || !target || !target->pending_remote_join)
            return ZLINK_REQUEST_CONFLICT;
        void *source_bound_stream = source->bound_stream;
        zlink::spot_node_t *source_bound_session_node =
          source->bound_session_node;
        zlink_routing_id_t source_bound_session_rid =
          source->bound_session_rid;
        const bool transfer_bound_session =
          source_bound_stream && source_bound_session_node
          && valid_routing_id (&source_bound_session_rid);

        zlink_actor_ref_t source_ref;
        zlink_actor_ref_t target_ref;
        fill_ref (source, &source_ref);
        fill_ref (target, &target_ref);
        const uint64_t source_epoch = source->join_epoch;
        target->join_epoch = request_->join_epoch;
        source_leave_spot = source->joined_spot_state;
        source_leave_info = make_lifecycle_info (
          source_ref, target_ref, request_->source_spot_rid,
          request_->spot_state->routing_id, source_epoch);
        target_join_spot = request_->spot_state;
        target_join_info = make_lifecycle_info (
          source_ref, target_ref, request_->source_spot_rid,
          request_->spot_state->routing_id, target->join_epoch);
        target->pending_remote_join = false;
        set_actor_spot_locked (target, request_->spot);
        request_->pending_target = NULL;
        std::unique_ptr<actor_handle_t> retired =
          remove_actor_locked (source, false);
        LIBZLINK_UNUSED (retired);
        if (transfer_bound_session) {
            actor_session_state_t::binding_map_t::iterator binding_it =
              find_session_binding_locked (source_bound_stream,
                                           &source_bound_session_rid);
            if (binding_it != session_bindings_end_locked ()) {
                session_binding_t &binding = binding_it->second;
                session_binding_t::actor_entry_t &entry =
                  binding.actors[target->actor_id];
                entry.actor = target;
                fill_ref (target, &entry.ref);
                target->bound_session_node = source_bound_session_node;
                target->bound_stream = source_bound_stream;
                target->bound_session_rid = source_bound_session_rid;
                target->last_changed_ms = now_ms ();
            }
        }
        create_active_route_locked (target);
        if (readable_actor_out_ && !target->queue.empty ())
            *readable_actor_out_ = target;
    } else {
        if (!request_->actor)
            return ZLINK_REQUEST_CONFLICT;
        zlink_actor_ref_t actor_ref;
        fill_ref (request_->actor, &actor_ref);
        const uint64_t source_epoch = request_->actor->join_epoch;
        request_->actor->join_epoch = request_->join_epoch;
        source_leave_spot = request_->actor->joined_spot_state;
        source_leave_info = make_lifecycle_info (
          actor_ref, actor_ref, request_->source_spot_rid,
          request_->spot_state->routing_id, source_epoch);
        target_join_spot = request_->spot_state;
        target_join_info = make_lifecycle_info (
          actor_ref, actor_ref, request_->source_spot_rid,
          request_->spot_state->routing_id, request_->actor->join_epoch);
        set_actor_spot_locked (request_->actor, request_->spot);
        create_active_route_locked (request_->actor);
        if (readable_actor_out_ && !request_->actor->queue.empty ())
            *readable_actor_out_ = request_->actor;
    }

    schedule_lifecycle_event_locked (source_leave_spot, false,
                                     source_leave_info);
    schedule_lifecycle_event_locked (target_join_spot, true,
                                     target_join_info);
    return ZLINK_REQUEST_OK;
}

struct actor_lookup_operation_arg_t
{
    zlink::spot_node_t *request_node;
    zlink_routing_id_t target_node_rid;
    char actor_id[ZLINK_ACTOR_ID_MAX];
};

void cleanup_actor_lookup_operation_arg (void *arg_)
{
    delete static_cast<actor_lookup_operation_arg_t *> (arg_);
}

void run_actor_lookup_operation (void *arg_,
                                 zlink_actor_lookup_result_t *out_)
{
    actor_lookup_operation_arg_t *arg =
      static_cast<actor_lookup_operation_arg_t *> (arg_);
    memset (out_, 0, sizeof (*out_));
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    if (!arg) {
        out_->result = ZLINK_REQUEST_NOT_CONNECTED;
        return;
    }

    zlink_actor_ref_t probe;
    memset (&probe, 0, sizeof (probe));
    probe.node_rid = arg->target_node_rid;
    strncpy (probe.actor_id, arg->actor_id, ZLINK_ACTOR_ID_MAX - 1);
    const actor_resolution_t resolved =
      resolve_actor_for_request_locked (arg->request_node, &probe);
    if (resolved.result != ZLINK_REQUEST_OK) {
        out_->result = resolved.result;
        if (resolved.result == ZLINK_REQUEST_NOT_CONNECTED)
            errno = ENOTCONN;
        return;
    }
    out_->result = ZLINK_REQUEST_OK;
    fill_ref (resolved.actor, &out_->actor);
}

struct actor_reply_operation_arg_t
{
    actor_reply_operation_arg_t () :
        run (NULL),
        request_node (NULL),
        stream (NULL)
    {
        memset (&actor, 0, sizeof (actor));
        memset (&rid, 0, sizeof (rid));
        memset (actor_id, 0, sizeof (actor_id));
    }

    zlink_request_result_t (*run) (actor_reply_operation_arg_t *);
    zlink::spot_node_t *request_node;
    void *stream;
    zlink_actor_ref_t actor;
    zlink_routing_id_t rid;
    char actor_id[ZLINK_ACTOR_ID_MAX];
};

void cleanup_actor_reply_operation_arg (void *arg_)
{
    delete static_cast<actor_reply_operation_arg_t *> (arg_);
}

zlink_request_result_t run_destroy_operation_locked (
  actor_reply_operation_arg_t *arg_)
{
    if (!arg_)
        return ZLINK_REQUEST_NOT_CONNECTED;
    const actor_resolution_t resolved =
      resolve_actor_for_request_locked (arg_->request_node, &arg_->actor);
    if (resolved.result != ZLINK_REQUEST_OK)
        return resolved.result;
    actor_handle_t *actor = resolved.actor;
    if (actor_in_user_spot_locked (actor)) {
        errno = EBUSY;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    if (actor_has_pending_join_locked (actor)) {
        errno = EBUSY;
        return ZLINK_REQUEST_BUSY;
    }
    if (actor->bound_stream) {
        actor_session_state_t::binding_map_t::const_iterator binding_it =
          find_session_binding_locked (actor->bound_stream,
                                       &actor->bound_session_rid);
        if (binding_it != session_bindings_end_const_locked ()
            && binding_it->second.in_progress
            && binding_it->second.in_progress_actor_id == actor->actor_id) {
            errno = EBUSY;
            return ZLINK_REQUEST_BUSY;
        }
    }

    zlink_actor_ref_t previous_actor;
    zlink_actor_ref_t zero_actor;
    zlink_routing_id_t zero_spot;
    memset (&zero_actor, 0, sizeof (zero_actor));
    memset (&zero_spot, 0, sizeof (zero_spot));
    fill_ref (actor, &previous_actor);
    std::shared_ptr<spot_logical_state_t> lifecycle_spot =
      actor->joined_spot_state;
    const zlink_spot_actor_lifecycle_info_t lifecycle_info =
      make_lifecycle_info (previous_actor, zero_actor,
                           actor_current_spot_rid_locked (actor), zero_spot,
                           actor->join_epoch);
    std::unique_ptr<actor_handle_t> actor_to_delete =
      remove_actor_locked (actor);
    LIBZLINK_UNUSED (actor_to_delete);
    schedule_lifecycle_event_locked (lifecycle_spot, false, lifecycle_info);
    return ZLINK_REQUEST_OK;
}

zlink_request_result_t run_leave_operation_locked (
  actor_reply_operation_arg_t *arg_)
{
    if (!arg_)
        return ZLINK_REQUEST_NOT_CONNECTED;
    const actor_resolution_t resolved =
      resolve_actor_for_request_locked (arg_->request_node, &arg_->actor);
    if (resolved.result != ZLINK_REQUEST_OK)
        return resolved.result;
    actor_handle_t *actor = resolved.actor;
    if (actor_has_pending_join_locked (actor)) {
        errno = EBUSY;
        return ZLINK_REQUEST_BUSY;
    }
    if (!actor->joined_spot_state
        || !same_routing_id (actor_current_spot_rid_locked (actor),
                             arg_->rid)) {
        errno = ESTALE;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    if (actor_in_entry_spot_locked (actor)) {
        if (!active_route_matches_locked (actor)
            && active_route_exists_locked (actor))
            create_active_route_locked (actor);
        return ZLINK_REQUEST_OK;
    }

    zlink_actor_ref_t actor_ref;
    fill_ref (actor, &actor_ref);
    const zlink_routing_id_t previous_spot =
      actor_current_spot_rid_locked (actor);
    const uint64_t previous_epoch = actor->join_epoch;
    const uint64_t epoch = next_commit_epoch_locked ();
    std::shared_ptr<spot_logical_state_t> source_spot =
      actor->joined_spot_state;
    const zlink_spot_actor_lifecycle_info_t leave_info =
      make_lifecycle_info (actor_ref, actor_ref, previous_spot,
                           zlink::spot_node_access_t::entry_spot_state (
                             actor->node)
                             ->routing_id,
                           previous_epoch);
    actor->join_epoch = epoch;
    set_actor_entry_spot_locked (actor);
    const zlink_routing_id_t entry_spot =
      actor_current_spot_rid_locked (actor);
    const zlink_spot_actor_lifecycle_info_t join_info =
      make_lifecycle_info (actor_ref, actor_ref, previous_spot, entry_spot,
                           epoch);
    schedule_lifecycle_event_locked (source_spot, false, leave_info);
    schedule_lifecycle_event_locked (actor->joined_spot_state, true, join_info);
    return ZLINK_REQUEST_OK;
}

zlink_request_result_t run_bind_operation_locked (
  actor_reply_operation_arg_t *arg_)
{
    if (!arg_)
        return ZLINK_REQUEST_INVALID_STATE;
    zlink::spot_node_t *stream_owner = stream_owner_locked (arg_->stream);
    if (!stream_owner) {
        errno = EFSM;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    const actor_resolution_t resolved =
      resolve_actor_for_request_locked (stream_owner, &arg_->actor);
    if (resolved.result != ZLINK_REQUEST_OK)
        return resolved.result;
    actor_handle_t *actor = resolved.actor;
    return bind_actor_to_session_locked (stream_owner, arg_->stream, arg_->rid,
                                         actor);
}

zlink_request_result_t run_unbind_operation_locked (
  actor_reply_operation_arg_t *arg_)
{
    if (!arg_)
        return ZLINK_REQUEST_INVALID_STATE;
    zlink::spot_node_t *stream_owner = stream_owner_locked (arg_->stream);
    if (!stream_owner) {
        errno = EFSM;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    return unbind_actor_from_session_locked (stream_owner, arg_->stream,
                                             arg_->rid, arg_->actor_id);
}

zlink_request_result_t run_actor_reply_operation (void *arg_)
{
    actor_reply_operation_arg_t *arg =
      static_cast<actor_reply_operation_arg_t *> (arg_);
    if (!arg)
        return ZLINK_REQUEST_INTERNAL_ERROR;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    if (!arg->run)
        return ZLINK_REQUEST_INTERNAL_ERROR;
    return arg->run (arg);
}

void drain_lifecycle_events_for_spot (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->logical_state)
        return;

    for (;;) {
        lifecycle_event_t event;
        lifecycle_registration_t registration;
        void *userdata = NULL;
        {
            std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
            if (!spot_->logical_state)
                return;
            if (!pop_lifecycle_event_locked (spot_, &event, &registration))
                return;
            userdata = registration.userdata;
        }

        zlink_spot_actor_lifecycle_handler_fn handler =
          event.join ? registration.on_join : registration.on_leave;
        if (!handler)
            continue;

        const zlink::spot_dispatch_event_callback_context_t dispatch_scope (
          spot_);
        actor_lifecycle_handler_scope_t lifecycle_scope (spot_, event.info);
        handler (spot_, &event.info, userdata);
    }
}

zlink_submit_result_t complete_idempotent_join_async (
  zlink_msg_t *parts_, size_t part_count_, zlink_actor_join_handler_fn handler_,
  void *userdata_, const zlink_actor_ref_t *actor_,
  const zlink_routing_id_t *spot_rid_, uint64_t join_epoch_)
{
    consume_multipart_payload (parts_, part_count_);
    idempotent_join_completion_t *completion =
      new (std::nothrow) idempotent_join_completion_t;
    if (!completion) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    completion->handler = handler_;
    completion->userdata = userdata_;
    memset (&completion->result, 0, sizeof (completion->result));
    completion->result.result = ZLINK_REQUEST_OK;
    if (actor_)
        completion->result.actor = *actor_;
    if (spot_rid_)
        completion->result.joined_spot_rid = *spot_rid_;
    completion->result.join_epoch = join_epoch_;
    (void) zlink::request_timeout::schedule (
      1, complete_idempotent_join_scheduled, completion,
      cleanup_idempotent_join_completion);
    return ZLINK_SUBMIT_OK;
}

void increment_size_index (std::map<actor_handle_t *, size_t> *index_,
                           actor_handle_t *key_)
{
    if (!index_ || !key_)
        return;
    ++(*index_)[key_];
}

void decrement_size_index (std::map<actor_handle_t *, size_t> *index_,
                           actor_handle_t *key_)
{
    if (!index_ || !key_)
        return;
    std::map<actor_handle_t *, size_t>::iterator it = index_->find (key_);
    if (it == index_->end ())
        return;
    if (it->second <= 1)
        index_->erase (it);
    else
        --it->second;
}

void increment_spot_index (spot_logical_state_t *key_)
{
    if (key_)
        ++actor_runtime().joins.pending_count_by_spot[key_];
}

void decrement_spot_index (spot_logical_state_t *key_)
{
    if (!key_)
        return;
    std::map<spot_logical_state_t *, size_t>::iterator it =
      actor_runtime().joins.pending_count_by_spot.find (key_);
    if (it == actor_runtime().joins.pending_count_by_spot.end ())
        return;
    if (it->second <= 1)
        actor_runtime().joins.pending_count_by_spot.erase (it);
    else
        --it->second;
}

void index_join_request_locked (queued_join_request_t *request_)
{
    if (!request_ || request_->indexed || request_->replied)
        return;

    mark_join_request_live_locked (request_);
    increment_size_index (&actor_runtime().joins.pending_count_by_actor,
                          request_->actor);
    increment_spot_index (join_queue_key (request_));
    track_pending_remote_actor_locked (request_);
    request_->indexed = true;
}

void unindex_join_request_locked (queued_join_request_t *request_)
{
    if (!request_ || !request_->indexed)
        return;

    unmark_join_request_live_locked (request_);
    decrement_size_index (&actor_runtime().joins.pending_count_by_actor,
                          request_->actor);
    decrement_spot_index (join_queue_key (request_));
    untrack_pending_remote_actor_locked (request_);
    request_->indexed = false;
}

void retire_join_request_locked (queued_join_request_t *request_)
{
    if (!request_)
        return;
    unindex_join_request_locked (request_);
    if (request_->pending_target
        && request_->pending_target->pending_remote_join) {
        std::unique_ptr<actor_handle_t> pending =
          remove_actor_locked (request_->pending_target, false);
        request_->pending_target = NULL;
    }
    zlink::spot_clear_msg_parts (&request_->message_parts);
    request_->replied = true;
}

void release_join_request_after_completion (queued_join_request_t *request_)
{
    if (!request_)
        return;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        unindex_join_request_locked (request_);
        timeout_task.swap (request_->timeout_task);
    }
    zlink::request_timeout::cancel (timeout_task);
    delete request_;
}

void remove_pending_join_request_locked (queued_join_request_t *request_)
{
    spot_logical_state_t *key = join_queue_key (request_);
    if (!request_ || !key)
        return;
    std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >::
      iterator queue_it;
    if (!find_join_queue_locked (key, &queue_it))
        return;
    std::deque<queued_join_request_t *> &queue = queue_it->second;
    for (std::deque<queued_join_request_t *>::iterator it = queue.begin ();
         it != queue.end (); ++it) {
        if (*it == request_) {
            queue.erase (it);
            break;
        }
    }
    erase_join_queue_if_empty_locked (key);
}

void handle_join_timeout (void *userdata_)
{
    queued_join_request_t *request =
      static_cast<queued_join_request_t *> (userdata_);
    if (!request)
        return;
    bool timed_out = false;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        if (join_request_live_locked (request) && !request->replied) {
            remove_pending_join_request_locked (request);
            retire_join_request_locked (request);
            request->timeout_task.reset ();
            timed_out = true;
        }
    }
    if (!timed_out)
        return;
    complete_join_request (request, ZLINK_REQUEST_TIMED_OUT);
    delete request;
}

void schedule_join_timeout (queued_join_request_t *request_,
                            uint32_t timeout_ms_)
{
    if (!request_ || timeout_ms_ == 0)
        return;
    request_->timeout_task =
      zlink::request_timeout::schedule (timeout_ms_, handle_join_timeout,
                                        request_);
}

bool spot_dispatch_handler_attached (const spot_handle_t *spot_)
{
    if (!spot_ || !spot_->logical_state
        || !spot_->logical_state->request_reply_state)
        return false;
    std::lock_guard<std::mutex> lock (
      spot_->logical_state->request_reply_state->dispatch.mutex);
    return spot_->logical_state->request_reply_state->dispatch.handler != NULL;
}

zlink_submit_result_t validate_actor_bound_session_locked (
  actor_handle_t *actor_, void **stream_out_, zlink_routing_id_t *rid_out_)
{
    if (!actor_ || !stream_out_ || !rid_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!actor_->bound_stream) {
        errno = ENOENT;
        return ZLINK_SUBMIT_NOT_FOUND;
    }
    if (!known_node_locked (actor_->bound_session_node)) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }

    actor_session_state_t::binding_map_t::iterator binding_it =
      find_session_binding_locked (actor_->bound_stream,
                                   &actor_->bound_session_rid);
    if (binding_it == session_bindings_end_locked ()) {
        clear_actor_bound_session_locked (actor_, false);
        errno = ENOENT;
        return ZLINK_SUBMIT_NOT_FOUND;
    }

    std::map<std::string, session_binding_t::actor_entry_t>::const_iterator
      actor_it =
      binding_it->second.actors.find (actor_->actor_id);
    if (actor_it == binding_it->second.actors.end ()
        || actor_it->second.actor != actor_
        || actor_it->second.ref.generation != actor_->generation) {
        errno = ESTALE;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    *stream_out_ = actor_->bound_stream;
    *rid_out_ = actor_->bound_session_rid;
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t enqueue_bound_actor_part_locked (
  zlink::spot_node_t *stream_owner_,
  session_binding_t *binding_,
  session_binding_t::actor_entry_t *entry_,
  const zlink_routing_id_t *session_rid_,
  const char *actor_id_,
  zlink_msg_t *part_,
  zlink_part_flag_t part_flag_,
  actor_handle_t **actor_out_)
{
    if (actor_out_)
        *actor_out_ = NULL;
    if (!stream_owner_ || !binding_ || !entry_ || !session_rid_ || !actor_id_
        || !part_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (binding_->in_progress
        && binding_->in_progress_actor_id != actor_id_) {
        errno = EFSM;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    const actor_resolution_t resolved =
      resolve_actor_for_request_locked (stream_owner_, &entry_->ref, true);
    if (resolved.result != ZLINK_REQUEST_OK)
        return request_result_to_submit_result (resolved.result);
    actor_handle_t *actor = resolved.actor;
    entry_->actor = actor;

    zlink_routing_id_t source_node_rid;
    memset (&source_node_rid, 0, sizeof (source_node_rid));
    (void) stream_owner_->node_routing_id (&source_node_rid);

    queued_actor_part_t queued;
    fill_ref (actor, &queued.info.actor);
    queued.info.source_node_rid = source_node_rid;
    queued.info.source_session_rid = *session_rid_;
    queued.part_flag = part_flag_;
    if (zlink_msg_adopt (&queued.part, part_) != ZLINK_CONFIG_OK)
        return ZLINK_SUBMIT_INTERNAL_ERROR;
    queued.owns = true;
    actor->queue.push_back (std::move (queued));
    actor->last_changed_ms = now_ms ();
    if (part_flag_ == ZLINK_PART_MORE) {
        binding_->in_progress = true;
        binding_->in_progress_actor_id = actor_id_;
    } else {
        binding_->in_progress = false;
        binding_->in_progress_actor_id.clear ();
    }
    if (actor_out_)
        *actor_out_ = actor;
    return ZLINK_SUBMIT_OK;
}

}

void zlink_actor_run_lifecycle_for_spot (void *spot_)
{
    drain_lifecycle_events_for_spot (as_spot_handle (spot_));
}

extern "C" zlink_config_result_t zlink_spot_node_actor_new (
  void *node_, const char *actor_id_, zlink_actor_ref_t *actor_out_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!valid_actor_id (actor_id_) || !actor_out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->routed_enabled ()) {
        errno = ENOTSUP;
        return ZLINK_CONFIG_NOT_SUPPORTED;
    }

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node->node_routing_id (&node_rid) != 0)
        return zlink::config_result_internal::from_errno (errno);

    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_handle_t *actor = create_actor_locked (node, node_rid, actor_id_);
    if (!actor)
        return zlink::config_result_internal::from_errno (errno);
    fill_ref (actor, actor_out_);
    actor->join_epoch = next_commit_epoch_locked ();
    create_active_route_locked (actor);
    zlink_actor_ref_t zero_actor;
    zlink_routing_id_t zero_spot;
    memset (&zero_actor, 0, sizeof (zero_actor));
    memset (&zero_spot, 0, sizeof (zero_spot));
    const zlink_spot_actor_lifecycle_info_t info = make_lifecycle_info (
      zero_actor, *actor_out_, zero_spot, actor_current_spot_rid_locked (actor),
      actor->join_epoch);
    schedule_lifecycle_event_locked (actor->joined_spot_state, true, info);
    return ZLINK_CONFIG_OK;
}

void register_actor_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_runtime().nodes.known_spots.insert (spot_);
}

void register_actor_spot_node (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node_->node_routing_id (&node_rid) != 0)
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_runtime().nodes.known_nodes.insert (node_);
    actor_runtime().nodes.nodes_by_rid[routing_id_key (node_rid)] = node_;
}

void erase_actor_spot_node (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    std::vector<std::unique_ptr<actor_handle_t> > actors_to_delete;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        for (std::set<std::pair<zlink::spot_node_t *, std::string> >::iterator
               it = actor_runtime().routes.disconnected.begin ();
             it != actor_runtime().routes.disconnected.end ();) {
            if (it->first == node_)
                it = actor_runtime().routes.disconnected.erase (it);
            else
                ++it;
        }
        for (std::map<std::string, zlink::spot_node_t *>::iterator it =
               actor_runtime().nodes.nodes_by_rid.begin ();
             it != actor_runtime().nodes.nodes_by_rid.end ();) {
            if (it->second == node_)
                it = actor_runtime().nodes.nodes_by_rid.erase (it);
            else
                ++it;
        }
        while (!actors_by_id_locked (node_).empty ()) {
            actor_handle_t *actor = actors_by_id_locked (node_).begin ()->second;
            actors_to_delete.push_back (remove_actor_locked (actor, false));
        }
        for (std::map<void *, zlink::spot_node_t *>::iterator it =
               actor_runtime().sessions.stream_owners.begin ();
             it != actor_runtime().sessions.stream_owners.end ();) {
            if (it->second == node_) {
                actor_runtime().sessions.explicit_stream_owners.erase (it->first);
                it = actor_runtime().sessions.stream_owners.erase (it);
            } else {
                ++it;
            }
        }
        actor_runtime().nodes.known_nodes.erase (node_);
    }
}

void note_actor_spot_node_peer_disconnected (
  zlink::spot_node_t *node_, const zlink_routing_id_t *target_node_rid_)
{
    if (!node_ || !valid_routing_id (target_node_rid_))
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_runtime().routes.disconnected.insert (
      std::make_pair (node_, routing_id_key (*target_node_rid_)));
}

void erase_actor_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    std::deque<queued_join_request_t *> pending;
    const std::shared_ptr<spot_logical_state_t> logical_state =
      spot_->logical_state;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        spot_handle_t *replacement = NULL;
        for (std::set<spot_handle_t *>::iterator it = actor_runtime().nodes.known_spots.begin ();
             it != actor_runtime().nodes.known_spots.end (); ++it) {
            if (*it != spot_ && same_logical_spot (*it, spot_)) {
                replacement = *it;
                break;
            }
        }
        actor_runtime().nodes.known_spots.erase (spot_);
        if (replacement) {
            replace_live_join_spot_locked (spot_, replacement);
            return;
        }
        spot_logical_state_t *key = join_queue_key (logical_state);
        if (logical_state && logical_state->entry) {
            (void) key;
            replace_live_join_spot_locked (spot_, NULL);
            clear_lifecycle_state_locked (logical_state.get ());
            return;
        }
        clear_lifecycle_state_locked (logical_state.get ());
        take_join_queue_locked (key, &pending);
        collect_live_join_requests_for_state_locked (logical_state, &pending);
    }
    for (std::deque<queued_join_request_t *>::iterator it = pending.begin ();
         it != pending.end (); ++it) {
        {
            std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
            retire_join_request_locked (*it);
        }
        complete_join_request (*it, ZLINK_REQUEST_TERMINATED);
        release_join_request_after_completion (*it);
    }
}

extern "C" int zlink_spot_has_joined_or_pending_actor (void *spot_)
{
    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    if (!spot)
        return 0;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    if (spot->logical_state && spot->logical_state->entry)
        return 0;
    for (std::set<spot_handle_t *>::const_iterator it = actor_runtime().nodes.known_spots.begin ();
         it != actor_runtime().nodes.known_spots.end (); ++it) {
        if (*it != spot && same_logical_spot (*it, spot))
            return 0;
    }
    std::vector<actor_handle_t *> actors;
    collect_actor_handles_locked (&actors);
    for (std::vector<actor_handle_t *>::const_iterator it = actors.begin ();
         it != actors.end (); ++it) {
        if ((*it)->joined_spot_state
            && (*it)->joined_spot_state == spot->logical_state)
            return 1;
    }
    if (spot_has_pending_join_locked (join_queue_key (spot->logical_state)))
        return 1;
    return 0;
}

extern "C" void zlink_actor_replay_readable_for_spot (void *spot_)
{
    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    if (!spot || !spot->logical_state)
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    std::vector<actor_handle_t *> actors;
    collect_actor_handles_locked (&actors);
    for (std::vector<actor_handle_t *>::const_iterator it = actors.begin ();
         it != actors.end (); ++it) {
        actor_handle_t *actor = *it;
        if (!actor->pending_remote_join && actor->joined_spot_state
            && actor->joined_spot_state == spot->logical_state
            && !actor->queue.empty ()) {
            zlink_spot_notify_dispatch_info (
              spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE,
              ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR, &actor->ref_cache);
        }
    }
}

namespace zlink
{
namespace spot_actor_internal
{
int node_has_any_actor (void *node_)
{
    if (!node_)
        return 0;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    return actors_by_id_locked (static_cast<zlink::spot_node_t *> (node_))
               .empty ()
             ? 0
             : 1;
}
}
}

extern "C" int zlink_spot_node_has_any_actor (void *node_)
{
    return zlink::spot_actor_internal::node_has_any_actor (node_);
}

void erase_actor_stream_bindings (void *stream_)
{
    if (!stream_)
        return;
    std::deque<queued_join_request_t *> aborted_joins;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        drain_queued_join_requests_for_stream_locked (stream_, &aborted_joins);
        std::vector<queued_join_request_t *> received_aborts;
        collect_received_join_requests_for_stream_locked (stream_, aborted_joins,
                                                          &received_aborts);
        for (std::vector<queued_join_request_t *>::iterator it =
               received_aborts.begin ();
             it != received_aborts.end (); ++it) {
            queued_join_request_t *request = *it;
            remove_pending_join_request_locked (request);
            clear_actor_bound_session_locked (request->actor, true);
            retire_join_request_locked (request);
            aborted_joins.push_back (request);
        }
        erase_session_bindings_for_stream_locked (stream_);
        actor_runtime().sessions.explicit_stream_owners.erase (stream_);
        actor_runtime().sessions.stream_owners.erase (stream_);
    }
    for (std::deque<queued_join_request_t *>::iterator it =
           aborted_joins.begin ();
         it != aborted_joins.end (); ++it) {
        complete_join_request (*it, ZLINK_REQUEST_TERMINATED);
        release_join_request_after_completion (*it);
    }
}

extern "C" zlink_config_result_t zlink_spot_node_actor_lookup (
  void *node_, const char *actor_id_, zlink_actor_ref_t *out_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!valid_actor_id (actor_id_) || !out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    std::map<std::string, actor_handle_t *> &actors =
      actors_by_id_locked (static_cast<zlink::spot_node_t *> (node_));
    const std::map<std::string, actor_handle_t *>::const_iterator actor_it =
      actors.find (actor_id_);
    if (actor_it == actors.end () || actor_it->second->pending_remote_join) {
        errno = ENOENT;
        return ZLINK_CONFIG_NOT_FOUND;
    }
    fill_ref (actor_it->second, out_);
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_submit_result_t zlink_remote_actor_get_ref (
  void *node_,
  const zlink_routing_id_t *target_node_rid_,
  const char *actor_id_,
  zlink_actor_lookup_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_)
{
    if (!node_ || !valid_routing_id (target_node_rid_)
        || !valid_actor_id (actor_id_) || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    actor_lookup_operation_arg_t *arg =
      new (std::nothrow) actor_lookup_operation_arg_t;
    if (!arg) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    arg->request_node = static_cast<zlink::spot_node_t *> (node_);
    arg->target_node_rid = *target_node_rid_;
    memset (arg->actor_id, 0, sizeof (arg->actor_id));
    strncpy (arg->actor_id, actor_id_, ZLINK_ACTOR_ID_MAX - 1);
    return zlink::spot_actor_async::schedule_lookup_operation (
      handler_, userdata_, timeout_ms_, run_actor_lookup_operation, arg,
      cleanup_actor_lookup_operation_arg);
}

extern "C" zlink_submit_result_t zlink_spot_node_actor_destroy (
  void *node_, const zlink_actor_ref_t *actor_, zlink_reply_handler_fn handler_,
  void *userdata_, uint32_t timeout_ms_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!actor_ || !valid_actor_id (actor_->actor_id)
        || !valid_routing_id (&actor_->node_rid) || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (actor_lifecycle_reenters_same_actor (actor_)) {
        errno = EDEADLK;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    actor_reply_operation_arg_t *arg =
      new (std::nothrow) actor_reply_operation_arg_t;
    if (!arg) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    arg->run = run_destroy_operation_locked;
    arg->request_node = static_cast<zlink::spot_node_t *> (node_);
    arg->actor = *actor_;
    return zlink::spot_actor_async::schedule_reply_operation (
      handler_, userdata_, timeout_ms_, run_actor_reply_operation, arg,
      cleanup_actor_reply_operation_arg);
}

extern "C" zlink_submit_result_t zlink_spot_node_actor_join_spot (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_actor_join_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if ((flags_ & ~ZLINK_DONTWAIT) != 0) {
        errno = ENOTSUP;
        return ZLINK_SUBMIT_NOT_SUPPORTED;
    }
    if (!node_ || !actor_ref_ || !dest_node_rid_ || !dest_spot_rid_
        || !handler_ || !valid_actor_id (actor_ref_->actor_id)
        || !valid_routing_id (&actor_ref_->node_rid)
        || !valid_routing_id (dest_node_rid_)
        || !valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!valid_multipart_payload (parts_, part_count_))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (actor_lifecycle_reenters_same_actor (actor_ref_)) {
        errno = EDEADLK;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    queued_join_request_t *request = NULL;
    spot_handle_t *spot = NULL;
    zlink_request_result_t immediate_result = ZLINK_REQUEST_OK;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        zlink::spot_node_t *source_node =
          resolve_node_by_rid_locked (actor_ref_->node_rid);
        if (!source_node) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }

        actor_handle_t *actor = NULL;
        std::map<std::string, actor_handle_t *> &actors =
          actors_by_id_locked (source_node);
        std::map<std::string, actor_handle_t *>::iterator actor_it =
          actors.find (actor_ref_->actor_id);
        if (actor_it != actors.end ())
            actor = actor_it->second;
        if (!actor) {
            immediate_result = ZLINK_REQUEST_NOT_FOUND;
        } else if (actor_ref_->generation != 0
                   && actor->generation != actor_ref_->generation) {
            immediate_result = ZLINK_REQUEST_CONFLICT;
        }

        if (immediate_result != ZLINK_REQUEST_OK) {
            /* Complete outside the actor lock. */
        } else if (actor_has_pending_join_locked (actor)) {
            errno = EBUSY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
    }
    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_join_result (parts_, part_count_, handler_,
                                               userdata_, immediate_result);

    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
        if (!actor)
            immediate_result = actor_missing_request_result_from_errno ();
        zlink::spot_node_t *target_node =
          immediate_result == ZLINK_REQUEST_OK
            ? resolve_node_by_rid_locked (*dest_node_rid_)
            : NULL;
        if (immediate_result == ZLINK_REQUEST_OK && !target_node)
            immediate_result = ZLINK_REQUEST_NOT_CONNECTED;
        if (immediate_result != ZLINK_REQUEST_OK) {
            /* Complete outside the actor lock. */
        } else {
            std::shared_ptr<spot_logical_state_t> target_state =
              zlink::spot_node_access_t::lookup_spot_state (target_node,
                                                            dest_spot_rid_);
            spot = find_spot_facade_for_state_locked (target_node,
                                                      target_state);
            if (!spot)
                immediate_result = ZLINK_REQUEST_NOT_FOUND;
            else if (spot->logical_state && spot->logical_state->entry) {
                errno = EINVAL;
                return ZLINK_SUBMIT_INVALID_ARGUMENT;
            } else if (same_routing_id (actor->node_rid, *dest_node_rid_)
                       && actor->joined_spot_state
                       && same_routing_id (actor_current_spot_rid_locked (actor),
                                           *dest_spot_rid_)) {
                zlink_actor_ref_t current_ref;
                fill_ref (actor, &current_ref);
                const zlink_routing_id_t current_spot =
                  actor_current_spot_rid_locked (actor);
                if (!active_route_matches_locked (actor))
                    create_active_route_locked (actor);
                return complete_idempotent_join_async (
                  parts_, part_count_, handler_, userdata_, &current_ref,
                  &current_spot, actor->join_epoch);
            } else if (!same_routing_id (actor->node_rid, *dest_node_rid_)
                       && actor_route_disconnected_locked (actor->node,
                                                           *dest_node_rid_)) {
                immediate_result = ZLINK_REQUEST_NOT_CONNECTED;
            } else if (!same_routing_id (actor->node_rid, *dest_node_rid_)) {
                std::map<std::string, actor_handle_t *> &target_actors =
                  actors_by_id_locked (target_node);
                if (target_actors.count (actor->actor_id) != 0
                    || node_has_pending_join_actor_locked (target_node,
                                                           actor->actor_id
                                                             .c_str ())) {
                    immediate_result = ZLINK_REQUEST_CONFLICT;
                }
            }
        }
        if (immediate_result != ZLINK_REQUEST_OK)
            request = NULL;
        else {
            request = new (std::nothrow) queued_join_request_t ();
            if (!request) {
                errno = ENOMEM;
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
            request->actor = actor;
            request->spot = spot;
            request->spot_state = spot->logical_state;
            request->target_node = target_node;
            request->remote =
              !same_routing_id (actor->node_rid, *dest_node_rid_);
            request->target_node_rid = *dest_node_rid_;
            request->source_spot_rid = actor_current_spot_rid_locked (actor);
            if (request->remote) {
                request->target_actor_ref.node_rid = *dest_node_rid_;
                strncpy (request->target_actor_ref.actor_id,
                         actor->actor_id.c_str (), ZLINK_ACTOR_ID_MAX - 1);
                request->target_actor_ref.generation =
                  next_generation_for_node_locked (target_node);
                request->pending_target = create_actor_locked_with_generation (
                  target_node, *dest_node_rid_, actor->actor_id.c_str (),
                  request->target_actor_ref.generation, true);
                if (!request->pending_target) {
                    delete request;
                    return errno == EBUSY ? ZLINK_SUBMIT_INVALID_STATE
                                           : errno_to_submit_result (errno);
                }
            } else {
                fill_ref (actor, &request->target_actor_ref);
            }
            request->handler = handler_;
            request->userdata = userdata_;
            request->join_epoch = next_commit_epoch_locked ();
            const zlink_submit_result_t adopt_rc =
              adopt_multipart_payload (&request->message_parts, parts_,
                                       part_count_);
            if (adopt_rc != ZLINK_SUBMIT_OK) {
                if (request->pending_target)
                    std::unique_ptr<actor_handle_t> pending =
                      remove_actor_locked (request->pending_target, false);
                delete request;
                return adopt_rc;
            }
            index_join_request_locked (request);
            enqueue_join_request_locked (request);
        }
    }

    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_join_result (parts_, part_count_, handler_,
                                               userdata_, immediate_result);

    schedule_join_timeout (request, timeout_ms_);
    zlink_spot_notify_dispatch_info (
      spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE,
      ZLINK_SPOT_DISPATCH_SUBJECT_SPOT, spot);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_submit_result_t zlink_spot_node_actor_leave_spot (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!actor_ref_ || !dest_spot_rid_
        || !valid_actor_id (actor_ref_->actor_id)
        || !valid_routing_id (dest_spot_rid_) || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (actor_lifecycle_reenters_same_actor (actor_ref_)) {
        errno = EDEADLK;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    actor_reply_operation_arg_t *arg =
      new (std::nothrow) actor_reply_operation_arg_t;
    if (!arg) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    arg->run = run_leave_operation_locked;
    arg->request_node = static_cast<zlink::spot_node_t *> (node_);
    arg->actor = *actor_ref_;
    arg->rid = *dest_spot_rid_;
    return zlink::spot_actor_async::schedule_reply_operation (
      handler_, userdata_, timeout_ms_, run_actor_reply_operation, arg,
      cleanup_actor_reply_operation_arg);
}

extern "C" zlink_recv_result_t zlink_spot_actor_join_recv (
  void *spot_,
  zlink_actor_join_info_t *info_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  zlink_recv_flags_t flags_)
{
    if (!spot_ || !info_out_ || !parts_out_ || !part_count_out_) {
        errno = EINVAL;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (flags_ != ZLINK_RECV_FLAGS_DONTWAIT) {
        errno = ENOTSUP;
        return ZLINK_RECV_NOT_SUPPORTED;
    }
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->logical_state) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    queued_join_request_t *request = NULL;
    if (!peek_join_request_for_spot_locked (spot, &request)) {
        errno = EAGAIN;
        return ZLINK_RECV_NO_DATA;
    }
    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    for (zlink::spot_owned_msg_parts_t::iterator it =
           request->message_parts.begin ();
         it != request->message_parts.end (); ++it) {
        if (zlink::recv_tls_view::push (&(*it)) != 0) {
            zlink::recv_tls_view::abort ();
            return ZLINK_RECV_INTERNAL_ERROR;
        }
    }
    if (zlink::recv_tls_view::commit (parts_out_, part_count_out_) != 0) {
        zlink::recv_tls_view::abort ();
        return ZLINK_RECV_INTERNAL_ERROR;
    }
    zlink::spot_clear_msg_parts (&request->message_parts);
    remove_pending_join_request_locked (request);
    request->spot = spot;
    memset (info_out_, 0, sizeof (*info_out_));
    fill_ref (request->actor, &info_out_->source_actor);
    info_out_->target_actor = request->target_actor_ref;
    info_out_->source_node_rid = request->actor->node_rid;
    info_out_->source_spot_rid = request->source_spot_rid;
    info_out_->target_node_rid =
      request->target_node ? request->target_node_rid : request->actor->node_rid;
    if (request->spot_state)
        info_out_->target_spot_rid = request->spot_state->routing_id;
    info_out_->join_epoch = request->join_epoch;
    info_out_->request = request;
    info_out_->flags = request->remote ? ZLINK_ACTOR_JOIN_INFO_REMOTE : 0u;
    return ZLINK_RECV_OK;
}

extern "C" zlink_submit_result_t zlink_spot_actor_join_reply (
  void *spot_,
  const zlink_actor_join_info_t *info_,
  uint32_t accepted_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!spot_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!info_ || !info_->request) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if ((info_->flags & ~ZLINK_ACTOR_JOIN_INFO_REMOTE) != 0) {
        errno = EPROTO;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (accepted_ != 0u && accepted_ != 1u) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!valid_multipart_payload (parts_, part_count_))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    queued_join_request_t *request =
      static_cast<queued_join_request_t *> (info_->request);
    actor_handle_t *readable_actor = NULL;
    zlink_request_result_t completion_result =
      accepted_ ? ZLINK_REQUEST_OK : ZLINK_REQUEST_REJECTED;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        if (!join_request_live_locked (request) || request->replied
            || !request->spot
            || !same_logical_spot (request->spot, spot)) {
            errno = EALREADY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (info_->join_epoch != request->join_epoch
            || info_->flags
                 != (request->remote ? ZLINK_ACTOR_JOIN_INFO_REMOTE : 0u)) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        const zlink_submit_result_t adopt_rc =
          adopt_multipart_payload (&request->reply_parts, parts_, part_count_);
        if (adopt_rc != ZLINK_SUBMIT_OK) {
            return adopt_rc;
        }
        request->replied = true;
        if (accepted_)
            completion_result =
              commit_accepted_join_locked (request, &readable_actor);
        retire_join_request_locked (request);
    }

    if (readable_actor)
        notify_actor_readable (readable_actor);
    complete_join_request (request, completion_result);
    release_join_request_after_completion (request);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_recv_result_t zlink_spot_node_actor_recv_part (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  zlink_actor_recv_info_t *info_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_)
{
    if (!node_ || !actor_ref_ || !info_out_ || !part_out_
        || !has_more_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (flags_ != ZLINK_RECV_FLAGS_DONTWAIT) {
        errno = ENOTSUP;
        return ZLINK_RECV_NOT_SUPPORTED;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }

    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
    if (!actor || actor->node != static_cast<zlink::spot_node_t *> (node_)) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    spot_handle_t *current_spot = static_cast<spot_handle_t *> (
      zlink::spot_dispatch_event_callback_context_t::current_handle ());
    if (!current_spot || current_spot->logical_state != actor->joined_spot_state) {
        errno = ENOTSUP;
        return ZLINK_RECV_NOT_SUPPORTED;
    }
    if (actor->queue.empty ()) {
        errno = EAGAIN;
        return ZLINK_RECV_NO_DATA;
    }
    queued_actor_part_t &front = actor->queue.front ();
    *info_out_ = front.info;
    *has_more_out_ = front.part_flag;
    if (zlink_msg_adopt (part_out_, &front.part) != ZLINK_CONFIG_OK)
        return ZLINK_RECV_INTERNAL_ERROR;
    front.owns = false;
    actor->queue.pop_front ();
    actor->last_changed_ms = now_ms ();
    if (!actor->queue.empty ())
        notify_actor_readable (actor);
    return ZLINK_RECV_OK;
}

extern "C" zlink_submit_result_t zlink_stream_bind_actor (
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const zlink_actor_ref_t *actor_ref_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_)
{
    if (!stream_ || !valid_routing_id (session_rid_) || !actor_ref_
        || !valid_actor_id (actor_ref_->actor_id) || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    actor_reply_operation_arg_t *arg =
      new (std::nothrow) actor_reply_operation_arg_t;
    if (!arg) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    arg->run = run_bind_operation_locked;
    arg->stream = stream_;
    arg->actor = *actor_ref_;
    arg->rid = *session_rid_;
    return zlink::spot_actor_async::schedule_reply_operation (
      handler_, userdata_, timeout_ms_, run_actor_reply_operation, arg,
      cleanup_actor_reply_operation_arg);
}

extern "C" zlink_submit_result_t zlink_stream_unbind_actor (
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const char *actor_id_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  uint32_t timeout_ms_)
{
    if (!stream_ || !valid_routing_id (session_rid_)
        || !valid_actor_id (actor_id_) || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    actor_reply_operation_arg_t *arg =
      new (std::nothrow) actor_reply_operation_arg_t;
    if (!arg) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    arg->run = run_unbind_operation_locked;
    arg->stream = stream_;
    arg->rid = *session_rid_;
    strncpy (arg->actor_id, actor_id_, ZLINK_ACTOR_ID_MAX - 1);
    return zlink::spot_actor_async::schedule_reply_operation (
      handler_, userdata_, timeout_ms_, run_actor_reply_operation, arg,
      cleanup_actor_reply_operation_arg);
}

extern "C" zlink_submit_result_t zlink_stream_send_bound_actor_part (
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const char *actor_id_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_)
{
    LIBZLINK_UNUSED (flags_);
    if (!stream_ || !valid_routing_id (session_rid_)
        || !valid_actor_id (actor_id_) || !part_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    actor_handle_t *actor = NULL;
    {
        std::unique_lock<std::timed_mutex> lock (actor_runtime().mutex,
                                                 std::defer_lock);
        if (!lock.try_lock ()) {
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        actor_session_state_t::binding_map_t::iterator binding_it =
          find_session_binding_locked (stream_, session_rid_);
        if (binding_it == session_bindings_end_locked ()) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        session_binding_t &binding = binding_it->second;
        zlink::spot_node_t *stream_owner = stream_owner_locked (stream_);
        if (!stream_owner) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }
        std::map<std::string, session_binding_t::actor_entry_t>::iterator it =
          binding.actors.find (actor_id_);
        if (it == binding.actors.end ()) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        const zlink_submit_result_t enqueue_rc =
          enqueue_bound_actor_part_locked (stream_owner, &binding,
                                           &it->second, session_rid_,
                                           actor_id_, part_, part_flag_,
                                           &actor);
        if (enqueue_rc != ZLINK_SUBMIT_OK)
            return enqueue_rc;
    }
    if (!actor->pending_remote_join)
        notify_actor_readable (actor);
    return ZLINK_SUBMIT_OK;
}

namespace zlink
{
namespace spot_actor_internal
{
int set_stream_owner (void *stream_, void *node_)
{
    if (!stream_ || !node_ || !is_stream_socket (stream_)
        || !is_registered_spot_node_handle (node_)) {
        errno = EINVAL;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->routed_enabled ()) {
        errno = ENOTSUP;
        return -1;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    std::map<void *, zlink::spot_node_t *>::const_iterator previous =
      actor_runtime().sessions.stream_owners.find (stream_);
    if (previous != actor_runtime().sessions.stream_owners.end ()
        && previous->second != node) {
        errno = EBUSY;
        return -1;
    }
    actor_runtime().sessions.stream_owners[stream_] = node;
    actor_runtime().sessions.explicit_stream_owners.insert (stream_);
    return 0;
}
}
}

extern "C" zlink_config_result_t zlink_stream_attach_actor_gateway (
  void *stream_, void *node_)
{
    if (!stream_ || !node_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_) || !is_registered_spot_node_handle (node_)) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (zlink::spot_actor_internal::set_stream_owner (stream_, node_) != 0)
        return zlink::config_result_internal::from_errno (errno);
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_submit_result_t zlink_spot_node_actor_send_bound_session_msg (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  zlink_msg_t *message_,
  zlink_send_flags_t flags_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!actor_ref_ || !message_ || !valid_actor_id (actor_ref_->actor_id)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    void *stream = NULL;
    zlink_routing_id_t session_rid;
    {
        std::unique_lock<std::timed_mutex> lock (actor_runtime().mutex,
                                                 std::defer_lock);
        if (!lock.try_lock ()) {
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        zlink::spot_node_t *request_node =
          static_cast<zlink::spot_node_t *> (node_);
        const actor_resolution_t resolved =
          resolve_actor_for_request_locked (request_node, actor_ref_);
        if (resolved.result != ZLINK_REQUEST_OK)
            return request_result_to_submit_result (resolved.result);
        actor_handle_t *actor = resolved.actor;
        const zlink_submit_result_t bound_rc =
          validate_actor_bound_session_locked (actor, &stream, &session_rid);
        if (bound_rc != ZLINK_SUBMIT_OK)
            return bound_rc;
    }

    zlink_msg_t copied_message;
    const zlink_submit_result_t copy_rc =
      copy_msg_for_stream_send (message_, &copied_message);
    if (copy_rc != ZLINK_SUBMIT_OK)
        return copy_rc;

    const zlink_submit_result_t send_rc =
      send_copied_msg_to_bound_stream (stream, &session_rid, &copied_message,
                                       flags_);
    if (send_rc != ZLINK_SUBMIT_OK) {
        (void) zlink_msg_close (&copied_message);
        return send_rc;
    }
    (void) zlink_msg_close (message_);
    (void) zlink_msg_init (message_);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_handler_result_t zlink_spot_actor_lifecycle_handler (
  void *spot_,
  zlink_spot_actor_lifecycle_handler_fn on_join_,
  zlink_spot_actor_lifecycle_handler_fn on_leave_,
  void *userdata_)
{
    if (!spot_) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_HANDLE;
    }
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->logical_state) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_HANDLE;
    }
    if (actor_lifecycle_reenters_same_spot (spot)) {
        errno = EDEADLK;
        return ZLINK_HANDLER_DEADLOCK;
    }

    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    if (!on_join_ && !on_leave_) {
        clear_lifecycle_state_locked (spot->logical_state.get ());
        return ZLINK_HANDLER_OK;
    }
    lifecycle_registration_t &registration =
      ensure_lifecycle_registration_locked (spot->logical_state.get ());
    registration.on_join = on_join_;
    registration.on_leave = on_leave_;
    registration.userdata = userdata_;
    return ZLINK_HANDLER_OK;
}

extern "C" zlink_config_result_t zlink_stream_bound_actors (
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  zlink_actor_ref_t *entries_,
  size_t *count_)
{
    if (!stream_ || !valid_routing_id (session_rid_) || !count_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_session_state_t::binding_map_t::const_iterator binding_it =
      find_session_binding_locked (stream_, session_rid_);
    const size_t capacity = entries_ ? *count_ : 0;
    size_t written = 0;
    if (binding_it != session_bindings_end_const_locked ()) {
        const session_binding_t &binding = binding_it->second;
        for (std::map<std::string, session_binding_t::actor_entry_t>::
               const_iterator it = binding.actors.begin ();
             it != binding.actors.end (); ++it) {
            if (entries_ && written < capacity)
                entries_[written] = it->second.ref;
            ++written;
        }
    }
    if (entries_ && written > capacity)
        *count_ = capacity;
    else
        *count_ = written;
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_request_result_t zlink_spot_node_actor_close_bound_session (
  void *node_, const zlink_actor_ref_t *actor_ref_, uint32_t timeout_ms_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!actor_ref_ || !valid_actor_id (actor_ref_->actor_id)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }

    actor_handle_t *readable_actor = NULL;
    {
        std::unique_lock<std::timed_mutex> lock (actor_runtime().mutex,
                                                 std::defer_lock);
        const bool locked =
          timeout_ms_ == 0
            ? lock.try_lock ()
            : lock.try_lock_for (std::chrono::milliseconds (timeout_ms_));
        if (!locked) {
            errno = ETIMEDOUT;
            return ZLINK_REQUEST_TIMED_OUT;
        }
        actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
        if (!actor)
            return actor_missing_request_result_from_errno ();
        if (!actor->bound_stream) {
            errno = ENOENT;
            return ZLINK_REQUEST_NOT_FOUND;
        }

        actor_session_state_t::binding_map_t::iterator binding_it =
          find_session_binding_locked (actor->bound_stream,
                                       &actor->bound_session_rid);
        if (binding_it != session_bindings_end_locked ()) {
            binding_it->second.actors.erase (actor->actor_id);
            if (binding_it->second.actors.empty ())
                erase_session_binding_locked (binding_it);
        }
        clear_actor_bound_session_locked (actor, true);
        if (!actor->queue.empty ())
            readable_actor = actor;
    }
    if (readable_actor)
        notify_actor_readable (readable_actor);
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_config_result_t zlink_discovery_resolve_actor (
  void *discovery_, const char *actor_id_, zlink_actor_route_t *route_out_)
{
    if (!valid_actor_id (actor_id_) || !route_out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    zlink::discovery_t *discovery =
      zlink::discovery_access_t::from_handle (discovery_);
    if (!discovery) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    zlink_routing_id_t owner_rid;
    zlink_msg_t value;
    memset (&owner_rid, 0, sizeof (owner_rid));
    memset (&value, 0, sizeof (value));
    if (zlink::discovery_access_t::resolve_route (
          discovery, ZLINK_ROUTE_KIND_ACTOR, actor_id_, strlen (actor_id_),
          &owner_rid, &value)
        != 0) {
        {
            std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
            if (find_active_route_locked (actor_id_, route_out_)
                && actor_route_is_current_location (*route_out_, actor_id_))
                return ZLINK_CONFIG_OK;
        }
        errno = ENOENT;
        return ZLINK_CONFIG_NOT_FOUND;
    }

    if (zlink_msg_size (&value) != sizeof (*route_out_)) {
        (void) zlink_msg_close (&value);
        errno = ENOENT;
        return ZLINK_CONFIG_NOT_FOUND;
    }
    memcpy (route_out_, zlink_msg_data (&value), sizeof (*route_out_));
    (void) zlink_msg_close (&value);
    if (!actor_route_is_current_location (*route_out_, actor_id_)) {
        memset (route_out_, 0, sizeof (*route_out_));
        errno = ENOENT;
        return ZLINK_CONFIG_NOT_FOUND;
    }
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_config_result_t zlink_spot_node_spots_snapshot (
  void *node_, zlink_spot_node_spot_entry_t *entries_, size_t *count_)
{
    if (!node_ || !count_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    struct snapshot_spot_t
    {
        snapshot_spot_t () : facade (NULL) {}
        spot_handle_t *facade;
        std::shared_ptr<spot_logical_state_t> state;
    };
    std::vector<snapshot_spot_t> spots;
    std::shared_ptr<spot_logical_state_t> entry_state =
      zlink::spot_node_access_t::entry_spot_state (
        static_cast<zlink::spot_node_t *> (node_));
    bool has_entry_facade = false;
    for (std::set<spot_handle_t *>::const_iterator it = actor_runtime().nodes.known_spots.begin ();
         it != actor_runtime().nodes.known_spots.end (); ++it) {
        if ((*it)->node != node_)
            continue;
        snapshot_spot_t row;
        row.facade = *it;
        row.state = (*it)->logical_state;
        spots.push_back (row);
        if (entry_state && (*it)->logical_state == entry_state)
            has_entry_facade = true;
    }
    if (entry_state && !has_entry_facade) {
        snapshot_spot_t row;
        row.state = entry_state;
        spots.push_back (row);
    }
    if (!entries_) {
        *count_ = spots.size ();
        return ZLINK_CONFIG_OK;
    }
    std::vector<actor_handle_t *> actors;
    collect_actor_handles_locked (&actors);
    const size_t limit = std::min (*count_, spots.size ());
    for (size_t i = 0; i != limit; ++i) {
        memset (&entries_[i], 0, sizeof (entries_[i]));
        entries_[i].spot_rid =
          spots[i].state ? spots[i].state->routing_id
                         : spots[i].facade->spot_routing_id;
        entries_[i].spot_kind = spot_kind_for_state (spots[i].state);
        entries_[i].dispatch_handler_attached =
          spots[i].facade && spot_dispatch_handler_attached (spots[i].facade)
            ? 1u
            : 0u;
        for (std::vector<actor_handle_t *>::const_iterator actor_it =
               actors.begin ();
             actor_it != actors.end (); ++actor_it) {
            if (!(*actor_it)->pending_remote_join
                && (*actor_it)->joined_spot_state
                && (*actor_it)->joined_spot_state == spots[i].state)
                ++entries_[i].joined_actor_count;
        }
        entries_[i].pending_actor_join_count =
          pending_join_count_for_spot_locked (spots[i].state);
        entries_[i].route_synced =
          static_cast<zlink::spot_node_t *> (node_)->spot_owner_route_synced ()
            ? 1u
            : 0u;
        entries_[i].last_changed_ms = now_ms ();
    }
    *count_ = limit;
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_config_result_t zlink_spot_node_actors_snapshot (
  void *node_, zlink_spot_node_actor_entry_t *entries_, size_t *count_)
{
    if (!node_ || !count_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    std::vector<actor_handle_t *> actors;
    std::map<std::string, actor_handle_t *> &node_actors =
      actors_by_id_locked (static_cast<zlink::spot_node_t *> (node_));
    for (std::map<std::string, actor_handle_t *>::const_iterator it =
           node_actors.begin ();
         it != node_actors.end (); ++it)
        if (!it->second->pending_remote_join)
            actors.push_back (it->second);
    if (!entries_) {
        *count_ = actors.size ();
        return ZLINK_CONFIG_OK;
    }
    const size_t limit = std::min (*count_, actors.size ());
    for (size_t i = 0; i != limit; ++i) {
        memset (&entries_[i], 0, sizeof (entries_[i]));
        fill_ref (actors[i], &entries_[i].actor);
        if (actors[i]->joined_spot_state) {
            entries_[i].current_spot_rid =
              actors[i]->joined_spot_state->routing_id;
            entries_[i].current_spot_kind =
              spot_kind_for_state (actors[i]->joined_spot_state);
        }
        entries_[i].route_synced =
          active_route_matches_locked (actors[i]) ? 1u : 0u;
        entries_[i].pending_message_count =
          static_cast<uint32_t> (actors[i]->queue.size ());
        entries_[i].last_changed_ms = actors[i]->last_changed_ms;
    }
    *count_ = limit;
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_config_result_t zlink_spot_actors_snapshot (
  void *spot_, zlink_actor_ref_t *entries_, size_t *count_)
{
    if (!spot_ || !count_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    std::vector<actor_handle_t *> actors;
    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    collect_actor_handles_locked (&actors);
    std::vector<actor_handle_t *> matching;
    for (std::vector<actor_handle_t *>::const_iterator it = actors.begin ();
         it != actors.end (); ++it) {
        if (spot && !(*it)->pending_remote_join && (*it)->joined_spot_state
            && (*it)->joined_spot_state == spot->logical_state)
            matching.push_back (*it);
    }
    if (!entries_) {
        *count_ = matching.size ();
        return ZLINK_CONFIG_OK;
    }
    const size_t limit = std::min (*count_, matching.size ());
    for (size_t i = 0; i != limit; ++i)
        fill_ref (matching[i], &entries_[i]);
    *count_ = limit;
    return ZLINK_CONFIG_OK;
}
