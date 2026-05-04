/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_handle_internal.hpp"
#include "api/service_mode_internal.hpp"
#include "api/service_surface_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "api/request_result_internal.hpp"
#include "api/config_result_internal.hpp"
#include "api/handler_result_internal.hpp"
#include "services/discovery/discovery_access.hpp"
#include "services/spot/spot_handle.hpp"
#include "services/spot/spot_node.hpp"
#include "protocol/wire.hpp"
#include "utils/clock.hpp"
#include "utils/sleep.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
const uint32_t actor_handle_tag = 0xacc70001u;
const size_t actor_unread_part_hard_limit = 1024u;

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
        joined_spot (NULL),
        bound_session_node (NULL),
        bound_stream (NULL),
        last_changed_ms (0)
    {
        memset (&node_rid, 0, sizeof (node_rid));
        memset (&bound_session_rid, 0, sizeof (bound_session_rid));
    }

    bool check_tag () const { return tag == actor_handle_tag; }

    uint32_t tag;
    zlink::spot_node_t *node;
    zlink_routing_id_t node_rid;
    std::string actor_id;
    uint64_t generation;
    spot_handle_t *joined_spot;
    zlink::spot_node_t *bound_session_node;
    void *bound_stream;
    zlink_routing_id_t bound_session_rid;
    uint64_t last_changed_ms;
    std::deque<queued_actor_part_t> queue;
};

