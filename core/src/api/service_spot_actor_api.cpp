/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_handle_internal.hpp"
#include "api/service_mode_internal.hpp"
#include "api/service_surface_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/request_timeout_scheduler_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "api/request_result_internal.hpp"
#include "api/config_result_internal.hpp"
#include "api/handler_result_internal.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_subject_access.hpp"
#include "protocol/wire.hpp"
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
        owns_message (false),
        owns_reply (false),
        join_epoch (0),
        pending_target (NULL)
    {
        memset (&message, 0, sizeof (message));
        memset (&reply, 0, sizeof (reply));
        memset (&target_node_rid, 0, sizeof (target_node_rid));
        memset (&source_spot_rid, 0, sizeof (source_spot_rid));
        memset (&target_actor_ref, 0, sizeof (target_actor_ref));
    }

    ~queued_join_request_t ()
    {
        if (owns_message)
            (void) zlink_msg_close (&message);
        if (owns_reply)
            (void) zlink_msg_close (&reply);
    }

    actor_handle_t *actor;
    spot_handle_t *spot;
    std::shared_ptr<spot_logical_state_t> spot_state;
    zlink::spot_node_t *target_node;
    bool remote;
    zlink_routing_id_t target_node_rid;
    zlink_routing_id_t source_spot_rid;
    zlink_actor_ref_t target_actor_ref;
    zlink_reply_handler_fn handler;
    void *userdata;
    bool replied;
    bool owns_message;
    bool owns_reply;
    uint64_t join_epoch;
    actor_handle_t *pending_target;
    zlink_msg_t message;
    zlink_msg_t reply;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
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

struct actor_admission_t
{
    actor_admission_t () : handler (NULL), userdata (NULL) {}
    zlink_actor_admission_handler_fn handler;
    void *userdata;
};

struct actor_runtime_t
{
    actor_runtime_t () : protocol_drop_count (0), next_join_epoch (1) {}

    std::timed_mutex mutex;
    std::map<std::string, zlink::spot_node_t *> nodes_by_rid;
    std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >
      join_queues;
    std::set<spot_handle_t *> known_spots;
    std::map<std::string, session_binding_t> session_bindings;
    std::map<std::string, zlink_actor_route_t> active_routes;
    std::map<zlink::spot_node_t *, actor_admission_t> admission_handlers;
    std::set<zlink::spot_node_t *> known_nodes;
    std::set<std::pair<zlink::spot_node_t *, std::string> >
      disconnected_actor_routes;
    std::set<queued_join_request_t *> live_join_requests;
    uint64_t protocol_drop_count;
    uint64_t next_join_epoch;
};

actor_runtime_t &actor_runtime ()
{
    static actor_runtime_t runtime;
    return runtime;
}

thread_local bool g_in_actor_admission_handler = false;

