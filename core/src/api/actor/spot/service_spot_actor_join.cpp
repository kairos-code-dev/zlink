/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/actor/spot/service_spot_actor_join_internal.hpp"
#include "api/actor/spot/service_spot_actor_state_internal.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "runtime/services/actor/multipart/service_spot_actor_multipart_internal.hpp"
#include "runtime/services/actor/validation/service_spot_actor_validation_internal.hpp"
#include "runtime/services/spot/common/spot_message_parts_internal.hpp"
#include "runtime/services/spot/runtime/spot_handle.hpp"

#include <new>
#include <string.h>
#include <vector>

namespace
{

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

}

namespace zlink
{
namespace spot_actor_api_internal
{

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