struct queued_join_request_t
{
    queued_join_request_t () :
        actor (NULL),
        spot (NULL),
        handler (NULL),
        userdata (NULL),
        replied (false),
        has_timeout (false),
        owns_message (false),
        owns_reply (false)
    {
        memset (&message, 0, sizeof (message));
        memset (&reply, 0, sizeof (reply));
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
    zlink_reply_handler_fn handler;
    void *userdata;
    bool replied;
    bool has_timeout;
    bool owns_message;
    bool owns_reply;
    zlink_msg_t message;
    zlink_msg_t reply;
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

std::timed_mutex g_actor_mutex;
std::set<actor_handle_t *> g_actor_handles;
std::map<zlink::spot_node_t *, std::map<std::string, actor_handle_t *> >
  g_actors_by_node;
std::map<std::string, zlink::spot_node_t *> g_nodes_by_rid;
std::map<spot_handle_t *, std::deque<queued_join_request_t *> > g_join_queues;
std::set<spot_handle_t *> g_known_spots;
std::map<std::string, session_binding_t> g_session_bindings;
std::map<std::string, zlink_actor_route_t> g_active_routes;
std::map<zlink::spot_node_t *, actor_admission_t> g_admission_handlers;
std::set<zlink::spot_node_t *> g_known_nodes;
std::set<std::pair<zlink::spot_node_t *, std::string> >
  g_disconnected_actor_routes;
std::set<queued_join_request_t *> g_retired_join_requests;
uint64_t g_next_generation = 1;
thread_local bool g_in_actor_admission_handler = false;

bool lock_actor_request_mutex (std::unique_lock<std::timed_mutex> *lock_,
                               uint32_t timeout_ms_)
{
    if (!lock_)
        return false;
    if (timeout_ms_ == 0) {
        lock_->lock ();
        return true;
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

actor_handle_t *create_actor_locked (zlink::spot_node_t *node_,
                                     const zlink_routing_id_t &node_rid_,
                                     const char *actor_id_)
{
    std::map<std::string, actor_handle_t *> &actors = g_actors_by_node[node_];
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
    actor->generation = g_next_generation++;
    actor->last_changed_ms = zlink::clock_t ().now_ms ();

    actor_handle_t *raw = actor.release ();
    actors[raw->actor_id] = raw;
    g_actor_handles.insert (raw);
    g_nodes_by_rid[routing_id_key (node_rid_)] = node_;
    return raw;
}

actor_handle_t *as_actor_locked (void *actor_)
{
    actor_handle_t *actor = static_cast<actor_handle_t *> (actor_);
    if (!actor || !actor->check_tag () || g_actor_handles.count (actor) == 0)
        return NULL;
    return actor;
}

zlink::spot_node_t *resolve_node_by_rid_locked (
  const zlink_routing_id_t &rid_)
{
    const std::map<std::string, zlink::spot_node_t *>::iterator it =
      g_nodes_by_rid.find (routing_id_key (rid_));
    return it == g_nodes_by_rid.end () ? NULL : it->second;
}

bool known_node_locked (zlink::spot_node_t *node_)
{
    return node_ && g_known_nodes.count (node_) != 0;
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
    return g_disconnected_actor_routes.count (
             std::make_pair (source_node_, routing_id_key (target_rid_)))
           != 0;
}

actor_handle_t *resolve_actor_ref_locked (const zlink_actor_ref_t *ref_)
{
    if (!ref_ || !valid_actor_id (ref_->actor_id)
        || !valid_routing_id (&ref_->node_rid))
        return NULL;

    zlink::spot_node_t *node = resolve_node_by_rid_locked (ref_->node_rid);
    if (!node) {
        errno = ENOENT;
        return NULL;
    }

    const std::map<zlink::spot_node_t *,
                   std::map<std::string, actor_handle_t *> >::iterator node_it =
      g_actors_by_node.find (node);
    if (node_it == g_actors_by_node.end ()) {
        errno = ENOENT;
        return NULL;
    }

    const std::map<std::string, actor_handle_t *>::iterator actor_it =
      node_it->second.find (ref_->actor_id);
    if (actor_it == node_it->second.end ()) {
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

bool active_route_matches_locked (const actor_handle_t *actor_)
{
    if (!actor_)
        return false;
    std::map<std::string, zlink_actor_route_t>::const_iterator it =
      g_active_routes.find (actor_->actor_id);
    return it != g_active_routes.end ()
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
    route.joined = actor_->joined_spot ? 1u : 0u;
    if (actor_->joined_spot)
        route.joined_spot_rid = actor_->joined_spot->spot_routing_id;
    g_active_routes[actor_->actor_id] = route;
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
      g_active_routes.find (actor_->actor_id);
    if (it == g_active_routes.end ())
        return;
    if (same_routing_id (it->second.actor.node_rid, actor_->node_rid)
        && it->second.actor.generation == actor_->generation)
        g_active_routes.erase (it);
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
    actor_->joined_spot = NULL;
    actor_->last_changed_ms = now_ms ();
    update_active_route_locked (actor_);
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
          g_session_bindings.find (bound_key);
        if (binding_it != g_session_bindings.end ()) {
            if (erase_session_binding_) {
                binding_it->second.actors.erase (actor_->actor_id);
                if (binding_it->second.actors.empty ())
                    g_session_bindings.erase (binding_it);
            } else {
                std::map<std::string, session_binding_t::actor_entry_t>::iterator
                  entry_it = binding_it->second.actors.find (actor_->actor_id);
                if (entry_it != binding_it->second.actors.end ())
                    entry_it->second.actor = NULL;
            }
        }
        clear_actor_bound_session_locked (actor_, false);
    }

    std::map<zlink::spot_node_t *, std::map<std::string, actor_handle_t *> >::
      iterator node_it = g_actors_by_node.find (actor_->node);
    if (node_it != g_actors_by_node.end ()) {
        node_it->second.erase (actor_->actor_id);
        if (node_it->second.empty ())
            g_actors_by_node.erase (node_it);
    }
    remove_matching_active_route_locked (actor_);
    g_actor_handles.erase (actor_);
    actor_->tag = 0;
    return std::unique_ptr<actor_handle_t> (actor_);
}

void notify_actor_readable (actor_handle_t *actor_)
{
    if (actor_ && actor_->joined_spot)
        zlink_spot_notify_dispatch_info (
          actor_->joined_spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE,
          ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR, actor_);
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

void retire_join_request_locked (queued_join_request_t *request_)
{
    if (!request_)
        return;
    if (request_->owns_message) {
        (void) zlink_msg_close (&request_->message);
        request_->owns_message = false;
    }
    request_->replied = true;
    g_retired_join_requests.insert (request_);
}

void release_join_request_after_completion (queued_join_request_t *request_)
{
    if (!request_)
        return;
    bool should_delete = false;
    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        if (!request_->has_timeout) {
            g_retired_join_requests.erase (request_);
            should_delete = true;
        }
    }
    if (should_delete)
        delete request_;
}

void remove_pending_join_request_locked (queued_join_request_t *request_)
{
    if (!request_ || !request_->spot)
        return;
    std::deque<queued_join_request_t *> &queue = g_join_queues[request_->spot];
    for (std::deque<queued_join_request_t *>::iterator it = queue.begin ();
         it != queue.end (); ++it) {
        if (*it == request_) {
            queue.erase (it);
            break;
        }
    }
}

void schedule_join_timeout (queued_join_request_t *request_,
                            uint32_t timeout_ms_)
{
    if (!request_ || timeout_ms_ == 0)
        return;
    request_->has_timeout = true;
    std::thread ([request_, timeout_ms_] {
        zlink::sleep_ms (timeout_ms_);
        bool timed_out = false;
        bool release_retired = false;
        {
            std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
            if (g_retired_join_requests.count (request_) == 0
                && !request_->replied) {
                remove_pending_join_request_locked (request_);
                retire_join_request_locked (request_);
                timed_out = true;
            } else if (g_retired_join_requests.erase (request_) != 0) {
                release_retired = true;
            }
        }
        if (timed_out) {
            complete_join_request (request_, ZLINK_REQUEST_TIMED_OUT);
            {
                std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
                g_retired_join_requests.erase (request_);
            }
            delete request_;
        } else if (release_retired)
            delete request_;
    }).detach ();
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
    if (!spot_ || !spot_->request_reply_state)
        return false;
    std::lock_guard<std::mutex> lock (
      spot_->request_reply_state->dispatch.mutex);
    return spot_->request_reply_state->dispatch.handler != NULL;
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
      g_session_bindings.find (key);
    if (binding_it == g_session_bindings.end ()) {
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

extern "C" void *zlink_spot_node_actor_new (void *node_,
                                             const char *actor_id_)
{
    if (!node_ || !valid_actor_id (actor_id_)) {
        errno = EINVAL;
        return NULL;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return NULL;
    }
    if (g_in_actor_admission_handler) {
        errno = EFSM;
        return NULL;
    }

    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->routed_enabled ()) {
        errno = ENOTSUP;
        return NULL;
    }

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node->node_routing_id (&node_rid) != 0)
        return NULL;

    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    return create_actor_locked (node, node_rid, actor_id_);
}

void register_actor_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    g_known_spots.insert (spot_);
}

void register_actor_spot_node (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node_->node_routing_id (&node_rid) != 0)
        return;
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    g_known_nodes.insert (node_);
    g_nodes_by_rid[routing_id_key (node_rid)] = node_;
}

void erase_actor_spot_node (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    std::vector<std::unique_ptr<actor_handle_t> > actors_to_delete;
    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        g_known_nodes.erase (node_);
        g_admission_handlers.erase (node_);
        for (std::set<std::pair<zlink::spot_node_t *, std::string> >::iterator
               it = g_disconnected_actor_routes.begin ();
             it != g_disconnected_actor_routes.end ();) {
            if (it->first == node_)
                it = g_disconnected_actor_routes.erase (it);
            else
                ++it;
        }
        for (std::map<std::string, zlink::spot_node_t *>::iterator it =
               g_nodes_by_rid.begin ();
             it != g_nodes_by_rid.end ();) {
            if (it->second == node_)
                it = g_nodes_by_rid.erase (it);
            else
                ++it;
        }
        std::map<zlink::spot_node_t *, std::map<std::string, actor_handle_t *> >::
          iterator node_it = g_actors_by_node.find (node_);
        while (node_it != g_actors_by_node.end ()
               && !node_it->second.empty ()) {
            actor_handle_t *actor = node_it->second.begin ()->second;
            actors_to_delete.push_back (remove_actor_locked (actor, false));
            node_it = g_actors_by_node.find (node_);
        }
    }
}

void note_actor_spot_node_peer_disconnected (
  zlink::spot_node_t *node_, const zlink_routing_id_t *target_node_rid_)
{
    if (!node_ || !valid_routing_id (target_node_rid_))
        return;
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    g_disconnected_actor_routes.insert (
      std::make_pair (node_, routing_id_key (*target_node_rid_)));
}

void erase_actor_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    std::deque<queued_join_request_t *> pending;
    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        g_known_spots.erase (spot_);
        pending.swap (g_join_queues[spot_]);
        g_join_queues.erase (spot_);
        for (std::set<actor_handle_t *>::iterator it = g_actor_handles.begin ();
             it != g_actor_handles.end (); ++it) {
            if ((*it)->joined_spot == spot_)
                clear_actor_joined_spot_locked (*it);
        }
    }
    for (std::deque<queued_join_request_t *>::iterator it = pending.begin ();
         it != pending.end (); ++it) {
        {
            std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
            retire_join_request_locked (*it);
        }
        complete_join_request (*it, ZLINK_REQUEST_TERMINATED);
        release_join_request_after_completion (*it);
    }
}