uint64_t now_ms ();
uint64_t next_generation_for_node_locked (zlink::spot_node_t *node_);
void update_active_route_locked (actor_handle_t *actor_);
bool known_node_locked (zlink::spot_node_t *node_);
std::set<actor_handle_t *> &actor_handles_locked (zlink::spot_node_t *node_);
std::map<std::string, actor_handle_t *> &actors_by_id_locked (
  zlink::spot_node_t *node_);

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
    for (std::set<spot_handle_t *>::iterator it = actor_runtime().known_spots.begin ();
         it != actor_runtime().known_spots.end (); ++it) {
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

bool actor_has_pending_join_locked (const actor_handle_t *actor_)
{
    if (!actor_)
        return false;
    for (std::set<queued_join_request_t *>::const_iterator it =
           actor_runtime().live_join_requests.begin ();
         it != actor_runtime().live_join_requests.end (); ++it) {
        if ((*it)->actor == actor_ && !(*it)->replied)
            return true;
    }
    return false;
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
    for (std::set<queued_join_request_t *>::const_iterator it =
           actor_runtime().live_join_requests.begin ();
         it != actor_runtime().live_join_requests.end (); ++it) {
        const queued_join_request_t *request = *it;
        if (!request->replied && request->remote
            && request->target_node == node_
            && request->target_actor_ref.actor_id[0] != '\0'
            && strcmp (request->target_actor_ref.actor_id, actor_id_) == 0)
            return true;
    }
    return false;
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

bool lock_actor_request_mutex (std::unique_lock<std::timed_mutex> *lock_,
                               uint32_t timeout_ms_)
{
    if (!lock_)
        return false;
    if (timeout_ms_ == 0) {
        return lock_->try_lock ();
    }
    return lock_->try_lock_for (std::chrono::milliseconds (timeout_ms_));
}

std::string routing_id_key (const zlink_routing_id_t &rid_)
{
    return std::string (reinterpret_cast<const char *> (rid_.data), rid_.size);
}

bool same_routing_id (const zlink_routing_id_t &lhs_,
                      const zlink_routing_id_t &rhs_)
{
    return lhs_.size == rhs_.size
           && memcmp (lhs_.data, rhs_.data, lhs_.size) == 0;
}

bool valid_routing_id (const zlink_routing_id_t *rid_)
{
    return rid_ && rid_->size > 0 && rid_->size <= sizeof (rid_->data);
}

bool valid_actor_id (const char *actor_id_)
{
    if (!actor_id_ || actor_id_[0] == '\0')
        return false;
    return strnlen (actor_id_, ZLINK_ACTOR_ID_MAX) < ZLINK_ACTOR_ID_MAX;
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
    actor_runtime().nodes_by_rid[routing_id_key (node_rid_)] = node_;
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
      actor_runtime().nodes_by_rid.find (routing_id_key (rid_));
    return it == actor_runtime().nodes_by_rid.end () ? NULL : it->second;
}

bool known_node_locked (zlink::spot_node_t *node_)
{
    return node_ && actor_runtime().known_nodes.count (node_) != 0;
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
           actor_runtime().known_nodes.begin ();
         node_it != actor_runtime().known_nodes.end (); ++node_it) {
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
    return actor_runtime().disconnected_actor_routes.count (
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

std::string session_key (void *node_,
                         void *stream_,
                         const zlink_routing_id_t *session_rid_)
{
    return std::to_string (reinterpret_cast<uintptr_t> (node_)) + ":"
           + std::to_string (reinterpret_cast<uintptr_t> (stream_)) + ":"
           + routing_id_key (*session_rid_);
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

bool active_route_matches_locked (const actor_handle_t *actor_)
{
    if (!actor_)
        return false;
    std::map<std::string, zlink_actor_route_t>::const_iterator it =
      actor_runtime().active_routes.find (actor_->actor_id);
    return it != actor_runtime().active_routes.end ()
           && same_routing_id (it->second.actor.node_rid, actor_->node_rid)
           && it->second.actor.generation == actor_->generation;
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
    route.joined = actor_->joined_spot_state ? 1u : 0u;
    if (actor_->joined_spot_state)
        route.joined_spot_rid = actor_->joined_spot_state->routing_id;
    actor_runtime().active_routes[actor_->actor_id] = route;
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
      actor_runtime().active_routes.find (actor_->actor_id);
    if (it == actor_runtime().active_routes.end ())
        return;
    if (same_routing_id (it->second.actor.node_rid, actor_->node_rid)
        && it->second.actor.generation == actor_->generation)
        actor_runtime().active_routes.erase (it);
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
        const std::string bound_key =
          session_key (actor_->bound_session_node, actor_->bound_stream,
                       &actor_->bound_session_rid);
        std::map<std::string, session_binding_t>::iterator binding_it =
          actor_runtime().session_bindings.find (bound_key);
        if (binding_it != actor_runtime().session_bindings.end ()) {
            if (erase_session_binding_) {
                binding_it->second.actors.erase (actor_->actor_id);
                if (binding_it->second.actors.empty ())
                    actor_runtime().session_bindings.erase (binding_it);
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

    if (request_->owns_reply) {
        request_->handler (result_, &request_->reply, 1, request_->userdata);
        request_->owns_reply = false;
    } else {
        request_->handler (result_, NULL, 0, request_->userdata);
    }
}

zlink_submit_result_t complete_immediate_join_result (
  zlink_msg_t *message_, zlink_reply_handler_fn handler_, void *userdata_,
  zlink_request_result_t result_)
{
    (void) zlink_msg_close (message_);
    (void) zlink_msg_init (message_);
    handler_ (result_, NULL, 0, userdata_);
    return ZLINK_SUBMIT_OK;
}

struct idempotent_join_completion_t
{
    zlink_reply_handler_fn handler;
    void *userdata;
};

void complete_idempotent_join_scheduled (void *userdata_)
{
    idempotent_join_completion_t *completion =
      static_cast<idempotent_join_completion_t *> (userdata_);
    if (!completion)
        return;
    zlink_msg_t reply;
    zlink_msg_init (&reply);
    completion->handler (ZLINK_REQUEST_OK, &reply, 1, completion->userdata);
    (void) zlink_msg_close (&reply);
    delete completion;
}

void cleanup_idempotent_join_completion (void *userdata_)
{
    delete static_cast<idempotent_join_completion_t *> (userdata_);
}

zlink_submit_result_t complete_idempotent_join_async (
  zlink_msg_t *message_, zlink_reply_handler_fn handler_, void *userdata_)
{
    (void) zlink_msg_close (message_);
    (void) zlink_msg_init (message_);
    idempotent_join_completion_t *completion =
      new (std::nothrow) idempotent_join_completion_t;
    if (!completion) {
        errno = ENOMEM;
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    }
    completion->handler = handler_;
    completion->userdata = userdata_;
    (void) zlink::request_timeout::schedule (
      1, complete_idempotent_join_scheduled, completion,
      cleanup_idempotent_join_completion);
    return ZLINK_SUBMIT_OK;
}

void retire_join_request_locked (queued_join_request_t *request_)
{
    if (!request_)
        return;
    actor_runtime().live_join_requests.erase (request_);
    if (request_->pending_target
        && request_->pending_target->pending_remote_join) {
        std::unique_ptr<actor_handle_t> pending =
          remove_actor_locked (request_->pending_target, false);
        request_->pending_target = NULL;
    }
    if (request_->owns_message) {
        (void) zlink_msg_close (&request_->message);
        request_->owns_message = false;
    }
    request_->replied = true;
}

void release_join_request_after_completion (queued_join_request_t *request_)
{
    if (!request_)
        return;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        actor_runtime().live_join_requests.erase (request_);
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
    std::deque<queued_join_request_t *> &queue = actor_runtime().join_queues[key];
    for (std::deque<queued_join_request_t *>::iterator it = queue.begin ();
         it != queue.end (); ++it) {
        if (*it == request_) {
            queue.erase (it);
            break;
        }
    }
    if (queue.empty ())
        actor_runtime().join_queues.erase (key);
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
        if (actor_runtime().live_join_requests.count (request) != 0 && !request->replied) {
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

zlink_request_result_t errno_to_request_result (int err_)
{
    return zlink::request_result_internal::from_errno (err_);
}

zlink_submit_result_t errno_to_submit_result (int err_)
{
    return zlink::submit_result_internal::from_errno (err_);
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

    const std::string key =
      session_key (actor_->bound_session_node, actor_->bound_stream,
                   &actor_->bound_session_rid);
    std::map<std::string, session_binding_t>::iterator binding_it =
      actor_runtime().session_bindings.find (key);
    if (binding_it == actor_runtime().session_bindings.end ()) {
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

zlink_submit_result_t send_temp_to_bound_stream (void *stream_,
                                                 const zlink_routing_id_t *rid_,
                                                 zlink_msg_t *temp_,
                                                 zlink_send_flags_t flags_)
{
    zlink::socket_base_t *stream = try_as_socket (stream_);
    if (!stream || stream->socket_type () != ZLINK_CORE_SOCKET_STREAM) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }

    const zlink_submit_result_t rc =
      zlink_send_part_rid (stream_, rid_, temp_, flags_, ZLINK_PART_FINAL);
    return rc;
}

zlink_submit_result_t copy_msg_to_temp (zlink_msg_t *src_, zlink_msg_t *dst_)
{
    if (!src_ || !dst_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    memset (dst_, 0, sizeof (*dst_));
    if (zlink_msg_init (dst_) != ZLINK_CONFIG_OK)
        return ZLINK_SUBMIT_INTERNAL_ERROR;
    if (zlink_msg_copy (dst_, src_) != ZLINK_CONFIG_OK) {
        const int err = errno;
        (void) zlink_msg_close (dst_);
        errno = err;
        return errno_to_submit_result (err);
    }
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t build_packet_frame (zlink_msg_t *header_,
                                          zlink_msg_t *body_,
                                          zlink_msg_t *frame_out_)
{
    const size_t header_size = zlink_msg_size (header_);
    const size_t body_size = zlink_msg_size (body_);
    if (header_size > UINT16_MAX || body_size > UINT32_MAX
        || header_size > SIZE_MAX - body_size - 6u) {
        errno = EMSGSIZE;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const size_t total_size = 6u + header_size + body_size;
    memset (frame_out_, 0, sizeof (*frame_out_));
    if (zlink_msg_init_size (frame_out_, total_size) != ZLINK_CONFIG_OK)
        return ZLINK_SUBMIT_INTERNAL_ERROR;

    unsigned char *data =
      static_cast<unsigned char *> (zlink_msg_data (frame_out_));
    zlink::put_uint16 (data, static_cast<uint16_t> (header_size));
    zlink::put_uint32 (data + 2, static_cast<uint32_t> (body_size));
    if (header_size > 0)
        memcpy (data + 6, zlink_msg_data (header_), header_size);
    if (body_size > 0)
        memcpy (data + 6 + header_size, zlink_msg_data (body_), body_size);
    return ZLINK_SUBMIT_OK;
}
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
    if (g_in_actor_admission_handler) {
        errno = EFSM;
        return ZLINK_CONFIG_INVALID_STATE;
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
    return ZLINK_CONFIG_OK;
}

void register_actor_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_runtime().known_spots.insert (spot_);
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
    actor_runtime().known_nodes.insert (node_);
    actor_runtime().nodes_by_rid[routing_id_key (node_rid)] = node_;
}

void erase_actor_spot_node (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    std::vector<std::unique_ptr<actor_handle_t> > actors_to_delete;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        actor_runtime().admission_handlers.erase (node_);
        for (std::set<std::pair<zlink::spot_node_t *, std::string> >::iterator
               it = actor_runtime().disconnected_actor_routes.begin ();
             it != actor_runtime().disconnected_actor_routes.end ();) {
            if (it->first == node_)
                it = actor_runtime().disconnected_actor_routes.erase (it);
            else
                ++it;
        }
        for (std::map<std::string, zlink::spot_node_t *>::iterator it =
               actor_runtime().nodes_by_rid.begin ();
             it != actor_runtime().nodes_by_rid.end ();) {
            if (it->second == node_)
                it = actor_runtime().nodes_by_rid.erase (it);
            else
                ++it;
        }
        while (!actors_by_id_locked (node_).empty ()) {
            actor_handle_t *actor = actors_by_id_locked (node_).begin ()->second;
            actors_to_delete.push_back (remove_actor_locked (actor, false));
        }
        actor_runtime().known_nodes.erase (node_);
    }
}

void note_actor_spot_node_peer_disconnected (
  zlink::spot_node_t *node_, const zlink_routing_id_t *target_node_rid_)
{
    if (!node_ || !valid_routing_id (target_node_rid_))
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_runtime().disconnected_actor_routes.insert (
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
        for (std::set<spot_handle_t *>::iterator it = actor_runtime().known_spots.begin ();
             it != actor_runtime().known_spots.end (); ++it) {
            if (*it != spot_ && same_logical_spot (*it, spot_)) {
                replacement = *it;
                break;
            }
        }
        actor_runtime().known_spots.erase (spot_);
        if (replacement) {
            for (std::set<queued_join_request_t *>::iterator it =
                   actor_runtime().live_join_requests.begin ();
                 it != actor_runtime().live_join_requests.end (); ++it) {
                if ((*it)->spot == spot_)
                    (*it)->spot = replacement;
            }
            return;
        }
        spot_logical_state_t *key = join_queue_key (logical_state);
        if (logical_state && logical_state->entry) {
            (void) key;
            for (std::set<queued_join_request_t *>::iterator it =
                   actor_runtime().live_join_requests.begin ();
                 it != actor_runtime().live_join_requests.end (); ++it) {
                if ((*it)->spot == spot_)
                    (*it)->spot = NULL;
            }
            return;
        }
        pending.swap (actor_runtime().join_queues[key]);
        actor_runtime().join_queues.erase (key);
        for (std::set<queued_join_request_t *>::iterator it =
               actor_runtime().live_join_requests.begin ();
             it != actor_runtime().live_join_requests.end (); ++it) {
            queued_join_request_t *request = *it;
            if (request->spot_state == logical_state && !request->replied
                && std::find (pending.begin (), pending.end (), request)
                     == pending.end ())
                pending.push_back (request);
        }
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
    for (std::set<spot_handle_t *>::const_iterator it = actor_runtime().known_spots.begin ();
         it != actor_runtime().known_spots.end (); ++it) {
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
    std::map<spot_logical_state_t *, std::deque<queued_join_request_t *> >::
      const_iterator queue_it = actor_runtime().join_queues.find (
        join_queue_key (spot->logical_state));
    if (queue_it != actor_runtime().join_queues.end () && !queue_it->second.empty ())
        return 1;
    for (std::set<queued_join_request_t *>::const_iterator it =
           actor_runtime().live_join_requests.begin ();
         it != actor_runtime().live_join_requests.end (); ++it) {
        if (!(*it)->replied && (*it)->spot_state == spot->logical_state)
            return 1;
    }
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

extern "C" int zlink_spot_node_has_any_actor (void *node_)
{
    if (!node_)
        return 0;
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    return actors_by_id_locked (static_cast<zlink::spot_node_t *> (node_))
               .empty ()
             ? 0
             : 1;
}

void erase_actor_stream_bindings (void *stream_)
{
    if (!stream_)
        return;
    std::deque<queued_join_request_t *> aborted_joins;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        for (std::map<spot_logical_state_t *,
                      std::deque<queued_join_request_t *> >::iterator join_it =
               actor_runtime().join_queues.begin ();
             join_it != actor_runtime().join_queues.end (); ++join_it) {
            std::deque<queued_join_request_t *> &queue = join_it->second;
            for (std::deque<queued_join_request_t *>::iterator it =
                   queue.begin ();
                 it != queue.end ();) {
                queued_join_request_t *request = *it;
                if (request->actor && request->actor->bound_stream == stream_
                    && !request->replied) {
                    it = queue.erase (it);
                    if (request->actor) {
                        clear_actor_bound_session_locked (request->actor, true);
                        clear_actor_joined_spot_locked (request->actor);
                    }
                    retire_join_request_locked (request);
                    aborted_joins.push_back (request);
                    continue;
                }
                ++it;
            }
        }
        std::vector<queued_join_request_t *> received_aborts;
        for (std::set<queued_join_request_t *>::iterator it =
               actor_runtime().live_join_requests.begin ();
             it != actor_runtime().live_join_requests.end (); ++it) {
            queued_join_request_t *request = *it;
            if (request->actor && request->actor->bound_stream == stream_
                && !request->replied
                && std::find (aborted_joins.begin (), aborted_joins.end (),
                              request)
                     == aborted_joins.end ()) {
                received_aborts.push_back (request);
            }
        }
        for (std::vector<queued_join_request_t *>::iterator it =
               received_aborts.begin ();
             it != received_aborts.end (); ++it) {
            queued_join_request_t *request = *it;
            remove_pending_join_request_locked (request);
            clear_actor_bound_session_locked (request->actor, true);
            clear_actor_joined_spot_locked (request->actor);
            retire_join_request_locked (request);
            aborted_joins.push_back (request);
        }
        for (std::map<std::string, session_binding_t>::iterator it =
               actor_runtime().session_bindings.begin ();
             it != actor_runtime().session_bindings.end ();) {
            if (it->second.stream != stream_) {
                ++it;
                continue;
            }
            for (std::map<std::string, session_binding_t::actor_entry_t>::iterator
                   actor_it =
                   it->second.actors.begin ();
                 actor_it != it->second.actors.end (); ++actor_it) {
                actor_handle_t *actor = actor_it->second.actor;
                if (actor && actor->bound_stream == stream_) {
                    clear_actor_bound_session_locked (actor, true);
                    clear_actor_joined_spot_locked (actor);
                }
            }
            it = actor_runtime().session_bindings.erase (it);
        }
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

extern "C" zlink_config_result_t zlink_remote_actor_get_ref (
  const zlink_routing_id_t *target_node_rid_,
  const char *actor_id_,
  zlink_actor_ref_t *out_)
{
    if (!valid_routing_id (target_node_rid_) || !valid_actor_id (actor_id_)
        || !out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    memset (out_, 0, sizeof (*out_));
    out_->node_rid = *target_node_rid_;
    strncpy (out_->actor_id, actor_id_, ZLINK_ACTOR_ID_MAX - 1);
    out_->generation = 0;
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_handler_result_t zlink_spot_node_actor_admission_handler (
  void *node_, zlink_actor_admission_handler_fn handler_, void *userdata_)
{
    if (!node_ || !is_registered_spot_node_handle (node_)) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }

    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_admission_t &entry =
      actor_runtime().admission_handlers[static_cast<zlink::spot_node_t *> (node_)];
    entry.handler = handler_;
    entry.userdata = userdata_;
    return ZLINK_HANDLER_OK;
}

extern "C" zlink_request_result_t zlink_spot_node_create_remote_actor (
  void *node_,
  const zlink_routing_id_t *target_node_rid_,
  const char *actor_id_,
  zlink_msg_t *message_,
  zlink_actor_create_result_t *out_,
  uint32_t timeout_ms_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!valid_routing_id (target_node_rid_) || !valid_actor_id (actor_id_)
        || !out_) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }

    std::unique_lock<std::timed_mutex> lock (actor_runtime().mutex, std::defer_lock);
    if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
        errno = ETIMEDOUT;
        return ZLINK_REQUEST_TIMED_OUT;
    }
    zlink::spot_node_t *target = resolve_node_by_rid_locked (*target_node_rid_);
    if (!target) {
        errno = ENOTCONN;
        return ZLINK_REQUEST_NOT_CONNECTED;
    }

    std::map<std::string, actor_handle_t *> &actors =
      actors_by_id_locked (target);
    std::map<std::string, actor_handle_t *>::iterator it =
      actors.find (actor_id_);
    if (it != actors.end ()) {
        out_->status = ZLINK_ACTOR_CREATE_EXISTING;
        fill_ref (it->second, &out_->actor);
        if (message_) {
            (void) zlink_msg_close (message_);
            (void) zlink_msg_init (message_);
        }
        return ZLINK_REQUEST_OK;
    }

    actor_admission_t admission = actor_runtime().admission_handlers[target];
    zlink_actor_admission_result_t admission_result =
      ZLINK_ACTOR_ADMISSION_REJECT;
    if (admission.handler) {
        g_in_actor_admission_handler = true;
        admission_result =
          admission.handler (target, actor_id_, message_, admission.userdata);
        g_in_actor_admission_handler = false;
    }
    if (!admission.handler
        || admission_result != ZLINK_ACTOR_ADMISSION_ACCEPT) {
        if (message_) {
            (void) zlink_msg_close (message_);
            (void) zlink_msg_init (message_);
        }
        errno = EACCES;
        return ZLINK_REQUEST_REJECTED;
    }

    actor_handle_t *actor =
      create_actor_locked (target, *target_node_rid_, actor_id_);
    if (!actor)
        return errno_to_request_result (errno);

    out_->status = ZLINK_ACTOR_CREATE_CREATED;
    fill_ref (actor, &out_->actor);
    if (message_) {
        (void) zlink_msg_close (message_);
        (void) zlink_msg_init (message_);
    }
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_request_result_t zlink_spot_node_actor_destroy (
  void *node_, const zlink_actor_ref_t *actor_, uint32_t timeout_ms_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!actor_ || !valid_actor_id (actor_->actor_id)
        || !valid_routing_id (&actor_->node_rid)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }

    std::unique_ptr<actor_handle_t> actor_to_delete;
    {
        std::unique_lock<std::timed_mutex> lock (actor_runtime().mutex,
                                                 std::defer_lock);
        if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
            errno = ETIMEDOUT;
            return ZLINK_REQUEST_TIMED_OUT;
        }
        if (!resolve_node_by_rid_locked (actor_->node_rid)) {
            errno = ENOTCONN;
            return ZLINK_REQUEST_NOT_CONNECTED;
        }
        actor_handle_t *actor = resolve_actor_ref_locked (actor_);
        if (!actor) {
            if (errno == ESTALE)
                return ZLINK_REQUEST_CONFLICT;
            return ZLINK_REQUEST_NOT_FOUND;
        }
        if (actor_in_user_spot_locked (actor)) {
            errno = EBUSY;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        if (actor_has_pending_join_locked (actor)) {
            errno = EBUSY;
            return ZLINK_REQUEST_BUSY;
        }
        if (actor->bound_stream) {
            const std::string bound_key =
              session_key (actor->bound_session_node, actor->bound_stream,
                           &actor->bound_session_rid);
            std::map<std::string, session_binding_t>::const_iterator
              binding_it = actor_runtime().session_bindings.find (bound_key);
            if (binding_it != actor_runtime().session_bindings.end ()
                && binding_it->second.in_progress
                && binding_it->second.in_progress_actor_id
                     == actor->actor_id) {
                errno = EBUSY;
                return ZLINK_REQUEST_BUSY;
            }
        }
        actor_to_delete = remove_actor_locked (actor);
    }
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_submit_result_t zlink_spot_node_actor_join_spot (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *message_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if ((flags_ & ~ZLINK_DONTWAIT) != 0) {
        errno = ENOTSUP;
        return ZLINK_SUBMIT_NOT_SUPPORTED;
    }
    if (!node_ || !actor_ref_ || !dest_node_rid_ || !dest_spot_rid_
        || !message_ || !handler_ || !valid_actor_id (actor_ref_->actor_id)
        || !valid_routing_id (&actor_ref_->node_rid)
        || !valid_routing_id (dest_node_rid_)
        || !valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
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
        } else if (same_routing_id (actor->node_rid, *dest_node_rid_)
                   && actor->joined_spot_state
                   && same_routing_id (actor_current_spot_rid_locked (actor),
                                       *dest_spot_rid_)) {
            return complete_idempotent_join_async (message_, handler_,
                                                   userdata_);
        }
    }
    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_join_result (message_, handler_, userdata_,
                                               immediate_result);

    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
        if (!actor) {
            if (errno == ESTALE)
                immediate_result = ZLINK_REQUEST_CONFLICT;
            else
                immediate_result = ZLINK_REQUEST_NOT_FOUND;
        }
        zlink::spot_node_t *target_node =
          immediate_result == ZLINK_REQUEST_OK
            ? resolve_node_by_rid_locked (*dest_node_rid_)
            : NULL;
        if (immediate_result == ZLINK_REQUEST_OK && !target_node)
            immediate_result = ZLINK_REQUEST_NOT_FOUND;
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
            else if ((!spot->logical_state || !spot->logical_state->entry)
                     && !actor->bound_stream) {
                errno = EFSM;
                return ZLINK_SUBMIT_INVALID_STATE;
            } else if (!same_routing_id (actor->node_rid, *dest_node_rid_)) {
                std::map<std::string, actor_handle_t *> &target_actors =
                  actors_by_id_locked (target_node);
                if (target_actors.count (actor->actor_id) != 0
                    || node_has_pending_join_actor_locked (target_node,
                                                           actor->actor_id
                                                             .c_str ())) {
                    errno = EBUSY;
                    return ZLINK_SUBMIT_INVALID_STATE;
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
            request->join_epoch = actor_runtime().next_join_epoch++;
            if (actor_runtime().next_join_epoch == 0)
                actor_runtime().next_join_epoch = 1;
            if (zlink_msg_adopt (&request->message, message_)
                != ZLINK_CONFIG_OK) {
                if (request->pending_target)
                    std::unique_ptr<actor_handle_t> pending =
                      remove_actor_locked (request->pending_target, false);
                delete request;
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
            request->owns_message = true;
            actor_runtime().live_join_requests.insert (request);
            actor_runtime().join_queues[join_queue_key (request->spot_state)].push_back (
              request);
        }
    }

    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_join_result (message_, handler_, userdata_,
                                               immediate_result);

    schedule_join_timeout (request, timeout_ms_);
    zlink_spot_notify_dispatch_info (
      spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE,
      ZLINK_SPOT_DISPATCH_SUBJECT_SPOT, spot);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_request_result_t zlink_spot_node_actor_leave_spot (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint32_t timeout_ms_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!actor_ref_ || !dest_spot_rid_
        || !valid_actor_id (actor_ref_->actor_id)
        || !valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    std::unique_lock<std::timed_mutex> lock (actor_runtime().mutex, std::defer_lock);
    if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
        errno = ETIMEDOUT;
        return ZLINK_REQUEST_TIMED_OUT;
    }
    if (!resolve_node_by_rid_locked (actor_ref_->node_rid)) {
        errno = ENOTCONN;
        return ZLINK_REQUEST_NOT_CONNECTED;
    }
    actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
    if (!actor) {
        if (errno == ESTALE)
            return ZLINK_REQUEST_CONFLICT;
        return ZLINK_REQUEST_NOT_FOUND;
    }
    if (actor_has_pending_join_locked (actor)) {
        errno = EBUSY;
        return ZLINK_REQUEST_BUSY;
    }
    if (!actor->joined_spot_state
        || !same_routing_id (actor_current_spot_rid_locked (actor),
                             *dest_spot_rid_)) {
        errno = ESTALE;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    clear_actor_joined_spot_locked (actor);
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_recv_result_t zlink_spot_actor_join_recv (
  void *spot_,
  zlink_actor_join_info_t *info_out_,
  zlink_msg_t *message_out_,
  zlink_recv_flags_t flags_)
{
    if (!spot_ || !info_out_ || !message_out_) {
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
    std::deque<queued_join_request_t *> &queue =
      actor_runtime().join_queues[join_queue_key (spot->logical_state)];
    if (queue.empty ()) {
        errno = EAGAIN;
        return ZLINK_RECV_NO_DATA;
    }
    queued_join_request_t *request = queue.front ();
    queue.pop_front ();
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
    if (zlink_msg_adopt (message_out_, &request->message) != ZLINK_CONFIG_OK)
        return ZLINK_RECV_INTERNAL_ERROR;
    request->owns_message = false;
    return ZLINK_RECV_OK;
}

extern "C" zlink_submit_result_t zlink_spot_actor_join_reply (
  void *spot_,
  const zlink_actor_join_info_t *info_,
  uint32_t accepted_,
  zlink_msg_t *message_)
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
    queued_join_request_t *request =
      static_cast<queued_join_request_t *> (info_->request);
    actor_handle_t *readable_actor = NULL;
    zlink_request_result_t completion_result =
      accepted_ ? ZLINK_REQUEST_OK : ZLINK_REQUEST_REJECTED;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        if (actor_runtime().live_join_requests.count (request) == 0 || request->replied
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
        if (message_) {
            if (zlink_msg_adopt (&request->reply, message_) != ZLINK_CONFIG_OK)
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            request->owns_reply = true;
        }
        request->replied = true;
        if (accepted_) {
            if (request->remote) {
                actor_handle_t *source = request->actor;
                actor_handle_t *target = request->pending_target;
                if (!target || !target->pending_remote_join) {
                    completion_result = ZLINK_REQUEST_CONFLICT;
                } else {
                    target->bound_session_node = source->bound_session_node;
                    target->bound_stream = source->bound_stream;
                    target->bound_session_rid = source->bound_session_rid;
                    target->last_changed_ms = now_ms ();

                    bool session_updated = false;
                    if (source->bound_stream) {
                        const std::string bound_key = session_key (
                          source->bound_session_node, source->bound_stream,
                          &source->bound_session_rid);
                        std::map<std::string, session_binding_t>::iterator
                          binding_it = actor_runtime().session_bindings.find (bound_key);
                        if (binding_it != actor_runtime().session_bindings.end ()) {
                            std::map<std::string,
                                     session_binding_t::actor_entry_t>::iterator
                              entry_it =
                                binding_it->second.actors.find (
                                  source->actor_id);
                            if (entry_it != binding_it->second.actors.end ()
                                && entry_it->second.actor == source
                                && entry_it->second.ref.generation
                                     == source->generation) {
                                entry_it->second.actor = target;
                                fill_ref (target, &entry_it->second.ref);
                                session_updated = true;
                            }
                        }
                    }
                    if (session_updated) {
                        std::deque<queued_actor_part_t> pending_target_queue =
                          std::move (target->queue);
                        target->queue = std::move (source->queue);
                        for (std::deque<queued_actor_part_t>::iterator it =
                               target->queue.begin ();
                             it != target->queue.end (); ++it) {
                            fill_ref (target, &it->info.actor);
                        }
                        while (!pending_target_queue.empty ()) {
                            target->queue.push_back (
                              std::move (pending_target_queue.front ()));
                            pending_target_queue.pop_front ();
                        }
                        clear_actor_bound_session_locked (source, false);
                        target->pending_remote_join = false;
                        set_actor_spot_locked (target, request->spot);
                        request->pending_target = NULL;
                        std::unique_ptr<actor_handle_t> retired =
                          remove_actor_locked (source, false);
                        create_active_route_locked (target);
                        if (!target->queue.empty ())
                            readable_actor = target;
                    } else {
                        clear_actor_bound_session_locked (target, false);
                        request->pending_target = NULL;
                        std::unique_ptr<actor_handle_t> rollback =
                          remove_actor_locked (target, false);
                        completion_result = ZLINK_REQUEST_CONFLICT;
                    }
                }
            } else {
                set_actor_spot_locked (request->actor, request->spot);
                if (!request->actor->queue.empty ())
                    readable_actor = request->actor;
            }
        }
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

extern "C" zlink_request_result_t zlink_stream_bind_actor (
  void *node_,
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const zlink_actor_ref_t *actor_ref_,
  uint32_t timeout_ms_)
{
    if (!node_ || !stream_ || !valid_routing_id (session_rid_) || !actor_ref_
        || !valid_actor_id (actor_ref_->actor_id)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    std::unique_lock<std::timed_mutex> lock (actor_runtime().mutex, std::defer_lock);
    if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
        errno = ETIMEDOUT;
        return ZLINK_REQUEST_TIMED_OUT;
    }
    if (!resolve_node_by_rid_locked (actor_ref_->node_rid)) {
        errno = ENOTCONN;
        return ZLINK_REQUEST_NOT_CONNECTED;
    }
    actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
    if (!actor) {
        if (errno == ESTALE)
            return ZLINK_REQUEST_CONFLICT;
        errno = ENOENT;
        return ZLINK_REQUEST_NOT_FOUND;
    }
    if (actor->bound_stream
        && (actor->bound_session_node != static_cast<zlink::spot_node_t *> (node_)
            || actor->bound_stream != stream_
            || !same_routing_id (actor->bound_session_rid, *session_rid_))) {
        errno = EBUSY;
        return ZLINK_REQUEST_BUSY;
    }
    session_binding_t &binding =
      actor_runtime().session_bindings[session_key (node_, stream_, session_rid_)];
    binding.stream = stream_;
    binding.session_rid = *session_rid_;
    std::map<std::string, session_binding_t::actor_entry_t>::iterator previous =
      binding.actors.find (actor->actor_id);
    if (previous != binding.actors.end () && previous->second.actor
        && previous->second.actor != actor) {
        clear_actor_bound_session_locked (previous->second.actor, true);
    }
    session_binding_t::actor_entry_t entry;
    entry.actor = actor;
    fill_ref (actor, &entry.ref);
    binding.actors[actor->actor_id] = entry;
    actor->bound_session_node = static_cast<zlink::spot_node_t *> (node_);
    actor->bound_stream = stream_;
    actor->bound_session_rid = *session_rid_;
    actor->last_changed_ms = now_ms ();
    create_active_route_locked (actor);
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_request_result_t zlink_stream_unbind_actor (
  void *node_,
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const char *actor_id_,
  uint32_t timeout_ms_)
{
    if (!node_ || !stream_ || !valid_routing_id (session_rid_)
        || !valid_actor_id (actor_id_)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    std::unique_lock<std::timed_mutex> lock (actor_runtime().mutex, std::defer_lock);
    if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
        errno = ETIMEDOUT;
        return ZLINK_REQUEST_TIMED_OUT;
    }
    const std::string key = session_key (node_, stream_, session_rid_);
    std::map<std::string, session_binding_t>::iterator binding_it =
      actor_runtime().session_bindings.find (key);
    if (binding_it == actor_runtime().session_bindings.end ())
        return ZLINK_REQUEST_OK;
    session_binding_t &binding = binding_it->second;
    std::map<std::string, session_binding_t::actor_entry_t>::iterator it =
      binding.actors.find (actor_id_);
    if (it != binding.actors.end ()) {
        if (it->second.actor
            && actor_route_disconnected_locked (
              static_cast<zlink::spot_node_t *> (node_),
              it->second.ref.node_rid)) {
            errno = ENOTCONN;
            return ZLINK_REQUEST_NOT_CONNECTED;
        }
        if (it->second.actor && actor_in_user_spot_locked (it->second.actor)) {
            errno = EBUSY;
            return ZLINK_REQUEST_INVALID_STATE;
        }
        if (it->second.actor) {
            clear_actor_bound_session_locked (it->second.actor, true);
        }
        binding.actors.erase (it);
    }
    if (binding.actors.empty ())
        actor_runtime().session_bindings.erase (binding_it);
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_submit_result_t zlink_stream_send_bound_actor_part (
  void *node_,
  void *stream_,
  const zlink_routing_id_t *session_rid_,
  const char *actor_id_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_)
{
    LIBZLINK_UNUSED (flags_);
    if (!node_ || !stream_ || !valid_routing_id (session_rid_)
        || !valid_actor_id (actor_id_) || !part_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    zlink_routing_id_t source_node_rid;
    memset (&source_node_rid, 0, sizeof (source_node_rid));
    if (static_cast<zlink::spot_node_t *> (node_)->node_routing_id (
          &source_node_rid)
        != 0)
        return errno_to_submit_result (errno);
    actor_handle_t *actor = NULL;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        const std::string key = session_key (node_, stream_, session_rid_);
        std::map<std::string, session_binding_t>::iterator binding_it =
          actor_runtime().session_bindings.find (key);
        if (binding_it == actor_runtime().session_bindings.end ()) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        session_binding_t &binding = binding_it->second;
        if (binding.in_progress && binding.in_progress_actor_id != actor_id_) {
            errno = EFSM;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        std::map<std::string, session_binding_t::actor_entry_t>::iterator it =
          binding.actors.find (actor_id_);
        if (it == binding.actors.end ()) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        if (actor_route_disconnected_locked (
              static_cast<zlink::spot_node_t *> (node_),
              it->second.ref.node_rid)) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }
        if (!resolve_node_by_rid_locked (it->second.ref.node_rid)) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }
        actor = resolve_actor_ref_locked (&it->second.ref, true);
        if (!actor) {
            (void) zlink_msg_close (part_);
            (void) zlink_msg_init (part_);
            if (part_flag_ == ZLINK_PART_MORE) {
                binding.in_progress = true;
                binding.in_progress_actor_id = actor_id_;
            } else {
                binding.in_progress = false;
                binding.in_progress_actor_id.clear ();
            }
            return ZLINK_SUBMIT_OK;
        }
        it->second.actor = actor;
        queued_actor_part_t queued;
        queued.info.actor.node_rid = actor->node_rid;
        strncpy (queued.info.actor.actor_id, actor->actor_id.c_str (),
                 ZLINK_ACTOR_ID_MAX - 1);
        queued.info.actor.generation = actor->generation;
        queued.info.source_node_rid = source_node_rid;
        queued.info.source_session_rid = *session_rid_;
        queued.part_flag = part_flag_;
        if (zlink_msg_adopt (&queued.part, part_) != ZLINK_CONFIG_OK)
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        queued.owns = true;
        actor->queue.push_back (std::move (queued));
        actor->last_changed_ms = now_ms ();
        if (part_flag_ == ZLINK_PART_MORE) {
            binding.in_progress = true;
            binding.in_progress_actor_id = actor_id_;
        } else {
            binding.in_progress = false;
            binding.in_progress_actor_id.clear ();
        }
    }
    if (!actor->pending_remote_join)
        notify_actor_readable (actor);
    return ZLINK_SUBMIT_OK;
}

extern "C" int zlink_actor_pending_target_enqueue_for_testing (
  const zlink_actor_ref_t *target_ref_, const char *payload_)
{
    if (!target_ref_ || !payload_) {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_handle_t *actor = resolve_actor_ref_locked (target_ref_, true);
    if (!actor || !actor->pending_remote_join) {
        errno = ENOENT;
        return -1;
    }
    queued_actor_part_t queued;
    fill_ref (actor, &queued.info.actor);
    queued.info.source_node_rid = actor->node_rid;
    queued.part_flag = ZLINK_PART_FINAL;
    if (zlink_msg_init_size (&queued.part, strlen (payload_))
        != ZLINK_CONFIG_OK)
        return -1;
    memcpy (zlink_msg_data (&queued.part), payload_, strlen (payload_));
    queued.owns = true;
    actor->queue.push_back (std::move (queued));
    actor->last_changed_ms = now_ms ();
    return 0;
}

extern "C" int zlink_actor_queue_size_for_testing (
  const zlink_actor_ref_t *actor_ref_, size_t *size_out_)
{
    if (!actor_ref_ || !size_out_) {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_, true);
    if (!actor) {
        errno = ENOENT;
        return -1;
    }
    *size_out_ = actor->queue.size ();
    return 0;
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
        std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
        zlink::spot_node_t *request_node =
          static_cast<zlink::spot_node_t *> (node_);
        zlink::spot_node_t *actor_owner =
          resolve_node_by_rid_locked (actor_ref_->node_rid);
        const bool remote_request = actor_owner && actor_owner != request_node;
        actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
        if (!actor) {
            if (remote_request) {
                (void) zlink_msg_close (message_);
                (void) zlink_msg_init (message_);
                ++actor_runtime().protocol_drop_count;
                return ZLINK_SUBMIT_OK;
            }
            if (errno == ESTALE)
                return ZLINK_SUBMIT_INVALID_STATE;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        const zlink_submit_result_t bound_rc =
          validate_actor_bound_session_locked (actor, &stream, &session_rid);
        if (bound_rc != ZLINK_SUBMIT_OK) {
            if (remote_request) {
                (void) zlink_msg_close (message_);
                (void) zlink_msg_init (message_);
                ++actor_runtime().protocol_drop_count;
                return ZLINK_SUBMIT_OK;
            }
            return bound_rc;
        }
    }

    zlink_msg_t temp;
    const zlink_submit_result_t copy_rc = copy_msg_to_temp (message_, &temp);
    if (copy_rc != ZLINK_SUBMIT_OK)
        return copy_rc;

    const zlink_submit_result_t send_rc =
      send_temp_to_bound_stream (stream, &session_rid, &temp, flags_);
    if (send_rc != ZLINK_SUBMIT_OK) {
        (void) zlink_msg_close (&temp);
        return send_rc;
    }
    (void) zlink_msg_close (message_);
    (void) zlink_msg_init (message_);
    return ZLINK_SUBMIT_OK;
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
        if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
            errno = ETIMEDOUT;
            return ZLINK_REQUEST_TIMED_OUT;
        }
        actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
        if (!actor) {
            if (errno == ESTALE)
                return ZLINK_REQUEST_CONFLICT;
            return ZLINK_REQUEST_NOT_FOUND;
        }
        if (!actor->bound_stream) {
            errno = ENOENT;
            return ZLINK_REQUEST_NOT_FOUND;
        }

        const std::string bound_key =
          session_key (actor->bound_session_node, actor->bound_stream,
                       &actor->bound_session_rid);
        std::map<std::string, session_binding_t>::iterator binding_it =
          actor_runtime().session_bindings.find (bound_key);
        if (binding_it != actor_runtime().session_bindings.end ()) {
            binding_it->second.actors.erase (actor->actor_id);
            if (binding_it->second.actors.empty ())
                actor_runtime().session_bindings.erase (binding_it);
        }
        clear_actor_bound_session_locked (actor, true);
        clear_actor_joined_spot_locked (actor);
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
    if (!zlink::discovery_access_t::from_handle (discovery_)) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime().mutex);
    std::map<std::string, zlink_actor_route_t>::const_iterator it =
      actor_runtime().active_routes.find (actor_id_);
    if (it == actor_runtime().active_routes.end ()) {
        errno = ENOENT;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    *route_out_ = it->second;
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
    for (std::set<spot_handle_t *>::const_iterator it = actor_runtime().known_spots.begin ();
         it != actor_runtime().known_spots.end (); ++it) {
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
    const size_t limit = std::min (*count_, spots.size ());
    for (size_t i = 0; i != limit; ++i) {
        memset (&entries_[i], 0, sizeof (entries_[i]));
        entries_[i].spot_rid =
          spots[i].state ? spots[i].state->routing_id
                         : spots[i].facade->spot_routing_id;
        entries_[i].dispatch_handler_attached =
          spots[i].facade && spot_dispatch_handler_attached (spots[i].facade)
            ? 1u
            : 0u;
        std::vector<actor_handle_t *> actors;
        collect_actor_handles_locked (&actors);
        for (std::vector<actor_handle_t *>::const_iterator actor_it =
               actors.begin ();
             actor_it != actors.end (); ++actor_it) {
            if (!(*actor_it)->pending_remote_join
                && (*actor_it)->joined_spot_state
                && (*actor_it)->joined_spot_state == spots[i].state)
                ++entries_[i].joined_actor_count;
        }
        uint32_t pending_count = 0;
        for (std::set<queued_join_request_t *>::const_iterator join_it =
               actor_runtime().live_join_requests.begin ();
             join_it != actor_runtime().live_join_requests.end (); ++join_it) {
            const queued_join_request_t *request = *join_it;
            if (!request->replied && request->spot_state == spots[i].state)
                ++pending_count;
        }
        entries_[i].pending_actor_join_count = pending_count;
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
        entries_[i].joined = actors[i]->joined_spot_state ? 1u : 0u;
        if (actors[i]->joined_spot_state)
            entries_[i].joined_spot_rid =
              actors[i]->joined_spot_state->routing_id;
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
