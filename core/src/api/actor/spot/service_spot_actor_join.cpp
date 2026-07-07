/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/actor/spot/service_spot_actor_join_internal.hpp"
#include "api/actor/spot/service_spot_actor_state_internal.hpp"
#include "api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "runtime/services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp"
#include "runtime/services/actor/lifecycle/service_spot_actor_lifecycle_context_internal.hpp"
#include "runtime/services/actor/multipart/service_spot_actor_multipart_internal.hpp"
#include "runtime/services/actor/result/service_spot_actor_result_internal.hpp"
#include "runtime/services/actor/service_spot_actor_internal.hpp"
#include "runtime/services/actor/validation/service_spot_actor_validation_internal.hpp"
#include "runtime/services/spot/common/spot_message_parts_internal.hpp"
#include "runtime/services/spot/node/spot_node.hpp"
#include "runtime/services/spot/node/spot_node_access.hpp"
#include "runtime/services/spot/pubsub/spot_subject_access.hpp"
#include "runtime/services/spot/runtime/spot_handle.hpp"
#include "utils/clock.hpp"

#include <new>
#include <string.h>
#include <vector>

namespace
{

using zlink::spot_actor_api_internal::actor_handle_t;
using zlink::spot_actor_api_internal::actor_runtime;

struct idempotent_join_completion_t
{
    zlink_actor_join_spot_handler_fn handler;
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

bool same_join_spot (const spot_handle_t *lhs_, const spot_handle_t *rhs_)
{
    if (!lhs_ || !rhs_)
        return false;
    if (lhs_ == rhs_)
        return true;
    return lhs_->logical_state && lhs_->logical_state == rhs_->logical_state;
}

actor_handle_t *resolve_join_actor_ref_locked (const zlink_actor_ref_t *ref_)
{
    if (!ref_ || !zlink::spot_actor_internal::valid_actor_id (ref_->actor_id)
        || !zlink::spot_actor_internal::valid_routing_id (&ref_->node_rid))
        return NULL;

    zlink::spot_node_t *node = actor_runtime ().nodes.resolve_node_by_rid (ref_->node_rid);
    if (!node) {
        errno = ENOENT;
        return NULL;
    }

    std::map<std::string, actor_handle_t *> &actors =
      zlink::spot_node_access_t::actors_by_id (node);
    const std::map<std::string, actor_handle_t *>::iterator actor_it =
      actors.find (ref_->actor_id);
    if (actor_it == actors.end () || actor_it->second->pending_remote_join) {
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

bool join_node_has_pending_actor_locked (zlink::spot_node_t *node_, const char *actor_id_)
{
    if (!node_ || !actor_id_)
        return false;
    std::map<std::string, actor_handle_t *> &actors =
      zlink::spot_node_access_t::actors_by_id (node_);
    std::map<std::string, actor_handle_t *>::const_iterator actor_it = actors.find (actor_id_);
    if (actor_it != actors.end () && actor_it->second->pending_remote_join)
        return true;
    return actor_runtime ().joins.has_pending_remote_actor (node_, actor_id_);
}

spot_handle_t *
find_join_spot_facade_locked (zlink::spot_node_t *node_,
                              const std::shared_ptr<spot_logical_state_t> &state_)
{
    return actor_runtime ().nodes.find_spot_for_state (node_, state_);
}

}

namespace zlink
{
namespace spot_actor_api_internal
{

void complete_join_request (queued_join_request_t *request_, zlink_request_result_t result_);

bool join_request_live_locked (queued_join_request_t *request_)
{
    return actor_runtime ().joins.is_live (request_);
}

void index_join_request_locked (queued_join_request_t *request_)
{
    if (!request_ || request_->indexed || request_->replied)
        return;

    actor_runtime ().joins.mark_live (request_);
    actor_runtime ().joins.increment_actor_pending (request_->actor);
    actor_runtime ().joins.increment_spot_pending (join_queue_key (request_));
    actor_runtime ().joins.track_pending_remote_actor (request_);
    request_->indexed = true;
}

void unindex_join_request_locked (queued_join_request_t *request_)
{
    if (!request_ || !request_->indexed)
        return;

    actor_runtime ().joins.unmark_live (request_);
    actor_runtime ().joins.decrement_actor_pending (request_->actor);
    actor_runtime ().joins.decrement_spot_pending (join_queue_key (request_));
    actor_runtime ().joins.untrack_pending_remote_actor (request_);
    request_->indexed = false;
}

void retire_join_request_locked (queued_join_request_t *request_)
{
    if (!request_)
        return;
    unindex_join_request_locked (request_);
    remove_join_pending_target_locked (request_);
    zlink::spot_clear_msg_parts (&request_->message_parts);
    request_->replied = true;
}

void release_join_request_after_completion (queued_join_request_t *request_)
{
    if (!request_)
        return;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        unindex_join_request_locked (request_);
        timeout_task.swap (request_->timeout_task);
    }
    zlink::request_timeout::cancel (timeout_task);
    delete request_;
}

void remove_pending_join_request_locked (queued_join_request_t *request_)
{
    actor_runtime ().joins.remove_queued (request_);
}

void handle_join_timeout (void *userdata_)
{
    queued_join_request_t *request = static_cast<queued_join_request_t *> (userdata_);
    if (!request)
        return;
    bool timed_out = false;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
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

void schedule_join_timeout (queued_join_request_t *request_, uint32_t timeout_ms_)
{
    if (!request_ || timeout_ms_ == 0)
        return;
    request_->timeout_task =
      zlink::request_timeout::schedule (timeout_ms_, handle_join_timeout, request_);
}

uint64_t next_join_commit_epoch_locked ()
{
    uint64_t epoch = actor_runtime ().next_join_epoch++;
    if (actor_runtime ().next_join_epoch == 0)
        actor_runtime ().next_join_epoch = 1;
    return epoch == 0 ? actor_runtime ().next_join_epoch++ : epoch;
}

zlink_routing_id_t join_actor_current_spot_rid_locked (const actor_handle_t *actor_)
{
    zlink_routing_id_t rid;
    memset (&rid, 0, sizeof (rid));
    if (actor_ && actor_->joined_spot_state)
        return actor_->joined_spot_state->routing_id;
    return rid;
}

bool join_actor_has_pending_request_locked (const actor_handle_t *actor_)
{
    return actor_runtime ().joins.actor_has_pending (actor_);
}

zlink_spot_actor_lifecycle_info_t make_join_lifecycle_info (
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

void set_join_actor_spot_locked (actor_handle_t *actor_, spot_handle_t *spot_)
{
    if (!actor_)
        return;
    actor_->joined_spot_state =
      spot_ ? spot_->logical_state : std::shared_ptr<spot_logical_state_t> ();
    actor_->last_changed_ms = zlink::clock_t ().now_ms ();
    actor_runtime ().routes.publish_active (actor_, false);
}

void create_join_active_route_locked (actor_handle_t *actor_)
{
    actor_runtime ().routes.publish_active (actor_, true);
}

bool actor_in_entry_spot_locked (const actor_handle_t *actor_)
{
    return actor_ && actor_->joined_spot_state && actor_->joined_spot_state->entry;
}

uint64_t next_join_generation_for_node_locked (zlink::spot_node_t *node_)
{
    uint64_t &next = zlink::spot_node_access_t::next_actor_generation (node_);
    if (next == 0)
        next = 1;
    return next++;
}

zlink_request_result_t commit_accepted_join_locked (queued_join_request_t *request_,
                                                    actor_handle_t **readable_actor_out_)
{
    if (readable_actor_out_)
        *readable_actor_out_ = NULL;
    if (!request_)
        return ZLINK_REQUEST_INTERNAL_ERROR;

    zlink_spot_actor_lifecycle_info_t source_leave_info;
    zlink_spot_actor_lifecycle_info_t target_join_info;
    std::shared_ptr<spot_logical_state_t> source_leave_spot;
    std::shared_ptr<spot_logical_state_t> target_join_spot;
    memset (&source_leave_info, 0, sizeof (source_leave_info));
    memset (&target_join_info, 0, sizeof (target_join_info));

    if (request_->entry_spot_join && request_->actor && request_->spot_state
        && zlink::spot_actor_internal::same_routing_id (request_->actor->node_rid,
                                                        request_->target_node_rid)
        && actor_in_entry_spot_locked (request_->actor)) {
        if (!actor_runtime ().routes.active_matches (request_->actor))
            create_join_active_route_locked (request_->actor);
        return ZLINK_REQUEST_OK;
    }

    if (request_->remote) {
        actor_handle_t *source = request_->actor;
        actor_handle_t *target = request_->pending_target;
        if (!target || !target->pending_remote_join)
            return ZLINK_REQUEST_CONFLICT;
        const actor_bound_session_transfer_t bound_session_transfer =
          actor_runtime ().sessions.capture_bound_session (source);

        zlink_actor_ref_t source_ref;
        zlink_actor_ref_t target_ref;
        if (source)
            source_ref = source->ref_cache;
        else
            source_ref = request_->source_actor_ref;
        target_ref = target->ref_cache;
        const uint64_t source_epoch = source ? source->join_epoch : 0;
        target->join_epoch = request_->join_epoch;
        if (source) {
            source_leave_spot = source->joined_spot_state;
            source_leave_info = make_join_lifecycle_info (
              source_ref, target_ref, request_->source_spot_rid, request_->spot_state->routing_id,
              source_epoch);
        }
        target_join_spot = request_->spot_state;
        target_join_info =
          make_join_lifecycle_info (source_ref, target_ref, request_->source_spot_rid,
                                    request_->spot_state->routing_id, target->join_epoch);
        target->pending_remote_join = false;
        set_join_actor_spot_locked (target, request_->spot);
        request_->pending_target = NULL;
        if (source)
            remove_join_actor_locked (source, false);
        actor_runtime ().sessions.transfer_bound_session (bound_session_transfer, target,
                                                          zlink::clock_t ().now_ms ());
        create_join_active_route_locked (target);
        if (readable_actor_out_ && !target->queue.empty ())
            *readable_actor_out_ = target;
    } else {
        if (!request_->actor)
            return ZLINK_REQUEST_CONFLICT;
        const zlink_actor_ref_t actor_ref = request_->actor->ref_cache;
        const uint64_t source_epoch = request_->actor->join_epoch;
        request_->actor->join_epoch = request_->join_epoch;
        source_leave_spot = request_->actor->joined_spot_state;
        source_leave_info =
          make_join_lifecycle_info (actor_ref, actor_ref, request_->source_spot_rid,
                                    request_->spot_state->routing_id, source_epoch);
        target_join_spot = request_->spot_state;
        target_join_info = make_join_lifecycle_info (
          actor_ref, actor_ref, request_->source_spot_rid, request_->spot_state->routing_id,
          request_->actor->join_epoch);
        set_join_actor_spot_locked (request_->actor, request_->spot);
        create_join_active_route_locked (request_->actor);
        if (readable_actor_out_ && !request_->actor->queue.empty ())
            *readable_actor_out_ = request_->actor;
    }

    if (source_leave_spot)
        schedule_join_lifecycle_event_locked (source_leave_spot,
                                              ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT,
                                              source_leave_info);
    schedule_join_lifecycle_event_locked (target_join_spot, ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED,
                                          target_join_info);
    return ZLINK_REQUEST_OK;
}

queued_join_request_t *
find_external_join_reply_request_locked (zlink::spot_node_t *node_,
                                         const zlink_routing_id_t *reply_source_node_rid_,
                                         const zlink::spot_actor_gateway::frame_t &frame_,
                                         bool entry_spot_join_)
{
    if (!node_ || !reply_source_node_rid_)
        return NULL;
    for (std::set<queued_join_request_t *>::iterator it =
           actor_runtime ().joins.live_requests.begin ();
         it != actor_runtime ().joins.live_requests.end (); ++it) {
        queued_join_request_t *request = *it;
        if (!request || request->entry_spot_join != entry_spot_join_ || !request->remote
            || request->target_node || !request->actor || request->replied)
            continue;
        if (request->join_epoch != frame_.request_id)
            continue;
        if (strcmp (request->actor->actor_id.c_str (), frame_.actor_id) != 0)
            continue;
        if (!zlink::spot_actor_internal::same_routing_id (request->target_node_rid,
                                                          *reply_source_node_rid_))
            continue;
        return request;
    }
    return NULL;
}

int enqueue_actor_gateway_entry_join_request_locked (
  zlink::spot_node_t *node_,
  const zlink_routing_id_t *source_node_rid_,
  const zlink::spot_actor_gateway::frame_t &frame_,
  zlink_msg_t *parts_,
  size_t part_count_,
  bool entry_spot_join_,
  spot_handle_t **notify_spot_out_)
{
    if (notify_spot_out_)
        *notify_spot_out_ = NULL;
    if (!node_ || !source_node_rid_
        || !zlink::spot_actor_internal::valid_routing_id (source_node_rid_)
        || !zlink::spot_actor_internal::valid_multipart_payload (parts_, part_count_)) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t target_node_rid;
    memset (&target_node_rid, 0, sizeof (target_node_rid));
    if (node_->node_routing_id (&target_node_rid) != 0)
        return -1;

    std::shared_ptr<spot_logical_state_t> target_state =
      entry_spot_join_ ? zlink::spot_node_access_t::entry_spot_state (node_)
                       : zlink::spot_node_access_t::lookup_spot_state (node_, &frame_.session_rid);
    spot_handle_t *spot = actor_runtime ().nodes.find_spot_for_state (node_, target_state);
    if (!spot) {
        errno = ENOENT;
        return -1;
    }
    if (zlink::spot_node_access_t::actors_by_id (node_).count (frame_.actor_id) != 0
        || actor_runtime ().joins.has_pending_remote_actor (node_, frame_.actor_id)) {
        errno = EEXIST;
        return -1;
    }

    queued_join_request_t *request = new (std::nothrow) queued_join_request_t ();
    if (!request) {
        errno = ENOMEM;
        return -1;
    }
    request->spot = spot;
    request->spot_state = target_state;
    request->target_node = node_;
    request->remote = true;
    request->target_node_rid = target_node_rid;
    request->source_node_rid = *source_node_rid_;
    if (entry_spot_join_)
        request->source_spot_rid = frame_.session_rid;
    request->source_actor_ref.node_rid = *source_node_rid_;
    strncpy (request->source_actor_ref.actor_id, frame_.actor_id, ZLINK_ACTOR_ID_MAX - 1);
    request->source_actor_ref.generation = frame_.generation;
    request->target_actor_ref.node_rid = target_node_rid;
    strncpy (request->target_actor_ref.actor_id, frame_.actor_id, ZLINK_ACTOR_ID_MAX - 1);
    request->target_actor_ref.generation = next_join_generation_for_node_locked (node_);
    request->entry_spot_join = entry_spot_join_;
    request->join_epoch = frame_.request_id;
    request->pending_target = create_join_actor_locked_with_generation (
      node_, target_node_rid, frame_.actor_id, request->target_actor_ref.generation, true);
    if (!request->pending_target) {
        delete request;
        return -1;
    }

    const zlink_submit_result_t adopt_rc =
      zlink::spot_actor_internal::adopt_multipart_payload (&request->message_parts, parts_,
                                                           part_count_);
    if (adopt_rc != ZLINK_SUBMIT_OK) {
        remove_join_pending_target_locked (request);
        delete request;
        errno = EIO;
        return -1;
    }

    index_join_request_locked (request);
    actor_runtime ().joins.enqueue (request);
    if (notify_spot_out_)
        *notify_spot_out_ = spot;
    return 0;
}

int process_actor_gateway_entry_join_reply_locked (zlink::spot_node_t *node_,
                                                   const zlink_routing_id_t *reply_source_node_rid_,
                                                   const zlink::spot_actor_gateway::frame_t &frame_,
                                                   zlink_msg_t *parts_,
                                                   size_t part_count_,
                                                   bool entry_spot_join_,
                                                   join_request_completion_batch_t *completed_out_,
                                                   actor_handle_t **source_actor_to_remove_out_)
{
    if (completed_out_)
        completed_out_->requests.clear ();
    if (source_actor_to_remove_out_)
        *source_actor_to_remove_out_ = NULL;
    if (!node_ || !reply_source_node_rid_
        || !zlink::spot_actor_internal::valid_multipart_payload (parts_, part_count_)) {
        errno = EINVAL;
        return -1;
    }

    queued_join_request_t *request = find_external_join_reply_request_locked (
      node_, reply_source_node_rid_, frame_, entry_spot_join_);
    if (!request) {
        errno = ENOENT;
        return -1;
    }

    const zlink_submit_result_t adopt_rc =
      zlink::spot_actor_internal::adopt_multipart_payload (&request->reply_parts, parts_,
                                                           part_count_);
    if (adopt_rc != ZLINK_SUBMIT_OK) {
        errno = EIO;
        return -1;
    }

    request->target_actor_ref.node_rid = *reply_source_node_rid_;
    strncpy (request->target_actor_ref.actor_id, frame_.actor_id, ZLINK_ACTOR_ID_MAX - 1);
    request->target_actor_ref.generation = frame_.generation;
    request->join_result_code = frame_.join_result_code;
    request->source_spot_rid = frame_.session_rid;

    actor_handle_t *source = request->actor;
    if (frame_.join_result_code == 0 && source) {
        const actor_bound_session_transfer_t transfer =
          actor_runtime ().sessions.capture_bound_session (source);
        if (transfer.valid)
            actor_runtime ().sessions.bind_actor_ref (transfer.stream, transfer.session_rid,
                                                      request->target_actor_ref);
        if (source_actor_to_remove_out_)
            *source_actor_to_remove_out_ = source;
    }

    retire_join_request_locked (request);
    if (completed_out_)
        completed_out_->requests.push_back (request);
    return 0;
}

int process_actor_gateway_join_packet_locked (
  zlink::spot_node_t *node_,
  const zlink_routing_id_t *source_node_rid_,
  const zlink::spot_actor_gateway::frame_t &frame_,
  zlink_msg_t *parts_,
  size_t part_count_,
  bool *handled_out_,
  spot_handle_t **notify_spot_out_,
  join_request_completion_batch_t *completed_out_,
  actor_handle_t **source_actor_to_remove_out_)
{
    if (handled_out_)
        *handled_out_ = true;
    if (frame_.kind == zlink::spot_actor_gateway::packet_entry_join_request) {
        return enqueue_actor_gateway_entry_join_request_locked (
          node_, source_node_rid_, frame_, parts_, part_count_, true, notify_spot_out_);
    }
    if (frame_.kind == zlink::spot_actor_gateway::packet_entry_join_reply) {
        return process_actor_gateway_entry_join_reply_locked (
          node_, source_node_rid_, frame_, parts_, part_count_, true, completed_out_,
          source_actor_to_remove_out_);
    }
    if (frame_.kind == zlink::spot_actor_gateway::packet_spot_join_request) {
        return enqueue_actor_gateway_entry_join_request_locked (
          node_, source_node_rid_, frame_, parts_, part_count_, false, notify_spot_out_);
    }
    if (frame_.kind == zlink::spot_actor_gateway::packet_spot_join_reply) {
        return process_actor_gateway_entry_join_reply_locked (
          node_, source_node_rid_, frame_, parts_, part_count_, false, completed_out_,
          source_actor_to_remove_out_);
    }
    if (handled_out_)
        *handled_out_ = false;
    return -1;
}

void complete_join_request (queued_join_request_t *request_, zlink_request_result_t result_)
{
    if (!request_ || (!request_->handler && !request_->entry_handler))
        return;

    zlink_actor_join_result_t result;
    zlink_actor_join_entry_spot_result_t entry_result;
    memset (&result, 0, sizeof (result));
    memset (&entry_result, 0, sizeof (entry_result));
    result.result = result_;
    result.join_result_code = request_->join_result_code;
    entry_result.result = result_;
    entry_result.join_result_code = request_->join_result_code;
    entry_result.target_node_rid = request_->target_node_rid;
    if (result_ == ZLINK_REQUEST_OK) {
        if (request_->join_result_code != 0) {
            if (request_->actor)
                result.actor = request_->actor->ref_cache;
            result.joined_spot_rid = request_->source_spot_rid;
        } else if (request_->remote)
            result.actor = request_->target_actor_ref;
        else if (request_->actor)
            result.actor = request_->actor->ref_cache;
        if (request_->join_result_code == 0 && request_->spot_state)
            result.joined_spot_rid = request_->spot_state->routing_id;
        else if (request_->join_result_code == 0 && request_->entry_spot_join
                 && zlink::spot_actor_internal::valid_routing_id (&request_->source_spot_rid))
            result.joined_spot_rid = request_->source_spot_rid;
        else if (request_->join_result_code == 0 && request_->remote
                 && zlink::spot_actor_internal::valid_routing_id (&request_->source_spot_rid))
            result.joined_spot_rid = request_->source_spot_rid;
        result.join_epoch = request_->join_epoch;
        entry_result.actor = result.actor;
        entry_result.joined_spot_rid = result.joined_spot_rid;
        entry_result.join_epoch = result.join_epoch;
        entry_result.flags = result.flags;
    }

    if (!request_->reply_parts.empty ()) {
        std::vector<zlink_msg_t> reply_parts;
        if (zlink::spot_move_msg_parts (&request_->reply_parts, &reply_parts) != 0) {
            result.result = ZLINK_REQUEST_INTERNAL_ERROR;
            entry_result.result = ZLINK_REQUEST_INTERNAL_ERROR;
            if (request_->entry_spot_join)
                request_->entry_handler (&entry_result, NULL, 0, request_->userdata);
            else
                request_->handler (&result, NULL, 0, request_->userdata);
            return;
        }
        if (request_->entry_spot_join)
            request_->entry_handler (&entry_result, reply_parts.empty () ? NULL : &reply_parts[0],
                                     reply_parts.size (), request_->userdata);
        else
            request_->handler (&result, reply_parts.empty () ? NULL : &reply_parts[0],
                               reply_parts.size (), request_->userdata);
    } else {
        if (request_->entry_spot_join)
            request_->entry_handler (&entry_result, NULL, 0, request_->userdata);
        else
            request_->handler (&result, NULL, 0, request_->userdata);
    }
}

void complete_and_release_join_request (queued_join_request_t *request_,
                                        zlink_request_result_t result_)
{
    complete_join_request (request_, result_);
    release_join_request_after_completion (request_);
}

void collect_join_spot_facade_erase_locked (spot_handle_t *spot_,
                                            std::deque<queued_join_request_t *> *pending_)
{
    if (!spot_)
        return;
    const std::shared_ptr<spot_logical_state_t> logical_state = spot_->logical_state;
    spot_handle_t *replacement = actor_runtime ().nodes.find_replacement_spot (spot_,
                                                                               logical_state);
    actor_runtime ().nodes.erase_spot (spot_);
    if (replacement) {
        actor_runtime ().joins.replace_live_spot (spot_, replacement);
        return;
    }
    spot_logical_state_t *key = join_queue_key (logical_state);
    if (logical_state && logical_state->entry) {
        (void) key;
        actor_runtime ().joins.replace_live_spot (spot_, NULL);
        actor_runtime ().lifecycle.clear (logical_state.get ());
        return;
    }
    actor_runtime ().lifecycle.clear (logical_state.get ());
    actor_runtime ().joins.take_queue (key, pending_);
    actor_runtime ().joins.collect_live_for_state (logical_state, pending_);
}

bool join_spot_has_joined_or_pending_actor_locked (spot_handle_t *spot_)
{
    if (!spot_ || (spot_->logical_state && spot_->logical_state->entry))
        return false;
    if (actor_runtime ().nodes.has_peer_spot_facade (spot_))
        return false;
    std::vector<actor_handle_t *> actors;
    actor_runtime ().nodes.collect_actor_handles (&actors);
    for (std::vector<actor_handle_t *>::const_iterator it = actors.begin (); it != actors.end ();
         ++it) {
        if ((*it)->joined_spot_state && (*it)->joined_spot_state == spot_->logical_state)
            return true;
    }
    return actor_runtime ().joins.spot_has_pending (join_queue_key (spot_->logical_state));
}

void collect_join_stream_queued_erase_requests_locked (
  void *stream_,
  std::deque<queued_join_request_t *> *queued_aborts_)
{
    if (!stream_ || !queued_aborts_)
        return;
    actor_runtime ().joins.drain_queued_for_stream (stream_, queued_aborts_);
}

void collect_join_stream_live_erase_requests_locked (
  void *stream_,
  const std::deque<queued_join_request_t *> &queued_aborts_,
  std::vector<queued_join_request_t *> *live_aborts_)
{
    if (!stream_ || !live_aborts_)
        return;
    actor_runtime ().joins.collect_live_for_stream (stream_, queued_aborts_, live_aborts_);
}

void abort_join_requests_for_stream_locked (void *stream_,
                                            join_actor_session_clear_fn clear_session_,
                                            void *userdata_,
                                            join_request_completion_batch_t *aborted_)
{
    if (!stream_ || !aborted_)
        return;

    std::vector<queued_join_request_t *> received_aborts;
    collect_join_stream_queued_erase_requests_locked (stream_, &aborted_->requests);
    for (std::deque<queued_join_request_t *>::iterator it = aborted_->requests.begin ();
         it != aborted_->requests.end (); ++it) {
        if (clear_session_)
            clear_session_ ((*it)->actor, userdata_);
        retire_join_request_locked (*it);
    }
    collect_join_stream_live_erase_requests_locked (stream_, aborted_->requests,
                                                    &received_aborts);
    for (std::vector<queued_join_request_t *>::iterator it = received_aborts.begin ();
         it != received_aborts.end (); ++it) {
        queued_join_request_t *request = *it;
        remove_pending_join_request_locked (request);
        if (clear_session_)
            clear_session_ (request->actor, userdata_);
        retire_join_request_locked (request);
        aborted_->requests.push_back (request);
    }
}

void complete_and_release_join_requests (join_request_completion_batch_t *requests_,
                                         zlink_request_result_t result_)
{
    if (!requests_)
        return;
    for (std::deque<queued_join_request_t *>::iterator it = requests_->requests.begin ();
         it != requests_->requests.end (); ++it)
        complete_and_release_join_request (*it, result_);
    requests_->requests.clear ();
}

zlink_submit_result_t complete_immediate_join_result (zlink_msg_t *parts_,
                                                      size_t part_count_,
                                                      zlink_actor_join_spot_handler_fn handler_,
                                                      void *userdata_,
                                                      zlink_request_result_t result_)
{
    zlink::spot_actor_internal::consume_multipart_payload (parts_, part_count_);
    zlink_actor_join_result_t result;
    memset (&result, 0, sizeof (result));
    result.result = result_;
    handler_ (&result, NULL, 0, userdata_);
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t
complete_immediate_entry_join_result (zlink_msg_t *parts_,
                                      size_t part_count_,
                                      zlink_actor_join_entry_spot_handler_fn handler_,
                                      void *userdata_,
                                      const zlink_routing_id_t *target_node_rid_,
                                      zlink_request_result_t result_)
{
    zlink::spot_actor_internal::consume_multipart_payload (parts_, part_count_);
    zlink_actor_join_entry_spot_result_t result;
    memset (&result, 0, sizeof (result));
    result.result = result_;
    if (target_node_rid_)
        result.target_node_rid = *target_node_rid_;
    handler_ (&result, NULL, 0, userdata_);
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t complete_idempotent_join_async (zlink_msg_t *parts_,
                                                      size_t part_count_,
                                                      zlink_actor_join_spot_handler_fn handler_,
                                                      void *userdata_,
                                                      const zlink_actor_ref_t *actor_,
                                                      const zlink_routing_id_t *spot_rid_,
                                                      uint64_t join_epoch_)
{
    zlink::spot_actor_internal::consume_multipart_payload (parts_, part_count_);
    idempotent_join_completion_t *completion = new (std::nothrow) idempotent_join_completion_t;
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
    (void) zlink::request_timeout::schedule (1, complete_idempotent_join_scheduled, completion,
                                             cleanup_idempotent_join_completion);
    return ZLINK_SUBMIT_OK;
}

}
}

using namespace zlink::spot_actor_api_internal;

void erase_actor_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    std::deque<queued_join_request_t *> pending;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        collect_join_spot_facade_erase_locked (spot_, &pending);
    }
    for (std::deque<queued_join_request_t *>::iterator it = pending.begin (); it != pending.end ();
         ++it) {
        {
            std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
            retire_join_request_locked (*it);
        }
        complete_and_release_join_request (*it, ZLINK_REQUEST_TERMINATED);
    }
}

extern "C" int zlink_spot_has_joined_or_pending_actor (void *spot_)
{
    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    if (!spot)
        return 0;
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    return join_spot_has_joined_or_pending_actor_locked (spot) ? 1 : 0;
}

extern "C" zlink_submit_result_t
zlink_spot_node_actor_join_spot (void *node_,
                                 const zlink_actor_ref_t *actor_ref_,
                                 const zlink_routing_id_t *dest_node_rid_,
                                 const zlink_routing_id_t *dest_spot_rid_,
                                 zlink_msg_t *parts_,
                                 size_t part_count_,
                                 zlink_actor_join_spot_handler_fn handler_,
                                 void *userdata_,
                                 zlink_send_flags_t flags_,
                                 uint32_t timeout_ms_)
{
    if ((flags_ & ~ZLINK_DONTWAIT) != 0) {
        errno = ENOTSUP;
        return ZLINK_SUBMIT_NOT_SUPPORTED;
    }
    if (!node_ || !actor_ref_ || !dest_node_rid_ || !dest_spot_rid_ || !handler_
        || !zlink::spot_actor_internal::valid_actor_id (actor_ref_->actor_id)
        || !zlink::spot_actor_internal::valid_routing_id (&actor_ref_->node_rid)
        || !zlink::spot_actor_internal::valid_routing_id (dest_node_rid_)
        || !zlink::spot_actor_internal::valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!zlink::spot_actor_internal::valid_multipart_payload (parts_, part_count_))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (!as_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (zlink::spot_actor_lifecycle::reenters_same_actor (actor_ref_)) {
        errno = EDEADLK;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    queued_join_request_t *request = NULL;
    spot_handle_t *spot = NULL;
    zlink_request_result_t immediate_result = ZLINK_REQUEST_OK;
    bool external_gateway_join = false;
    zlink::spot_node_t *external_source_node = NULL;
    zlink_routing_id_t external_source_node_rid;
    memset (&external_source_node_rid, 0, sizeof (external_source_node_rid));
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        zlink::spot_node_t *source_node =
          actor_runtime ().nodes.resolve_node_by_rid (actor_ref_->node_rid);
        if (!source_node) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }

        actor_handle_t *actor = NULL;
        std::map<std::string, actor_handle_t *> &actors =
          zlink::spot_node_access_t::actors_by_id (source_node);
        std::map<std::string, actor_handle_t *>::iterator actor_it =
          actors.find (actor_ref_->actor_id);
        if (actor_it != actors.end ())
            actor = actor_it->second;
        if (!actor) {
            immediate_result = ZLINK_REQUEST_NOT_FOUND;
        } else if (actor_ref_->generation != 0 && actor->generation != actor_ref_->generation) {
            immediate_result = ZLINK_REQUEST_CONFLICT;
        }

        if (immediate_result != ZLINK_REQUEST_OK) {
            /* Complete outside the actor lock. */
        } else if (actor_runtime ().joins.actor_has_pending (actor)) {
            errno = EBUSY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
    }
    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_join_result (parts_, part_count_, handler_, userdata_,
                                               immediate_result);

    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        actor_handle_t *actor = resolve_join_actor_ref_locked (actor_ref_);
        if (!actor)
            immediate_result =
              zlink::spot_actor_internal::actor_missing_request_result_from_errno ();
        zlink::spot_node_t *target_node = immediate_result == ZLINK_REQUEST_OK
                                            ? actor_runtime ().nodes.resolve_node_by_rid (
                                                *dest_node_rid_)
                                            : NULL;
        if (immediate_result == ZLINK_REQUEST_OK && !target_node) {
            request = new (std::nothrow) queued_join_request_t ();
            if (!request) {
                errno = ENOMEM;
                return ZLINK_SUBMIT_INTERNAL_ERROR;
            }
            request->actor = actor;
            request->target_node = NULL;
            request->remote = true;
            request->target_node_rid = *dest_node_rid_;
            request->source_node_rid = actor->node_rid;
            request->source_spot_rid = join_actor_current_spot_rid_locked (actor);
            request->source_actor_ref = actor->ref_cache;
            request->target_actor_ref.node_rid = *dest_node_rid_;
            strncpy (request->target_actor_ref.actor_id, actor->actor_id.c_str (),
                     ZLINK_ACTOR_ID_MAX - 1);
            request->handler = handler_;
            request->userdata = userdata_;
            request->join_epoch = next_join_commit_epoch_locked ();
            index_join_request_locked (request);
            external_gateway_join = true;
            external_source_node = actor_runtime ().nodes.resolve_node_by_rid (actor->node_rid);
            external_source_node_rid = actor->node_rid;
        } else if (immediate_result != ZLINK_REQUEST_OK) {
            /* Complete outside the actor lock. */
        } else {
            std::shared_ptr<spot_logical_state_t> target_state =
              zlink::spot_node_access_t::lookup_spot_state (target_node, dest_spot_rid_);
            spot = find_join_spot_facade_locked (target_node, target_state);
            if (!spot)
                immediate_result = ZLINK_REQUEST_NOT_FOUND;
            else if (spot->logical_state && spot->logical_state->entry) {
                errno = EINVAL;
                return ZLINK_SUBMIT_INVALID_ARGUMENT;
            } else if (zlink::spot_actor_internal::same_routing_id (actor->node_rid,
                                                                    *dest_node_rid_)
                       && actor->joined_spot_state
                       && zlink::spot_actor_internal::same_routing_id (
                         join_actor_current_spot_rid_locked (actor), *dest_spot_rid_)) {
                const zlink_actor_ref_t current_ref = actor->ref_cache;
                const zlink_routing_id_t current_spot = join_actor_current_spot_rid_locked (actor);
                if (!actor_runtime ().routes.active_matches (actor))
                    create_join_active_route_locked (actor);
                return complete_idempotent_join_async (parts_, part_count_, handler_, userdata_,
                                                       &current_ref, &current_spot,
                                                       actor->join_epoch);
            } else if (!zlink::spot_actor_internal::same_routing_id (actor->node_rid,
                                                                     *dest_node_rid_)
                       && actor_runtime ().routes.is_disconnected (actor->node,
                                                                   *dest_node_rid_)) {
                immediate_result = ZLINK_REQUEST_NOT_CONNECTED;
            } else if (!zlink::spot_actor_internal::same_routing_id (actor->node_rid,
                                                                     *dest_node_rid_)) {
                std::map<std::string, actor_handle_t *> &target_actors =
                  zlink::spot_node_access_t::actors_by_id (target_node);
                if (target_actors.count (actor->actor_id) != 0
                    || join_node_has_pending_actor_locked (target_node,
                                                           actor->actor_id.c_str ())) {
                    immediate_result = ZLINK_REQUEST_CONFLICT;
                }
            }
        }
        if (immediate_result != ZLINK_REQUEST_OK)
            request = NULL;
        else if (!external_gateway_join) {
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
              !zlink::spot_actor_internal::same_routing_id (actor->node_rid, *dest_node_rid_);
            request->target_node_rid = *dest_node_rid_;
            request->source_spot_rid = join_actor_current_spot_rid_locked (actor);
            if (request->remote) {
                request->target_actor_ref.node_rid = *dest_node_rid_;
                strncpy (request->target_actor_ref.actor_id, actor->actor_id.c_str (),
                         ZLINK_ACTOR_ID_MAX - 1);
                request->target_actor_ref.generation =
                  next_join_generation_for_node_locked (target_node);
                request->pending_target = create_join_actor_locked_with_generation (
                  target_node, *dest_node_rid_, actor->actor_id.c_str (),
                  request->target_actor_ref.generation, true);
                if (!request->pending_target) {
                    delete request;
                    return errno == EBUSY ? ZLINK_SUBMIT_INVALID_STATE
                                          : zlink::spot_actor_internal::errno_to_submit_result (
                                              errno);
                }
            } else {
                request->target_actor_ref = actor->ref_cache;
            }
            request->handler = handler_;
            request->userdata = userdata_;
            request->join_epoch = next_join_commit_epoch_locked ();
            const zlink_submit_result_t adopt_rc =
              zlink::spot_actor_internal::adopt_multipart_payload (&request->message_parts, parts_,
                                                                   part_count_);
            if (adopt_rc != ZLINK_SUBMIT_OK) {
                if (request->pending_target)
                    remove_join_pending_target_locked (request);
                delete request;
                return adopt_rc;
            }
            index_join_request_locked (request);
            actor_runtime ().joins.enqueue (request);
        }
    }

    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_join_result (parts_, part_count_, handler_, userdata_,
                                               immediate_result);

    if (external_gateway_join) {
        const zlink_submit_result_t send_rc =
          zlink::spot_actor_internal::send_actor_gateway_multipart_from_source (
            external_source_node, external_source_node_rid, *dest_node_rid_,
            zlink::spot_actor_gateway::packet_spot_join_request, *dest_spot_rid_,
            actor_ref_->actor_id, actor_ref_->generation, request->join_epoch, 0, parts_,
            part_count_, flags_);
        if (send_rc != ZLINK_SUBMIT_OK) {
            const zlink_request_result_t failure =
              zlink::spot_actor_internal::errno_to_request_result (errno);
            {
                std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
                retire_join_request_locked (request);
            }
            complete_and_release_join_request (request, failure);
            return ZLINK_SUBMIT_OK;
        }
        schedule_join_timeout (request, timeout_ms_);
        return ZLINK_SUBMIT_OK;
    }

    schedule_join_timeout (request, timeout_ms_);
    zlink_spot_notify_dispatch_info (spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE,
                                     ZLINK_SPOT_DISPATCH_SUBJECT_SPOT, spot);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_submit_result_t
zlink_spot_node_actor_join_entry_spot (void *node_,
                                       const zlink_actor_ref_t *actor_,
                                       const zlink_routing_id_t *dest_node_rid_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_,
                                       zlink_actor_join_entry_spot_handler_fn handler_,
                                       void *userdata_,
                                       zlink_send_flags_t flags_,
                                       uint32_t timeout_ms_)
{
    if (!node_ || !actor_ || !dest_node_rid_ || !handler_
        || !zlink::spot_actor_internal::valid_actor_id (actor_->actor_id)
        || !zlink::spot_actor_internal::valid_routing_id (&actor_->node_rid)
        || !zlink::spot_actor_internal::valid_routing_id (dest_node_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!zlink::spot_actor_internal::valid_multipart_payload (parts_, part_count_))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if ((flags_ & ~ZLINK_DONTWAIT) != 0) {
        errno = ENOTSUP;
        return ZLINK_SUBMIT_NOT_SUPPORTED;
    }
    if (!as_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (zlink::spot_actor_lifecycle::reenters_same_actor (actor_)) {
        errno = EDEADLK;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    queued_join_request_t *request = NULL;
    spot_handle_t *spot = NULL;
    zlink_request_result_t immediate_result = ZLINK_REQUEST_OK;
    bool external_gateway_join = false;
    zlink::spot_node_t *external_source_node = NULL;
    zlink_routing_id_t external_source_node_rid;
    zlink_routing_id_t external_source_spot_rid;
    memset (&external_source_node_rid, 0, sizeof (external_source_node_rid));
    memset (&external_source_spot_rid, 0, sizeof (external_source_spot_rid));
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        zlink::spot_node_t *source_node =
          actor_runtime ().nodes.resolve_node_by_rid (actor_->node_rid);
        if (!source_node) {
            errno = ENOTCONN;
            return ZLINK_SUBMIT_NOT_CONNECTED;
        }

        actor_handle_t *actor = NULL;
        std::map<std::string, actor_handle_t *> &actors =
          zlink::spot_node_access_t::actors_by_id (source_node);
        std::map<std::string, actor_handle_t *>::iterator actor_it = actors.find (actor_->actor_id);
        if (actor_it != actors.end ())
            actor = actor_it->second;
        if (!actor) {
            immediate_result = ZLINK_REQUEST_NOT_FOUND;
        } else if (actor_->generation != 0 && actor->generation != actor_->generation) {
            immediate_result = ZLINK_REQUEST_CONFLICT;
        } else if (actor_runtime ().joins.actor_has_pending (actor)) {
            errno = EBUSY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (immediate_result == ZLINK_REQUEST_OK) {
            zlink::spot_node_t *target_node =
              actor_runtime ().nodes.resolve_node_by_rid (*dest_node_rid_);
            if (!target_node) {
                request = new (std::nothrow) queued_join_request_t ();
                if (!request) {
                    errno = ENOMEM;
                    return ZLINK_SUBMIT_INTERNAL_ERROR;
                }
                request->actor = actor;
                request->target_node = NULL;
                request->remote = true;
                request->target_node_rid = *dest_node_rid_;
                request->source_node_rid = actor->node_rid;
                request->source_spot_rid = join_actor_current_spot_rid_locked (actor);
                request->source_actor_ref = actor->ref_cache;
                request->target_actor_ref.node_rid = *dest_node_rid_;
                strncpy (request->target_actor_ref.actor_id, actor->actor_id.c_str (),
                         ZLINK_ACTOR_ID_MAX - 1);
                request->entry_handler = handler_;
                request->userdata = userdata_;
                request->entry_spot_join = true;
                request->join_epoch = next_join_commit_epoch_locked ();
                index_join_request_locked (request);
                external_gateway_join = true;
                external_source_node = source_node;
                external_source_node_rid = actor->node_rid;
                external_source_spot_rid = request->source_spot_rid;
            } else if (!zlink::spot_actor_internal::same_routing_id (actor->node_rid,
                                                                     *dest_node_rid_)
                       && actor_runtime ().routes.is_disconnected (actor->node,
                                                                   *dest_node_rid_)) {
                immediate_result = ZLINK_REQUEST_NOT_CONNECTED;
            } else {
                std::shared_ptr<spot_logical_state_t> target_state =
                  zlink::spot_node_access_t::entry_spot_state (target_node);
                spot = find_join_spot_facade_locked (target_node, target_state);
                if (!spot) {
                    immediate_result = ZLINK_REQUEST_NOT_FOUND;
                } else {
                    request = new (std::nothrow) queued_join_request_t ();
                    if (!request) {
                        errno = ENOMEM;
                        return ZLINK_SUBMIT_INTERNAL_ERROR;
                    }
                    request->actor = actor;
                    request->spot = spot;
                    request->spot_state = target_state;
                    request->target_node = target_node;
                    request->remote =
                      !zlink::spot_actor_internal::same_routing_id (actor->node_rid,
                                                                    *dest_node_rid_);
                    request->target_node_rid = *dest_node_rid_;
                    request->source_spot_rid = join_actor_current_spot_rid_locked (actor);
                    request->entry_handler = handler_;
                    request->userdata = userdata_;
                    request->entry_spot_join = true;
                    request->join_epoch = next_join_commit_epoch_locked ();
                    if (request->remote) {
                        std::map<std::string, actor_handle_t *> &target_actors =
                          zlink::spot_node_access_t::actors_by_id (target_node);
                        if (target_actors.count (actor->actor_id) != 0
                            || join_node_has_pending_actor_locked (target_node,
                                                                   actor->actor_id.c_str ())) {
                            delete request;
                            request = NULL;
                            immediate_result = ZLINK_REQUEST_CONFLICT;
                        } else {
                            request->target_actor_ref.node_rid = *dest_node_rid_;
                            strncpy (request->target_actor_ref.actor_id, actor->actor_id.c_str (),
                                     ZLINK_ACTOR_ID_MAX - 1);
                            request->target_actor_ref.generation =
                              next_join_generation_for_node_locked (target_node);
                            request->pending_target = create_join_actor_locked_with_generation (
                              target_node, *dest_node_rid_, actor->actor_id.c_str (),
                              request->target_actor_ref.generation, true);
                            if (!request->pending_target) {
                                delete request;
                                return errno == EBUSY
                                         ? ZLINK_SUBMIT_INVALID_STATE
                                         : zlink::spot_actor_internal::errno_to_submit_result (
                                             errno);
                            }
                        }
                    } else {
                        request->target_actor_ref = actor->ref_cache;
                    }
                    if (request) {
                        const zlink_submit_result_t adopt_rc =
                          zlink::spot_actor_internal::adopt_multipart_payload (
                            &request->message_parts, parts_, part_count_);
                        if (adopt_rc != ZLINK_SUBMIT_OK) {
                            if (request->pending_target)
                                remove_join_pending_target_locked (request);
                            delete request;
                            return adopt_rc;
                        }
                        index_join_request_locked (request);
                        actor_runtime ().joins.enqueue (request);
                    }
                }
            }
        }
    }

    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_entry_join_result (parts_, part_count_, handler_, userdata_,
                                                     dest_node_rid_, immediate_result);

    if (external_gateway_join) {
        const zlink_submit_result_t send_rc =
          zlink::spot_actor_internal::send_actor_gateway_multipart_from_source (
            external_source_node, external_source_node_rid, *dest_node_rid_,
            zlink::spot_actor_gateway::packet_entry_join_request, external_source_spot_rid,
            actor_->actor_id, actor_->generation, request->join_epoch, 0, parts_, part_count_,
            flags_);
        if (send_rc != ZLINK_SUBMIT_OK) {
            const zlink_request_result_t failure =
              zlink::spot_actor_internal::errno_to_request_result (errno);
            {
                std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
                retire_join_request_locked (request);
            }
            complete_and_release_join_request (request, failure);
            return ZLINK_SUBMIT_OK;
        }
        schedule_join_timeout (request, timeout_ms_);
        return ZLINK_SUBMIT_OK;
    }

    schedule_join_timeout (request, timeout_ms_);
    zlink_spot_notify_dispatch_info (spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE,
                                     ZLINK_SPOT_DISPATCH_SUBJECT_SPOT, spot);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_recv_result_t zlink_spot_actor_join_recv (void *spot_,
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
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    queued_join_request_t *request = NULL;
    if (!actor_runtime ().joins.peek_for_spot (spot, &request)) {
        errno = EAGAIN;
        return ZLINK_RECV_NO_DATA;
    }
    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    for (zlink::spot_owned_msg_parts_t::iterator it = request->message_parts.begin ();
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
    if (request->actor)
        info_out_->source_actor = request->actor->ref_cache;
    else
        info_out_->source_actor = request->source_actor_ref;
    info_out_->target_actor = request->target_actor_ref;
    info_out_->source_node_rid =
      request->actor ? request->actor->node_rid : request->source_node_rid;
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

extern "C" zlink_submit_result_t zlink_spot_actor_join_reply (void *spot_,
                                                              const zlink_actor_join_info_t *info_,
                                                              int32_t join_result_code_,
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
    if (!zlink::spot_actor_internal::valid_multipart_payload (parts_, part_count_))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    queued_join_request_t *request = static_cast<queued_join_request_t *> (info_->request);
    actor_handle_t *readable_actor = NULL;
    zlink_request_result_t completion_result = ZLINK_REQUEST_OK;
    bool send_external_reply = false;
    zlink::spot_node_t *external_reply_node = NULL;
    zlink_routing_id_t external_reply_source_node_rid;
    zlink_routing_id_t external_reply_target_node_rid;
    zlink_routing_id_t external_reply_target_spot_rid;
    char external_reply_actor_id[ZLINK_ACTOR_ID_MAX];
    uint64_t external_reply_actor_generation = 0;
    uint64_t external_reply_join_epoch = 0;
    int32_t external_reply_join_result_code = 0;
    std::vector<zlink_msg_t> external_reply_parts;
    memset (&external_reply_source_node_rid, 0, sizeof (external_reply_source_node_rid));
    memset (&external_reply_target_node_rid, 0, sizeof (external_reply_target_node_rid));
    memset (&external_reply_target_spot_rid, 0, sizeof (external_reply_target_spot_rid));
    memset (external_reply_actor_id, 0, sizeof (external_reply_actor_id));
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        if (!join_request_live_locked (request) || request->replied || !request->spot
            || !same_join_spot (request->spot, spot)) {
            errno = EALREADY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        if (info_->join_epoch != request->join_epoch
            || info_->flags != (request->remote ? ZLINK_ACTOR_JOIN_INFO_REMOTE : 0u)) {
            errno = ESTALE;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
        const zlink_submit_result_t adopt_rc =
          zlink::spot_actor_internal::adopt_multipart_payload (&request->reply_parts, parts_,
                                                               part_count_);
        if (adopt_rc != ZLINK_SUBMIT_OK)
            return adopt_rc;
        request->replied = true;
        request->join_result_code = join_result_code_;
        if (join_result_code_ == 0)
            completion_result = commit_accepted_join_locked (request, &readable_actor);
        if (request->remote && !request->actor
            && zlink::spot_actor_internal::valid_routing_id (&request->source_node_rid)) {
            send_external_reply = true;
            external_reply_node = request->target_node;
            external_reply_target_node_rid = request->source_node_rid;
            external_reply_target_spot_rid =
              request->spot_state ? request->spot_state->routing_id : request->source_spot_rid;
            external_reply_actor_generation = request->target_actor_ref.generation;
            external_reply_join_epoch = request->join_epoch;
            external_reply_join_result_code = join_result_code_;
            strncpy (external_reply_actor_id, request->target_actor_ref.actor_id,
                     ZLINK_ACTOR_ID_MAX - 1);
            if (external_reply_node)
                (void) external_reply_node->node_routing_id (&external_reply_source_node_rid);
            if (zlink::spot_move_msg_parts (&request->reply_parts, &external_reply_parts) != 0)
                completion_result = ZLINK_REQUEST_INTERNAL_ERROR;
        }
        retire_join_request_locked (request);
    }

    if (readable_actor)
        notify_join_actor_readable (readable_actor);
    if (send_external_reply && external_reply_node
        && zlink::spot_actor_internal::valid_routing_id (&external_reply_source_node_rid)) {
        zlink_msg_t *reply_data = external_reply_parts.empty () ? NULL : &external_reply_parts[0];
        const zlink_submit_result_t send_rc =
          zlink::spot_actor_internal::send_actor_gateway_multipart_from_source (
            external_reply_node, external_reply_source_node_rid, external_reply_target_node_rid,
            request->entry_spot_join ? zlink::spot_actor_gateway::packet_entry_join_reply
                                     : zlink::spot_actor_gateway::packet_spot_join_reply,
            external_reply_target_spot_rid, external_reply_actor_id,
            external_reply_actor_generation, external_reply_join_epoch,
            external_reply_join_result_code, reply_data, external_reply_parts.size (),
            ZLINK_DONTWAIT);
        if (send_rc != ZLINK_SUBMIT_OK && completion_result == ZLINK_REQUEST_OK)
            completion_result = zlink::spot_actor_internal::errno_to_request_result (errno);
    }
    complete_and_release_join_request (request, completion_result);
    return ZLINK_SUBMIT_OK;
}