void erase_actor_stream_bindings (void *stream_)
{
    if (!stream_)
        return;
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    for (std::map<std::string, session_binding_t>::iterator it =
           g_session_bindings.begin ();
         it != g_session_bindings.end ();) {
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
            }
        }
        it = g_session_bindings.erase (it);
    }
}

extern "C" zlink_request_result_t zlink_actor_destroy (void **actor_p_,
                                                        uint32_t timeout_ms_)
{
    if (!actor_p_) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!*actor_p_)
        return ZLINK_REQUEST_OK;

    std::unique_ptr<actor_handle_t> actor_to_delete;
    {
        std::unique_lock<std::timed_mutex> lock (g_actor_mutex,
                                                 std::defer_lock);
        if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
            errno = ETIMEDOUT;
            return ZLINK_REQUEST_TIMED_OUT;
        }
        actor_handle_t *actor = as_actor_locked (*actor_p_);
        if (!actor) {
            errno = EFAULT;
            return ZLINK_REQUEST_INVALID_ARGUMENT;
        }
        if (actor->joined_spot) {
            errno = EBUSY;
            return ZLINK_REQUEST_BUSY;
        }
        if (actor->bound_stream) {
            const std::string bound_key =
              session_key (actor->bound_session_node, actor->bound_stream,
                           &actor->bound_session_rid);
            std::map<std::string, session_binding_t>::const_iterator
              binding_it = g_session_bindings.find (bound_key);
            if (binding_it != g_session_bindings.end ()
                && binding_it->second.in_progress
                && binding_it->second.in_progress_actor_id
                     == actor->actor_id) {
                errno = EBUSY;
                return ZLINK_REQUEST_BUSY;
            }
        }
        actor_to_delete = remove_actor_locked (actor);
        *actor_p_ = NULL;
    }
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_config_result_t zlink_actor_get_ref (void *actor_,
                                                       zlink_actor_ref_t *out_)
{
    if (!actor_ || !out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }

    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    actor_handle_t *actor = as_actor_locked (actor_);
    if (!actor) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    fill_ref (actor, out_);
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_config_result_t zlink_spot_node_actor_lookup (
  void *node_, const char *actor_id_, zlink_actor_ref_t *out_)
{
    if (!node_ || !valid_actor_id (actor_id_) || !out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    const std::map<zlink::spot_node_t *,
                   std::map<std::string, actor_handle_t *> >::iterator node_it =
      g_actors_by_node.find (static_cast<zlink::spot_node_t *> (node_));
    if (node_it == g_actors_by_node.end ()) {
        errno = ENOENT;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    const std::map<std::string, actor_handle_t *>::const_iterator actor_it =
      node_it->second.find (actor_id_);
    if (actor_it == node_it->second.end ()) {
        errno = ENOENT;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
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

    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    actor_admission_t &entry =
      g_admission_handlers[static_cast<zlink::spot_node_t *> (node_)];
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
    if (!node_ || !valid_routing_id (target_node_rid_)
        || !valid_actor_id (actor_id_) || !message_ || !out_) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }

    std::unique_lock<std::timed_mutex> lock (g_actor_mutex, std::defer_lock);
    if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
        errno = ETIMEDOUT;
        return ZLINK_REQUEST_TIMED_OUT;
    }
    zlink::spot_node_t *target = resolve_node_by_rid_locked (*target_node_rid_);
    if (!target) {
        errno = ENOTCONN;
        return ZLINK_REQUEST_NOT_CONNECTED;
    }

    std::map<std::string, actor_handle_t *> &actors = g_actors_by_node[target];
    std::map<std::string, actor_handle_t *>::iterator it =
      actors.find (actor_id_);
    if (it != actors.end ()) {
        out_->status = ZLINK_ACTOR_CREATE_EXISTING;
        fill_ref (it->second, &out_->actor);
        (void) zlink_msg_close (message_);
        (void) zlink_msg_init (message_);
        return ZLINK_REQUEST_OK;
    }

    actor_admission_t admission = g_admission_handlers[target];
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
        (void) zlink_msg_close (message_);
        (void) zlink_msg_init (message_);
        errno = EACCES;
        return ZLINK_REQUEST_REJECTED;
    }

    actor_handle_t *actor =
      create_actor_locked (target, *target_node_rid_, actor_id_);
    if (!actor)
        return errno_to_request_result (errno);

    out_->status = ZLINK_ACTOR_CREATE_CREATED;
    fill_ref (actor, &out_->actor);
    (void) zlink_msg_close (message_);
    (void) zlink_msg_init (message_);
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_request_result_t zlink_spot_node_destroy_remote_actor (
  void *node_, const zlink_actor_ref_t *actor_, uint32_t timeout_ms_)
{
    if (!node_ || !actor_ || !valid_actor_id (actor_->actor_id)
        || !valid_routing_id (&actor_->node_rid)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }

    void *handle = NULL;
    {
        std::unique_lock<std::timed_mutex> lock (g_actor_mutex,
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
            return ZLINK_REQUEST_OK;
        }
        handle = actor;
    }
    return zlink_actor_destroy (&handle, timeout_ms_);
}

extern "C" zlink_submit_result_t zlink_actor_join_spot (
  void *actor_, void *spot_, zlink_msg_t *message_,
  zlink_reply_handler_fn handler_, void *userdata_, zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    LIBZLINK_UNUSED (flags_);
    LIBZLINK_UNUSED (timeout_ms_);
    if (!actor_ || !spot_ || !message_ || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_handle (spot_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    actor_handle_t *actor = NULL;
    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        actor = as_actor_locked (actor_);
        if (!actor) {
            errno = EFAULT;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        if (actor->node != spot->node) {
            errno = EINVAL;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
    }

    zlink_actor_ref_t ref;
    fill_ref (actor, &ref);
    return zlink_spot_node_actor_join_spot (
      actor->node, &ref, &spot->spot_routing_id, message_, handler_, userdata_,
      flags_, timeout_ms_);
}

extern "C" zlink_submit_result_t zlink_spot_node_actor_join_spot (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *message_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    LIBZLINK_UNUSED (flags_);
    if (!node_ || !actor_ref_ || !dest_spot_rid_ || !message_ || !handler_
        || !valid_actor_id (actor_ref_->actor_id)) {
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
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        zlink::spot_node_t *target_node =
          resolve_node_by_rid_locked (actor_ref_->node_rid);
        if (!target_node) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }

        actor_handle_t *actor = NULL;
        std::map<zlink::spot_node_t *, std::map<std::string, actor_handle_t *> >::
          iterator node_it = g_actors_by_node.find (target_node);
        if (node_it != g_actors_by_node.end ()) {
            std::map<std::string, actor_handle_t *>::iterator actor_it =
              node_it->second.find (actor_ref_->actor_id);
            if (actor_it != node_it->second.end ())
                actor = actor_it->second;
        }
        if (!actor) {
            immediate_result = ZLINK_REQUEST_NOT_FOUND;
        } else if (actor_ref_->generation != 0
                   && actor->generation != actor_ref_->generation) {
            immediate_result = ZLINK_REQUEST_CONFLICT;
        }

        if (immediate_result != ZLINK_REQUEST_OK) {
            /* Complete outside the actor lock. */
        } else if (actor->joined_spot
                   && same_routing_id (actor->joined_spot->spot_routing_id,
                                       *dest_spot_rid_)) {
            zlink_msg_t reply;
            zlink_msg_init (&reply);
            handler_ (ZLINK_REQUEST_OK, &reply, 1, userdata_);
            (void) zlink_msg_close (&reply);
            (void) zlink_msg_close (message_);
            (void) zlink_msg_init (message_);
            return ZLINK_SUBMIT_OK;
        }
        if (immediate_result == ZLINK_REQUEST_OK && actor->joined_spot) {
            errno = EBUSY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
    }
    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_join_result (message_, handler_, userdata_,
                                               immediate_result);

    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
        if (!actor) {
            if (errno == ESTALE)
                immediate_result = ZLINK_REQUEST_CONFLICT;
            else
                immediate_result = ZLINK_REQUEST_NOT_FOUND;
        }
        if (immediate_result != ZLINK_REQUEST_OK) {
            /* Complete outside the actor lock. */
        } else {
            spot = NULL;
            for (std::set<spot_handle_t *>::iterator it = g_known_spots.begin ();
                 it != g_known_spots.end (); ++it) {
                if ((*it)->node == actor->node
                    && same_routing_id ((*it)->spot_routing_id,
                                        *dest_spot_rid_)) {
                    spot = *it;
                    break;
                }
            }
            if (!spot)
                immediate_result = ZLINK_REQUEST_NOT_FOUND;
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
            request->handler = handler_;
            request->userdata = userdata_;
            if (zlink_msg_adopt (&request->message, message_)
                != ZLINK_CONFIG_OK) {
                delete request;
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
            request->owns_message = true;
            g_join_queues[spot].push_back (request);
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

extern "C" zlink_config_result_t zlink_actor_leave_spot (void *actor_,
                                                          void *spot_)
{
    if (!actor_ || !spot_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_handle (spot_)) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    actor_handle_t *actor = as_actor_locked (actor_);
    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    if (!actor || actor->node != spot->node) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (actor->joined_spot == spot)
        clear_actor_joined_spot_locked (actor);
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_request_result_t zlink_spot_node_actor_leave_spot (
  void *node_,
  const zlink_actor_ref_t *actor_ref_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint32_t timeout_ms_)
{
    if (!node_ || !actor_ref_ || !dest_spot_rid_
        || !valid_actor_id (actor_ref_->actor_id)) {
        errno = EINVAL;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_REQUEST_INVALID_ARGUMENT;
    }
    std::unique_lock<std::timed_mutex> lock (g_actor_mutex, std::defer_lock);
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
        return ZLINK_REQUEST_OK;
    }
    if (actor->joined_spot
        && same_routing_id (actor->joined_spot->spot_routing_id,
                            *dest_spot_rid_)) {
        clear_actor_joined_spot_locked (actor);
    }
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
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    std::deque<queued_join_request_t *> &queue =
      g_join_queues[static_cast<spot_handle_t *> (spot_)];
    if (queue.empty ()) {
        errno = EAGAIN;
        return ZLINK_RECV_NO_DATA;
    }
    queued_join_request_t *request = queue.front ();
    queue.pop_front ();
    memset (info_out_, 0, sizeof (*info_out_));
    fill_ref (request->actor, &info_out_->actor);
    info_out_->source_node_rid = request->actor->node_rid;
    info_out_->request = request;
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
    if (!spot_ || !info_ || !info_->request || !message_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    queued_join_request_t *request =
      static_cast<queued_join_request_t *> (info_->request);
    actor_handle_t *readable_actor = NULL;
    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        if (g_retired_join_requests.count (request) != 0
            || request->replied || request->spot != spot_) {
            errno = EFSM;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (zlink_msg_adopt (&request->reply, message_) != ZLINK_CONFIG_OK)
            return ZLINK_SUBMIT_INTERNAL_ERROR;
        request->owns_reply = true;
        request->replied = true;
        if (accepted_) {
            request->actor->joined_spot = request->spot;
            request->actor->last_changed_ms = now_ms ();
            update_active_route_locked (request->actor);
            if (!request->actor->queue.empty ())
                readable_actor = request->actor;
        }
        retire_join_request_locked (request);
    }

    if (readable_actor)
        notify_actor_readable (readable_actor);
    complete_join_request (request, accepted_ ? ZLINK_REQUEST_OK
                                              : ZLINK_REQUEST_REJECTED);
    release_join_request_after_completion (request);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_recv_result_t zlink_actor_recv_part (
  void *actor_,
  zlink_actor_recv_info_t *info_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_)
{
    if (!actor_ || !info_out_ || !part_out_ || !has_more_out_) {
        errno = EINVAL;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (flags_ != ZLINK_RECV_FLAGS_DONTWAIT) {
        errno = ENOTSUP;
        return ZLINK_RECV_NOT_SUPPORTED;
    }
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    actor_handle_t *actor = as_actor_locked (actor_);
    if (!actor) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (zlink::spot_dispatch_event_callback_context_t::current_handle ()
        != actor->joined_spot) {
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
    std::unique_lock<std::timed_mutex> lock (g_actor_mutex, std::defer_lock);
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
      g_session_bindings[session_key (node_, stream_, session_rid_)];
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
    std::unique_lock<std::timed_mutex> lock (g_actor_mutex, std::defer_lock);
    if (!lock_actor_request_mutex (&lock, timeout_ms_)) {
        errno = ETIMEDOUT;
        return ZLINK_REQUEST_TIMED_OUT;
    }
    const std::string key = session_key (node_, stream_, session_rid_);
    std::map<std::string, session_binding_t>::iterator binding_it =
      g_session_bindings.find (key);
    if (binding_it == g_session_bindings.end ())
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
        if (it->second.actor) {
            clear_actor_bound_session_locked (it->second.actor, true);
        }
        binding.actors.erase (it);
    }
    if (binding.actors.empty ())
        g_session_bindings.erase (binding_it);
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
    actor_handle_t *actor = NULL;
    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        const std::string key = session_key (node_, stream_, session_rid_);
        std::map<std::string, session_binding_t>::iterator binding_it =
          g_session_bindings.find (key);
        if (binding_it == g_session_bindings.end ()) {
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
        actor = resolve_actor_ref_locked (&it->second.ref);
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
        if (actor->queue.size () >= actor_unread_part_hard_limit) {
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        queued_actor_part_t queued;
        queued.info.actor.node_rid = actor->node_rid;
        strncpy (queued.info.actor.actor_id, actor->actor_id.c_str (),
                 ZLINK_ACTOR_ID_MAX - 1);
        queued.info.actor.generation = actor->generation;
        queued.info.source_node_rid = actor->node_rid;
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
    notify_actor_readable (actor);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_submit_result_t zlink_actor_send_bound_session_msg (
  void *actor_, zlink_msg_t *message_, zlink_send_flags_t flags_)
{
    if (!actor_ || !message_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    void *stream = NULL;
    zlink_routing_id_t session_rid;
    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        actor_handle_t *actor = as_actor_locked (actor_);
        if (!actor) {
            errno = EFAULT;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        const zlink_submit_result_t bound_rc =
          validate_actor_bound_session_locked (actor, &stream, &session_rid);
        if (bound_rc != ZLINK_SUBMIT_OK)
            return bound_rc;
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

extern "C" zlink_submit_result_t zlink_actor_send_bound_session_packet (
  void *actor_,
  zlink_msg_t *header_,
  zlink_msg_t *body_,
  zlink_send_flags_t flags_)
{
    if (!actor_ || !header_ || !body_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    void *stream = NULL;
    zlink_routing_id_t session_rid;
    {
        std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
        actor_handle_t *actor = as_actor_locked (actor_);
        if (!actor) {
            errno = EFAULT;
            return ZLINK_SUBMIT_INVALID_ARGUMENT;
        }
        const zlink_submit_result_t bound_rc =
          validate_actor_bound_session_locked (actor, &stream, &session_rid);
        if (bound_rc != ZLINK_SUBMIT_OK)
            return bound_rc;
    }

    zlink_msg_t frame;
    const zlink_submit_result_t frame_rc =
      build_packet_frame (header_, body_, &frame);
    if (frame_rc != ZLINK_SUBMIT_OK)
        return frame_rc;

    const zlink_submit_result_t send_rc =
      send_temp_to_bound_stream (stream, &session_rid, &frame, flags_);
    if (send_rc != ZLINK_SUBMIT_OK) {
        (void) zlink_msg_close (&frame);
        return send_rc;
    }
    (void) zlink_msg_close (header_);
    (void) zlink_msg_init (header_);
    (void) zlink_msg_close (body_);
    (void) zlink_msg_init (body_);
    return ZLINK_SUBMIT_OK;
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
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    std::map<std::string, zlink_actor_route_t>::const_iterator it =
      g_active_routes.find (actor_id_);
    if (it == g_active_routes.end ()) {
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
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    std::vector<spot_handle_t *> spots;
    for (std::set<spot_handle_t *>::const_iterator it = g_known_spots.begin ();
         it != g_known_spots.end (); ++it) {
        if ((*it)->node == node_)
            spots.push_back (*it);
    }
    if (!entries_) {
        *count_ = spots.size ();
        return ZLINK_CONFIG_OK;
    }
    const size_t limit = std::min (*count_, spots.size ());
    for (size_t i = 0; i != limit; ++i) {
        memset (&entries_[i], 0, sizeof (entries_[i]));
        entries_[i].spot_rid = spots[i]->spot_routing_id;
        entries_[i].dispatch_handler_attached =
          spot_dispatch_handler_attached (spots[i]) ? 1u : 0u;
        for (std::set<actor_handle_t *>::const_iterator actor_it =
               g_actor_handles.begin ();
             actor_it != g_actor_handles.end (); ++actor_it) {
            if ((*actor_it)->joined_spot == spots[i])
                ++entries_[i].joined_actor_count;
        }
        entries_[i].pending_actor_join_count =
          static_cast<uint32_t> (g_join_queues[spots[i]].size ());
        entries_[i].route_synced =
          spots[i]->node && spots[i]->node->spot_owner_route_synced () ? 1u
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
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    std::vector<actor_handle_t *> actors;
    const std::map<zlink::spot_node_t *,
                   std::map<std::string, actor_handle_t *> >::const_iterator
      node_it = g_actors_by_node.find (static_cast<zlink::spot_node_t *> (node_));
    if (node_it != g_actors_by_node.end ()) {
        for (std::map<std::string, actor_handle_t *>::const_iterator it =
               node_it->second.begin ();
             it != node_it->second.end (); ++it)
            actors.push_back (it->second);
    }
    if (!entries_) {
        *count_ = actors.size ();
        return ZLINK_CONFIG_OK;
    }
    const size_t limit = std::min (*count_, actors.size ());
    for (size_t i = 0; i != limit; ++i) {
        memset (&entries_[i], 0, sizeof (entries_[i]));
        fill_ref (actors[i], &entries_[i].actor);
        entries_[i].joined = actors[i]->joined_spot ? 1u : 0u;
        if (actors[i]->joined_spot)
            entries_[i].joined_spot_rid = actors[i]->joined_spot->spot_routing_id;
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
    std::lock_guard<std::timed_mutex> lock (g_actor_mutex);
    std::vector<actor_handle_t *> actors;
    for (std::set<actor_handle_t *>::const_iterator it = g_actor_handles.begin ();
         it != g_actor_handles.end (); ++it) {
        if ((*it)->joined_spot == spot_)
            actors.push_back (*it);
    }
    if (!entries_) {
        *count_ = actors.size ();
        return ZLINK_CONFIG_OK;
    }
    const size_t limit = std::min (*count_, actors.size ());
    for (size_t i = 0; i != limit; ++i)
        fill_ref (actors[i], &entries_[i]);
    *count_ = limit;
    return ZLINK_CONFIG_OK;
}
