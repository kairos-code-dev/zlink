/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service/service_handle_internal.hpp"
#include "api/service/service_mode_internal.hpp"
#include "api/spot/dispatch/service_spot_dispatch_surface_internal.hpp"
#include "api/spot/dispatch/service_spot_dispatch_context_internal.hpp"
#include "services/actor/async/service_spot_actor_async_internal.hpp"
#include "services/actor/gateway/service_spot_actor_gateway_protocol_internal.hpp"
#include "services/actor/lifecycle/service_spot_actor_lifecycle_context_internal.hpp"
#include "services/actor/service_spot_actor_internal.hpp"
#include "services/actor/multipart/service_spot_actor_multipart_internal.hpp"
#include "services/actor/packet/service_spot_actor_packet_internal.hpp"
#include "services/actor/result/service_spot_actor_result_internal.hpp"
#include "services/actor/validation/service_spot_actor_validation_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_utils_internal.hpp"
#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "api/actor/spot/service_spot_actor_gateway_parts_internal.hpp"
#include "api/message/request_result_internal.hpp"
#include "api/message/recv_result_internal.hpp"
#include "api/core/config_result_internal.hpp"
#include "api/message/submit_result_internal.hpp"
#include "api/message/handler_result_internal.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_subject_access.hpp"
#include "core/recv_tls_view.hpp"
#include "utils/clock.hpp"
#include "utils/debug_log.hpp"
#include "utils/routing_id.hpp"
#include "api/actor/spot/service_spot_actor_join_internal.hpp"
#include "api/actor/spot/service_spot_actor_no_bind_internal.hpp"
#include "api/actor/spot/service_spot_actor_state_internal.hpp"
#include "protocol/wire.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace zlink::spot_actor_api_internal;

namespace zlink
{
namespace spot_actor_api_internal
{

actor_runtime_t &actor_runtime ()
{
    static actor_runtime_t runtime;
    return runtime;
}

}
}

namespace
{

const size_t stack_spot_actor_routed_part_capacity = 8;

const bool actor_gateway_debug_on = zlink::debug_env_enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE");

uint64_t now_ms ();
uint64_t next_generation_for_node_locked (zlink::spot_node_t *node_);
void update_active_route_locked (actor_handle_t *actor_);
uint64_t next_commit_epoch_locked ();
using zlink::routing_id_key;
using zlink::spot_actor_internal::actor_missing_request_result_from_errno;
using zlink::spot_actor_internal::actor_missing_submit_result_from_errno;
using zlink::spot_actor_internal::adopt_multipart_payload;
using zlink::spot_actor_internal::consume_multipart_payload;
using zlink::spot_actor_internal::copy_msg_for_stream_send;
using zlink::spot_actor_internal::errno_to_request_result;
using zlink::spot_actor_internal::errno_to_submit_result;
using zlink::spot_actor_internal::request_result_to_submit_result;
using zlink::spot_actor_internal::same_actor_ref_identity;
using zlink::spot_actor_internal::same_routing_id;
using zlink::spot_actor_internal::send_actor_gateway_multipart_from_source;
using zlink::spot_actor_internal::send_copied_msg_to_bound_stream;
using zlink::spot_actor_internal::valid_actor_id;
using zlink::spot_actor_internal::valid_multipart_payload;
using zlink::spot_actor_internal::valid_routing_id;

}

namespace
{

spot_handle_t *
find_spot_facade_for_state_locked (zlink::spot_node_t *node_,
                                   const std::shared_ptr<spot_logical_state_t> &state_)
{
    return actor_runtime ().nodes.find_spot_for_state (node_, state_);
}

bool actor_in_entry_spot_locked (const actor_handle_t *actor_)
{
    return actor_ && actor_->joined_spot_state && actor_->joined_spot_state->entry;
}

bool actor_in_user_spot_locked (const actor_handle_t *actor_)
{
    return actor_ && actor_->joined_spot_state && !actor_->joined_spot_state->entry;
}

zlink_spot_kind_t spot_kind_for_state (const std::shared_ptr<spot_logical_state_t> &state_)
{
    if (!state_)
        return ZLINK_SPOT_KIND_INVALID;
    return state_->entry ? ZLINK_SPOT_KIND_ENTRY : ZLINK_SPOT_KIND_USER;
}

bool actor_route_is_current_location (const zlink_actor_route_t &route_, const char *actor_id_)
{
    if (!actor_id_ || route_.actor.node_rid.size == 0 || route_.current_spot_rid.size == 0
        || (route_.current_spot_kind != ZLINK_SPOT_KIND_ENTRY
            && route_.current_spot_kind != ZLINK_SPOT_KIND_USER)) {
        return false;
    }
    return strncmp (route_.actor.actor_id, actor_id_, ZLINK_ACTOR_ID_MAX) == 0;
}

bool actor_has_pending_join_locked (const actor_handle_t *actor_)
{
    return actor_runtime ().joins.actor_has_pending (actor_);
}

bool node_has_pending_join_actor_locked (zlink::spot_node_t *node_, const char *actor_id_)
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

void clear_actor_bound_session_locked (actor_handle_t *actor_, bool update_changed_time_);

bool spot_has_pending_join_locked (spot_logical_state_t *key_)
{
    return actor_runtime ().joins.spot_has_pending (key_);
}

void enqueue_join_request_locked (queued_join_request_t *request_)
{
    if (!request_)
        return;
    actor_runtime ().joins.enqueue (request_);
}

bool peek_join_request_for_spot_locked (spot_handle_t *spot_, queued_join_request_t **request_out_)
{
    return actor_runtime ().joins.peek_for_spot (spot_, request_out_);
}

void take_join_queue_locked (spot_logical_state_t *key_,
                             std::deque<queued_join_request_t *> *pending_)
{
    actor_runtime ().joins.take_queue (key_, pending_);
}

void replace_live_join_spot_locked (spot_handle_t *from_, spot_handle_t *to_)
{
    actor_runtime ().joins.replace_live_spot (from_, to_);
}

void collect_live_join_requests_for_state_locked (
  const std::shared_ptr<spot_logical_state_t> &state_,
  std::deque<queued_join_request_t *> *pending_)
{
    actor_runtime ().joins.collect_live_for_state (state_, pending_);
}

void drain_queued_join_requests_for_stream_locked (void *stream_,
                                                   std::deque<queued_join_request_t *> *aborted_)
{
    if (!aborted_)
        return;
    const size_t old_size = aborted_->size ();
    actor_runtime ().joins.drain_queued_for_stream (stream_, aborted_);
    for (std::deque<queued_join_request_t *>::iterator it = aborted_->begin () + old_size;
         it != aborted_->end (); ++it) {
        clear_actor_bound_session_locked ((*it)->actor, true);
        retire_join_request_locked (*it);
    }
}

void collect_received_join_requests_for_stream_locked (
  void *stream_,
  const std::deque<queued_join_request_t *> &already_aborted_,
  std::vector<queued_join_request_t *> *received_aborts_)
{
    actor_runtime ().joins.collect_live_for_stream (stream_, already_aborted_, received_aborts_);
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
    actor_->joined_spot_state =
      spot_ ? spot_->logical_state : std::shared_ptr<spot_logical_state_t> ();
    actor_->last_changed_ms = now_ms ();
    update_active_route_locked (actor_);
}

void set_actor_entry_spot_locked (actor_handle_t *actor_)
{
    if (!actor_)
        return;
    actor_->joined_spot_state = zlink::spot_node_access_t::entry_spot_state (actor_->node);
    actor_->last_changed_ms = now_ms ();
    update_active_route_locked (actor_);
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

actor_handle_t *create_actor_locked_with_generation (zlink::spot_node_t *node_,
                                                     const zlink_routing_id_t &node_rid_,
                                                     const char *actor_id_,
                                                     uint64_t generation_,
                                                     bool pending_remote_join_ = false)
{
    std::map<std::string, actor_handle_t *> &actors =
      zlink::spot_node_access_t::actors_by_id (node_);
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
    actor->generation = generation_ == 0 ? next_generation_for_node_locked (node_) : generation_;
    actor->pending_remote_join = pending_remote_join_;
    actor->last_changed_ms = zlink::clock_t ().now_ms ();
    fill_ref (actor.get (), &actor->ref_cache);

    actor_handle_t *raw = actor.release ();
    actors[raw->actor_id] = raw;
    zlink::spot_node_access_t::actor_handles (node_).insert (raw);
    actor_runtime ().nodes.register_node (node_, node_rid_);
    if (!pending_remote_join_)
        set_actor_entry_spot_locked (raw);
    return raw;
}

actor_handle_t *create_actor_locked (zlink::spot_node_t *node_,
                                     const zlink_routing_id_t &node_rid_,
                                     const char *actor_id_)
{
    return create_actor_locked_with_generation (node_, node_rid_, actor_id_, 0);
}

actor_handle_t *as_actor_locked (void *actor_)
{
    actor_handle_t *actor = static_cast<actor_handle_t *> (actor_);
    if (!actor || !actor->check_tag () || !actor_runtime ().nodes.known_node (actor->node)
        || zlink::spot_node_access_t::actor_handles (actor->node).count (actor) == 0)
        return NULL;
    return actor;
}

bool actor_route_disconnected_locked (zlink::spot_node_t *source_node_,
                                      const zlink_routing_id_t &target_rid_)
{
    return actor_runtime ().routes.is_disconnected (source_node_, target_rid_);
}

actor_handle_t *resolve_actor_ref_locked (const zlink_actor_ref_t *ref_,
                                          bool include_pending_ = false)
{
    if (!ref_ || !valid_actor_id (ref_->actor_id) || !valid_routing_id (&ref_->node_rid))
        return NULL;

    zlink::spot_node_t *node = actor_runtime ().nodes.resolve_node_by_rid (ref_->node_rid);
    if (!node) {
        errno = ENOENT;
        return NULL;
    }

    std::map<std::string, actor_handle_t *> &actors =
      zlink::spot_node_access_t::actors_by_id (node);
    const std::map<std::string, actor_handle_t *>::iterator actor_it = actors.find (ref_->actor_id);
    if (actor_it == actors.end () || (!include_pending_ && actor_it->second->pending_remote_join)) {
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

actor_handle_t *resolve_logical_actor_ref_locked (const zlink_actor_ref_t *ref_,
                                                  bool include_pending_ = false)
{
    if (!ref_ || !valid_actor_id (ref_->actor_id))
        return NULL;

    zlink_actor_route_t route;
    if (actor_runtime ().routes.find_active (ref_->actor_id, &route)
        && actor_route_is_current_location (route, ref_->actor_id)) {
        if (ref_->generation != 0 && route.actor.generation != ref_->generation) {
            errno = ESTALE;
            return NULL;
        }
        return resolve_actor_ref_locked (&route.actor, include_pending_);
    }

    actor_handle_t *match =
      actor_runtime ().nodes.find_unique_actor_by_id (ref_->actor_id, include_pending_);
    if (!match) {
        return NULL;
    }
    if (ref_->generation != 0 && match->generation != ref_->generation) {
        errno = ESTALE;
        return NULL;
    }
    return match;
}

struct actor_resolution_t
{
    actor_resolution_t () : result (ZLINK_REQUEST_INTERNAL_ERROR), actor (NULL), owner (NULL) {}

    zlink_request_result_t result;
    actor_handle_t *actor;
    zlink::spot_node_t *owner;
};

actor_resolution_t resolve_actor_for_request_locked (zlink::spot_node_t *request_node_,
                                                     const zlink_actor_ref_t *ref_,
                                                     bool include_pending_ = false)
{
    actor_resolution_t resolved;
    if (!actor_runtime ().nodes.known_node (request_node_)) {
        resolved.result = ZLINK_REQUEST_NOT_CONNECTED;
        return resolved;
    }

    if (ref_ && valid_routing_id (&ref_->node_rid)) {
        resolved.owner = actor_runtime ().nodes.resolve_node_by_rid (ref_->node_rid);
        resolved.actor = resolve_actor_ref_locked (ref_, include_pending_);
    } else {
        resolved.actor = resolve_logical_actor_ref_locked (ref_, include_pending_);
        resolved.owner = resolved.actor ? resolved.actor->node : NULL;
    }

    if (!resolved.actor) {
        resolved.result = actor_missing_request_result_from_errno ();
        return resolved;
    }

    if (!resolved.owner
        || actor_route_disconnected_locked (request_node_, resolved.actor->node_rid)) {
        errno = ENOTCONN;
        resolved.result = ZLINK_REQUEST_NOT_CONNECTED;
        return resolved;
    }

    resolved.result = ZLINK_REQUEST_OK;
    return resolved;
}

bool is_external_remote_actor_ref_locked (zlink::spot_node_t *request_node_,
                                          const zlink_actor_ref_t *ref_)
{
    if (!request_node_ || !ref_ || !valid_routing_id (&ref_->node_rid))
        return false;

    zlink_routing_id_t local_rid;
    memset (&local_rid, 0, sizeof (local_rid));
    if (request_node_->node_routing_id (&local_rid) == 0
        && same_routing_id (local_rid, ref_->node_rid))
        return false;

    return actor_runtime ().nodes.resolve_node_by_rid (ref_->node_rid) == NULL;
}

bool is_remote_actor_ref_for_node (zlink::spot_node_t *request_node_, const zlink_actor_ref_t *ref_)
{
    if (!request_node_ || !ref_ || !valid_routing_id (&ref_->node_rid) || ref_->generation == 0)
        return false;

    zlink_routing_id_t local_rid;
    memset (&local_rid, 0, sizeof (local_rid));
    if (request_node_->node_routing_id (&local_rid) != 0 || !valid_routing_id (&local_rid))
        return false;

    return !same_routing_id (local_rid, ref_->node_rid);
}

zlink_submit_result_t
send_actor_gateway_packet_from_source (zlink::spot_node_t *origin_node_,
                                       const zlink_routing_id_t &source_node_rid_,
                                       const zlink_routing_id_t &target_node_rid_,
                                       uint8_t kind_,
                                       const zlink_routing_id_t &session_rid_,
                                       const char *actor_id_,
                                       uint64_t generation_,
                                       zlink_msg_t *payload_,
                                       zlink_send_flags_t flags_,
                                       zlink_part_flag_t part_flag_);

zlink_submit_result_t send_actor_gateway_packet (zlink::spot_node_t *origin_node_,
                                                 const zlink_routing_id_t &target_node_rid_,
                                                 uint8_t kind_,
                                                 const zlink_routing_id_t &session_rid_,
                                                 const char *actor_id_,
                                                 uint64_t generation_,
                                                 zlink_msg_t *payload_,
                                                 zlink_send_flags_t flags_,
                                                 zlink_part_flag_t part_flag_)
{
    if (!origin_node_ || !valid_routing_id (&target_node_rid_) || !payload_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    zlink_routing_id_t source_node_rid;
    memset (&source_node_rid, 0, sizeof (source_node_rid));
    if (origin_node_->node_routing_id (&source_node_rid) != 0)
        return errno_to_submit_result (errno);

    return send_actor_gateway_packet_from_source (origin_node_, source_node_rid, target_node_rid_,
                                                  kind_, session_rid_, actor_id_, generation_,
                                                  payload_, flags_, part_flag_);
}

zlink_submit_result_t
send_actor_gateway_packet_from_source (zlink::spot_node_t *origin_node_,
                                       const zlink_routing_id_t &source_node_rid_,
                                       const zlink_routing_id_t &target_node_rid_,
                                       uint8_t kind_,
                                       const zlink_routing_id_t &session_rid_,
                                       const char *actor_id_,
                                       uint64_t generation_,
                                       zlink_msg_t *payload_,
                                       zlink_send_flags_t flags_,
                                       zlink_part_flag_t part_flag_)
{
    if (!origin_node_ || !valid_routing_id (&source_node_rid_)
        || !valid_routing_id (&target_node_rid_) || !payload_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    zlink_msg_t control;
    if (!zlink::spot_actor_gateway::init_control_msg (kind_, session_rid_, actor_id_, generation_,
                                                      part_flag_, &control))
        return errno_to_submit_result (errno);

    zlink_msg_t payload_copy;
    const zlink_submit_result_t copy_rc = copy_msg_for_stream_send (payload_, &payload_copy);
    if (copy_rc != ZLINK_SUBMIT_OK) {
        (void) zlink_msg_close (&control);
        return copy_rc;
    }

    zlink_msg_t packet_parts[2] = {control, payload_copy};
    const std::string source_node = routing_id_key (source_node_rid_);
    const std::string target_node = routing_id_key (target_node_rid_);
    const size_t combined_count = zlink::spot_reqrep_internal::spot_routed_message_part_count (2);
    zlink_msg_t stack_combined[stack_spot_actor_routed_part_capacity];
    std::vector<zlink_msg_t> heap_combined;
    zlink_msg_t *combined =
      combined_count <= stack_spot_actor_routed_part_capacity ? stack_combined : NULL;
    if (!combined) {
        heap_combined.resize (combined_count);
        combined = &heap_combined[0];
    }
    if (zlink::spot_reqrep_internal::build_spot_routed_message_into (
          zlink::spot_routed_protocol::actor_gateway_endpoint_class, source_node,
          zlink::spot_actor_gateway::endpoint_name,
          zlink::spot_routed_protocol::actor_gateway_endpoint_class, target_node,
          zlink::spot_actor_gateway::endpoint_name, packet_parts, 2, combined, combined_count)
        != 0) {
        const int saved_errno = errno;
        errno = saved_errno;
        return errno_to_submit_result (errno);
    }

    const int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery_direct (
      origin_node_, flags_, combined, combined_count);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (combined, combined_count);
    errno = saved_errno;
    if (rc != 0)
        return zlink::submit_result_internal::from_errno (errno);

    (void) zlink_msg_close (payload_);
    (void) zlink_msg_init (payload_);
    return ZLINK_SUBMIT_OK;
}

}

namespace zlink
{
namespace spot_actor_internal
{

zlink_submit_result_t
send_actor_gateway_multipart_from_source (zlink::spot_node_t *origin_node_,
                                          const zlink_routing_id_t &source_node_rid_,
                                          const zlink_routing_id_t &target_node_rid_,
                                          uint8_t kind_,
                                          const zlink_routing_id_t &session_rid_,
                                          const char *actor_id_,
                                          uint64_t generation_,
                                          uint64_t request_id_,
                                          int32_t join_result_code_,
                                          zlink_msg_t *parts_,
                                          size_t part_count_,
                                          zlink_send_flags_t flags_)
{
    if (!origin_node_ || !valid_routing_id (&source_node_rid_)
        || !valid_routing_id (&target_node_rid_)
        || !valid_multipart_payload (parts_, part_count_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    zlink_msg_t control;
    if (!zlink::spot_actor_gateway::init_control_msg (kind_, session_rid_, actor_id_, generation_,
                                                      ZLINK_PART_FINAL, &control, request_id_,
                                                      join_result_code_))
        return errno_to_submit_result (errno);

    std::vector<zlink_msg_t> gateway_parts;
    if (zlink::spot_actor_internal::build_gateway_parts (&control, parts_, part_count_, true,
                                                         &gateway_parts)
        != 0)
        return errno_to_submit_result (errno);

    const std::string source_node = routing_id_key (source_node_rid_);
    const std::string target_node = routing_id_key (target_node_rid_);
    const size_t combined_count =
      zlink::spot_reqrep_internal::spot_routed_message_part_count (gateway_parts.size ());
    zlink_msg_t stack_combined[stack_spot_actor_routed_part_capacity];
    std::vector<zlink_msg_t> heap_combined;
    zlink_msg_t *combined =
      combined_count <= stack_spot_actor_routed_part_capacity ? stack_combined : NULL;
    if (!combined) {
        heap_combined.resize (combined_count);
        combined = &heap_combined[0];
    }
    if (zlink::spot_reqrep_internal::build_spot_routed_message_into (
          zlink::spot_routed_protocol::actor_gateway_endpoint_class, source_node,
          zlink::spot_actor_gateway::endpoint_name,
          zlink::spot_routed_protocol::actor_gateway_endpoint_class, target_node,
          zlink::spot_actor_gateway::endpoint_name, gateway_parts.data (), gateway_parts.size (),
          combined, combined_count)
        != 0) {
        return errno_to_submit_result (errno);
    }

    const int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery_direct (
      origin_node_, flags_, combined, combined_count);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (combined, combined_count);
    errno = saved_errno;
    return rc == 0 ? ZLINK_SUBMIT_OK : zlink::submit_result_internal::from_errno (errno);
}

}
}

namespace
{

zlink::spot_node_t *stream_owner_for_actor_ref_locked (void *stream_,
                                                       const zlink_actor_ref_t *actor_ref_)
{
    if (!actor_ref_)
        return actor_runtime ().sessions.stream_owner (stream_, actor_runtime ().nodes);
    return actor_runtime ().sessions.stream_owner_for_actor_ref (stream_, *actor_ref_,
                                                                 actor_runtime ().nodes);
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
    uint64_t epoch = actor_runtime ().next_join_epoch++;
    if (actor_runtime ().next_join_epoch == 0)
        actor_runtime ().next_join_epoch = 1;
    return epoch == 0 ? actor_runtime ().next_join_epoch++ : epoch;
}

void publish_active_route_locked (actor_handle_t *actor_, bool create_)
{
    actor_runtime ().routes.publish_active (actor_, create_);
}

void create_active_route_locked (actor_handle_t *actor_)
{
    publish_active_route_locked (actor_, true);
}

void update_active_route_locked (actor_handle_t *actor_)
{
    publish_active_route_locked (actor_, false);
}

void clear_actor_bound_session_locked (actor_handle_t *actor_, bool update_changed_time_)
{
    if (!actor_)
        return;
    actor_->bound_session_node = NULL;
    memset (&actor_->bound_session_node_rid, 0, sizeof (actor_->bound_session_node_rid));
    actor_->bound_stream = NULL;
    memset (&actor_->bound_session_rid, 0, sizeof (actor_->bound_session_rid));
    if (update_changed_time_)
        actor_->last_changed_ms = now_ms ();
}

std::unique_ptr<actor_handle_t> remove_actor_locked (actor_handle_t *actor_,
                                                     bool erase_session_binding_ = true)
{
    if (!actor_)
        return std::unique_ptr<actor_handle_t> ();

    if (actor_->bound_stream) {
        actor_runtime ().sessions.detach_actor (actor_, erase_session_binding_, true);
        clear_actor_bound_session_locked (actor_, false);
    }

    if (actor_runtime ().nodes.known_node (actor_->node))
        zlink::spot_node_access_t::actors_by_id (actor_->node).erase (actor_->actor_id);
    actor_runtime ().routes.remove_matching_active (actor_);
    if (actor_runtime ().nodes.known_node (actor_->node))
        zlink::spot_node_access_t::actor_handles (actor_->node).erase (actor_);
    actor_->tag = 0;
    return std::unique_ptr<actor_handle_t> (actor_);
}

void notify_actor_readable (actor_handle_t *actor_)
{
    spot_handle_t *dispatch_spot =
      actor_ && actor_->joined_spot_state
        ? find_spot_facade_for_state_locked (actor_->node, actor_->joined_spot_state)
        : NULL;
    if (dispatch_spot)
        zlink_spot_notify_dispatch_info (dispatch_spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE,
                                         ZLINK_SPOT_DISPATCH_SUBJECT_ACTOR, &actor_->ref_cache);
}

void schedule_lifecycle_event_locked (const std::shared_ptr<spot_logical_state_t> &spot_state_,
                                      zlink_spot_actor_lifecycle_event_kind_t kind_,
                                      const zlink_spot_actor_lifecycle_info_t &info_,
                                      zlink::spot_owned_msg_parts_t *request_parts_);

}

namespace zlink
{
namespace spot_actor_api_internal
{

void remove_join_pending_target_locked (queued_join_request_t *request_)
{
    if (!request_ || !request_->pending_target || !request_->pending_target->pending_remote_join)
        return;
    std::unique_ptr<actor_handle_t> pending =
      remove_actor_locked (request_->pending_target, false);
    LIBZLINK_UNUSED (pending);
    request_->pending_target = NULL;
}

void remove_join_actor_locked (actor_handle_t *actor_, bool erase_session_binding_)
{
    std::unique_ptr<actor_handle_t> retired =
      remove_actor_locked (actor_, erase_session_binding_);
    LIBZLINK_UNUSED (retired);
}

void notify_join_actor_readable (actor_handle_t *actor_)
{
    notify_actor_readable (actor_);
}

actor_handle_t *create_join_actor_locked_with_generation (zlink::spot_node_t *node_,
                                                          const zlink_routing_id_t &node_rid_,
                                                          const char *actor_id_,
                                                          uint64_t generation_,
                                                          bool pending_remote_join_)
{
    return create_actor_locked_with_generation (node_, node_rid_, actor_id_, generation_,
                                                pending_remote_join_);
}

void schedule_join_lifecycle_event_locked (
  const std::shared_ptr<spot_logical_state_t> &spot_state_,
  zlink_spot_actor_lifecycle_event_kind_t kind_,
  const zlink_spot_actor_lifecycle_info_t &info_)
{
    schedule_lifecycle_event_locked (spot_state_, kind_, info_, NULL);
}

}
}

namespace
{

void schedule_lifecycle_event_locked (const std::shared_ptr<spot_logical_state_t> &spot_state_,
                                      zlink_spot_actor_lifecycle_event_kind_t kind_,
                                      const zlink_spot_actor_lifecycle_info_t &info_,
                                      zlink::spot_owned_msg_parts_t *request_parts_ = NULL)
{
    if (!spot_state_)
        return;
    spot_handle_t *spot = find_spot_facade_for_state_locked (spot_state_->node, spot_state_);
    if (!spot)
        return;

    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> state =
      zlink::spot_reqrep_internal::try_find_spot_state (spot);
    if (!state)
        return;
    {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        if (!state->dispatch.handler)
            return;
    }

    lifecycle_event_t event;
    event.kind = kind_;
    event.info = info_;
    if (request_parts_)
        event.request_parts.swap (*request_parts_);
    actor_runtime ().lifecycle.enqueue (spot_state_.get (), std::move (event));
    zlink::spot_reqrep_internal::maybe_dispatch_spot_info (
      state.get (), ZLINK_SPOT_DISPATCH_EVENT_ACTOR_LIFECYCLE_READABLE,
      ZLINK_SPOT_DISPATCH_SUBJECT_SPOT, NULL);
}

zlink_spot_actor_lifecycle_info_t make_lifecycle_info (const zlink_actor_ref_t &previous_actor_,
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

zlink_request_result_t bind_actor_to_session_locked (zlink::spot_node_t *stream_owner_,
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

    actor_handle_t *previous_actor = NULL;
    actor_runtime ().sessions.bind_actor (stream_owner_, stream_, session_rid_, actor_, now_ms (),
                                          &previous_actor);
    if (previous_actor)
        clear_actor_bound_session_locked (previous_actor, true);
    return ZLINK_REQUEST_OK;
}

zlink_request_result_t unbind_actor_from_session_locked (zlink::spot_node_t *stream_owner_,
                                                         void *stream_,
                                                         const zlink_routing_id_t &session_rid_,
                                                         const char *actor_id_)
{
    if (!stream_ || !actor_id_) {
        errno = EFSM;
        return ZLINK_REQUEST_INVALID_STATE;
    }

    actor_session_state_t::binding_map_t::iterator binding_it =
      actor_runtime ().sessions.find_binding (stream_, &session_rid_);
    if (binding_it == actor_runtime ().sessions.bindings_end ())
        return ZLINK_REQUEST_OK;
    session_binding_t &binding = binding_it->second;
    std::map<std::string, session_binding_t::actor_entry_t>::iterator it =
      binding.actors.find (actor_id_);
    if (it == binding.actors.end ())
        return ZLINK_REQUEST_OK;

    if (stream_owner_ && valid_routing_id (&it->second.ref.node_rid)
        && actor_route_disconnected_locked (stream_owner_, it->second.ref.node_rid)) {
        errno = ENOTCONN;
        return ZLINK_REQUEST_NOT_CONNECTED;
    }
    if (it->second.actor)
        clear_actor_bound_session_locked (it->second.actor, true);
    binding.actors.erase (it);
    if (binding.actors.empty ()) {
        actor_runtime ().sessions.erase_binding (binding_it);
        actor_runtime ().sessions.erase_stream_owner_if_unused (stream_);
    }
    return ZLINK_REQUEST_OK;
}

struct actor_lookup_operation_arg_t
{
    zlink::spot_node_t *request_node;
    zlink_routing_id_t target_node_rid;
    char actor_id[ZLINK_ACTOR_ID_MAX];
};

actor_lookup_operation_arg_t *new_actor_lookup_operation_arg ()
{
    actor_lookup_operation_arg_t *arg = new (std::nothrow) actor_lookup_operation_arg_t;
    if (!arg) {
        errno = ENOMEM;
        return NULL;
    }
    memset (arg, 0, sizeof (*arg));
    return arg;
}

void cleanup_actor_lookup_operation_arg (void *arg_)
{
    delete static_cast<actor_lookup_operation_arg_t *> (arg_);
}

void run_actor_lookup_operation (void *arg_, zlink_actor_lookup_result_t *out_)
{
    actor_lookup_operation_arg_t *arg = static_cast<actor_lookup_operation_arg_t *> (arg_);
    memset (out_, 0, sizeof (*out_));
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
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
    actor_reply_operation_arg_t () : run (NULL), request_node (NULL), stream (NULL)
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

zlink_request_result_t run_actor_reply_operation (void *arg_);

actor_reply_operation_arg_t *
new_actor_reply_operation_arg (zlink_request_result_t (*run_) (actor_reply_operation_arg_t *))
{
    actor_reply_operation_arg_t *arg = new (std::nothrow) actor_reply_operation_arg_t;
    if (!arg) {
        errno = ENOMEM;
        return NULL;
    }
    arg->run = run_;
    return arg;
}

zlink_submit_result_t schedule_actor_reply_operation (zlink_reply_handler_fn handler_,
                                                      void *userdata_,
                                                      uint32_t timeout_ms_,
                                                      actor_reply_operation_arg_t *arg_)
{
    return zlink::spot_actor_async::schedule_reply_operation (handler_, userdata_, timeout_ms_,
                                                              run_actor_reply_operation, arg_,
                                                              cleanup_actor_reply_operation_arg);
}

zlink_request_result_t run_destroy_operation_locked (actor_reply_operation_arg_t *arg_)
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
          actor_runtime ().sessions.find_binding (actor->bound_stream, &actor->bound_session_rid);
        if (binding_it != actor_runtime ().sessions.bindings_end ()
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
    std::shared_ptr<spot_logical_state_t> lifecycle_spot = actor->joined_spot_state;
    const zlink_spot_actor_lifecycle_info_t lifecycle_info =
      make_lifecycle_info (previous_actor, zero_actor, actor_current_spot_rid_locked (actor),
                           zero_spot, actor->join_epoch);
    std::unique_ptr<actor_handle_t> actor_to_delete = remove_actor_locked (actor);
    LIBZLINK_UNUSED (actor_to_delete);
    schedule_lifecycle_event_locked (lifecycle_spot, ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT,
                                     lifecycle_info);
    return ZLINK_REQUEST_OK;
}

zlink_request_result_t run_leave_operation_locked (actor_reply_operation_arg_t *arg_)
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
        || !same_routing_id (actor_current_spot_rid_locked (actor), arg_->rid)) {
        errno = ESTALE;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    if (actor_in_entry_spot_locked (actor)) {
        if (!actor_runtime ().routes.active_matches (actor)
            && actor_runtime ().routes.active_exists (actor))
            create_active_route_locked (actor);
        return ZLINK_REQUEST_OK;
    }

    zlink_actor_ref_t actor_ref;
    fill_ref (actor, &actor_ref);
    const zlink_routing_id_t previous_spot = actor_current_spot_rid_locked (actor);
    const uint64_t previous_epoch = actor->join_epoch;
    const uint64_t epoch = next_commit_epoch_locked ();
    std::shared_ptr<spot_logical_state_t> source_spot = actor->joined_spot_state;
    const zlink_spot_actor_lifecycle_info_t leave_info = make_lifecycle_info (
      actor_ref, actor_ref, previous_spot,
      zlink::spot_node_access_t::entry_spot_state (actor->node)->routing_id, previous_epoch);
    actor->join_epoch = epoch;
    set_actor_entry_spot_locked (actor);
    const zlink_routing_id_t entry_spot = actor_current_spot_rid_locked (actor);
    const zlink_spot_actor_lifecycle_info_t join_info =
      make_lifecycle_info (actor_ref, actor_ref, previous_spot, entry_spot, epoch);
    schedule_lifecycle_event_locked (source_spot, ZLINK_SPOT_ACTOR_LIFECYCLE_LEFT, leave_info);
    schedule_lifecycle_event_locked (actor->joined_spot_state, ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED,
                                     join_info);
    return ZLINK_REQUEST_OK;
}

zlink_request_result_t run_bind_operation_locked (actor_reply_operation_arg_t *arg_)
{
    if (!arg_)
        return ZLINK_REQUEST_INVALID_STATE;
    zlink::spot_node_t *stream_owner =
      stream_owner_for_actor_ref_locked (arg_->stream, &arg_->actor);
    if (!stream_owner) {
        errno = ENOTCONN;
        return ZLINK_REQUEST_INVALID_STATE;
    }
    if (is_remote_actor_ref_for_node (stream_owner, &arg_->actor)) {
        actor_runtime ().sessions.bind_actor_ref (arg_->stream, arg_->rid, arg_->actor);
        return ZLINK_REQUEST_OK;
    }

    const actor_resolution_t resolved =
      resolve_actor_for_request_locked (stream_owner, &arg_->actor);
    if (resolved.result != ZLINK_REQUEST_OK) {
        if (is_external_remote_actor_ref_locked (stream_owner, &arg_->actor)) {
            actor_runtime ().sessions.bind_actor_ref (arg_->stream, arg_->rid, arg_->actor);
            return ZLINK_REQUEST_OK;
        }
        return resolved.result;
    }
    actor_handle_t *actor = resolved.actor;
    return bind_actor_to_session_locked (stream_owner, arg_->stream, arg_->rid, actor);
}

zlink_request_result_t run_unbind_operation_locked (actor_reply_operation_arg_t *arg_)
{
    if (!arg_)
        return ZLINK_REQUEST_INVALID_STATE;
    zlink::spot_node_t *stream_owner =
      actor_runtime ().sessions.stream_owner (arg_->stream, actor_runtime ().nodes);
    if (!stream_owner) {
        return unbind_actor_from_session_locked (NULL, arg_->stream, arg_->rid, arg_->actor_id);
    }
    return unbind_actor_from_session_locked (stream_owner, arg_->stream, arg_->rid, arg_->actor_id);
}

zlink_request_result_t run_actor_reply_operation (void *arg_)
{
    actor_reply_operation_arg_t *arg = static_cast<actor_reply_operation_arg_t *> (arg_);
    if (!arg)
        return ZLINK_REQUEST_INTERNAL_ERROR;
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    if (!arg->run)
        return ZLINK_REQUEST_INTERNAL_ERROR;
    return arg->run (arg);
}

void drain_lifecycle_events_for_spot (spot_handle_t *spot_)
{
    (void) spot_;
}

bool spot_dispatch_handler_attached (const spot_handle_t *spot_)
{
    if (!spot_ || !spot_->logical_state || !spot_->logical_state->request_reply_state)
        return false;
    std::lock_guard<std::mutex> lock (spot_->logical_state->request_reply_state->dispatch.mutex);
    return spot_->logical_state->request_reply_state->dispatch.handler != NULL;
}

zlink_submit_result_t validate_actor_bound_session_locked (actor_handle_t *actor_,
                                                           void **stream_out_,
                                                           zlink_routing_id_t *rid_out_)
{
    if (!actor_ || !stream_out_ || !rid_out_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!actor_->bound_stream) {
        errno = ENOENT;
        return ZLINK_SUBMIT_NOT_FOUND;
    }
    if (!actor_runtime ().nodes.known_node (actor_->bound_session_node)) {
        errno = ENOTCONN;
        return ZLINK_SUBMIT_NOT_CONNECTED;
    }

    actor_session_state_t::binding_map_t::iterator binding_it =
      actor_runtime ().sessions.find_binding (actor_->bound_stream, &actor_->bound_session_rid);
    if (binding_it == actor_runtime ().sessions.bindings_end ()) {
        clear_actor_bound_session_locked (actor_, false);
        errno = ENOENT;
        return ZLINK_SUBMIT_NOT_FOUND;
    }

    std::map<std::string, session_binding_t::actor_entry_t>::const_iterator actor_it =
      binding_it->second.actors.find (actor_->actor_id);
    if (actor_it == binding_it->second.actors.end () || actor_it->second.actor != actor_
        || actor_it->second.ref.generation != actor_->generation) {
        errno = ESTALE;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    *stream_out_ = actor_->bound_stream;
    *rid_out_ = actor_->bound_session_rid;
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t enqueue_bound_actor_part_locked (zlink::spot_node_t *stream_owner_,
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
    if (!stream_owner_ || !binding_ || !entry_ || !session_rid_ || !actor_id_ || !part_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (binding_->in_progress && binding_->in_progress_actor_id != actor_id_) {
        errno = EFSM;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    if (!entry_->actor && is_remote_actor_ref_for_node (stream_owner_, &entry_->ref)) {
        const zlink_submit_result_t send_rc = send_actor_gateway_packet (
          stream_owner_, entry_->ref.node_rid, zlink::spot_actor_gateway::packet_session_to_actor,
          *session_rid_, actor_id_, entry_->ref.generation, part_, ZLINK_DONTWAIT, part_flag_);
        if (send_rc != ZLINK_SUBMIT_OK)
            return send_rc;
        if (part_flag_ == ZLINK_PART_MORE) {
            binding_->in_progress = true;
            binding_->in_progress_actor_id = actor_id_;
        } else {
            binding_->in_progress = false;
            binding_->in_progress_actor_id.clear ();
        }
        return ZLINK_SUBMIT_OK;
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

int enqueue_actor_gateway_session_to_actor_locked (zlink::spot_node_t *node_,
                                                   const zlink_routing_id_t *source_node_rid_,
                                                   const zlink::spot_actor_gateway::frame_t &frame_,
                                                   zlink_msg_t *payload_,
                                                   actor_handle_t **readable_actor_out_)
{
    if (readable_actor_out_)
        *readable_actor_out_ = NULL;
    if (!node_ || !source_node_rid_ || !valid_routing_id (source_node_rid_) || !payload_) {
        errno = EINVAL;
        return -1;
    }

    zlink_actor_ref_t probe;
    memset (&probe, 0, sizeof (probe));
    if (node_->node_routing_id (&probe.node_rid) != 0)
        return -1;
    strncpy (probe.actor_id, frame_.actor_id, ZLINK_ACTOR_ID_MAX - 1);
    probe.generation = frame_.generation;

    actor_handle_t *actor = resolve_actor_ref_locked (&probe, true);
    if (!actor)
        return -1;

    actor->bound_session_node = NULL;
    actor->bound_session_node_rid = *source_node_rid_;
    actor->bound_stream = NULL;
    actor->bound_session_rid = frame_.session_rid;
    actor->last_changed_ms = now_ms ();

    queued_actor_part_t queued;
    fill_ref (actor, &queued.info.actor);
    queued.info.source_node_rid = *source_node_rid_;
    queued.info.source_session_rid = frame_.session_rid;
    queued.part_flag = frame_.part_flag;
    if (zlink_msg_adopt (&queued.part, payload_) != ZLINK_CONFIG_OK)
        return -1;
    queued.owns = true;
    actor->queue.push_back (std::move (queued));
    if (actor_gateway_debug_on) {
        std::fprintf (stderr,
                      "[actor-gateway] session->actor actor=%s part=%u queue=%zu notify=%d\n",
                      actor->actor_id.c_str (), static_cast<unsigned> (frame_.part_flag),
                      actor->queue.size (), frame_.part_flag == ZLINK_PART_FINAL ? 1 : 0);
    }
    if (readable_actor_out_ && frame_.part_flag == ZLINK_PART_FINAL)
        *readable_actor_out_ = actor;
    return 0;
}

int enqueue_actor_gateway_no_bind_locked (zlink::spot_node_t *node_,
                                          const zlink_routing_id_t *source_node_rid_,
                                          const zlink::spot_actor_gateway::frame_t &frame_,
                                          zlink_msg_t *payload_parts_,
                                          size_t payload_part_count_,
                                          actor_handle_t **readable_actor_out_)
{
    if (readable_actor_out_)
        *readable_actor_out_ = NULL;
    if (!node_ || !source_node_rid_ || !valid_routing_id (source_node_rid_)
        || !valid_multipart_payload (payload_parts_, payload_part_count_)
        || payload_part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }

    zlink_actor_ref_t probe;
    memset (&probe, 0, sizeof (probe));
    if (node_->node_routing_id (&probe.node_rid) != 0)
        return -1;
    strncpy (probe.actor_id, frame_.actor_id, ZLINK_ACTOR_ID_MAX - 1);
    probe.generation = frame_.generation;

    actor_handle_t *actor = resolve_actor_ref_locked (&probe, true);
    if (!actor)
        return -1;

    for (size_t i = 0; i < payload_part_count_; ++i) {
        queued_actor_part_t queued;
        fill_ref (actor, &queued.info.actor);
        queued.info.source_node_rid = *source_node_rid_;
        queued.info.source_session_rid = frame_.session_rid;
        queued.info.request_id = frame_.request_id;
        queued.info.flags = ZLINK_ACTOR_RECV_INFO_NO_BIND;
        queued.part_flag = i + 1 < payload_part_count_ ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
        if (zlink_msg_adopt (&queued.part, &payload_parts_[i]) != ZLINK_CONFIG_OK)
            return -1;
        queued.owns = true;
        actor->queue.push_back (std::move (queued));
    }
    actor->last_changed_ms = now_ms ();
    if (actor_gateway_debug_on) {
        std::fprintf (stderr,
                      "[actor-gateway] no-bind actor=%s parts=%zu queue=%zu notify=1\n",
                      actor->actor_id.c_str (), payload_part_count_, actor->queue.size ());
    }
    if (readable_actor_out_)
        *readable_actor_out_ = actor;
    return 0;
}

actor_session_state_t::binding_map_t::iterator
find_remote_session_binding_locked (const zlink::spot_actor_gateway::frame_t &frame_)
{
    return actor_runtime ().sessions.find_remote_binding (frame_.session_rid, frame_.actor_id,
                                                          frame_.generation);
}

int deliver_actor_gateway_actor_to_session_locked (zlink::spot_node_t *node_,
                                                   const zlink::spot_actor_gateway::frame_t &frame_,
                                                   zlink_msg_t *payload_)
{
    if (!node_ || !payload_) {
        errno = EINVAL;
        return -1;
    }

    actor_session_state_t::binding_map_t::iterator binding_it =
      find_remote_session_binding_locked (frame_);
    if (binding_it == actor_runtime ().sessions.bindings_end () || !binding_it->second.stream) {
        errno = ENOENT;
        if (actor_gateway_debug_on) {
            std::fprintf (stderr,
                          "[actor-gateway] actor->session missing session actor=%s errno=%d\n",
                          frame_.actor_id, errno);
        }
        return -1;
    }

    zlink_msg_t copied;
    const zlink_submit_result_t copy_rc = copy_msg_for_stream_send (payload_, &copied);
    if (copy_rc != ZLINK_SUBMIT_OK)
        return -1;

    const zlink_submit_result_t send_rc = send_copied_msg_to_bound_stream (
      binding_it->second.stream, &frame_.session_rid, &copied, ZLINK_DONTWAIT);
    if (send_rc != ZLINK_SUBMIT_OK) {
        const int saved_errno = errno;
        (void) zlink_msg_close (&copied);
        errno = saved_errno;
        if (actor_gateway_debug_on) {
            std::fprintf (stderr,
                          "[actor-gateway] actor->session stream send failed actor=%s errno=%d\n",
                          frame_.actor_id, errno);
        }
        return -1;
    }
    if (actor_gateway_debug_on) {
        std::fprintf (stderr, "[actor-gateway] actor->session delivered actor=%s\n",
                      frame_.actor_id);
    }
    return 0;
}

}

int zlink::spot_actor_internal::process_gateway_delivery (
  void *node_, const zlink_routing_id_t *source_node_rid_, zlink_msg_t *parts_, size_t part_count_)
{
    if (!node_ || !source_node_rid_ || !parts_ || part_count_ == 0) {
        errno = EINVAL;
        return -1;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!is_registered_spot_node_handle (node)) {
        errno = EFAULT;
        return -1;
    }

    zlink::spot_actor_gateway::frame_t frame;
    if (!zlink::spot_actor_gateway::parse_control_msg (&parts_[0], &frame))
        return -1;

    if (frame.kind == zlink::spot_actor_gateway::packet_actor_to_server_no_bind_reply) {
        return process_actor_gateway_no_bind_reply (
          source_node_rid_, frame, part_count_ > 1 ? &parts_[1] : NULL,
          part_count_ > 1 ? part_count_ - 1 : 0);
    }

    actor_handle_t *readable_actor = NULL;
    actor_handle_t *source_actor_to_remove = NULL;
    queued_join_request_t *completed_join = NULL;
    spot_handle_t *join_notify_spot = NULL;
    actor_no_bind_reply_t no_bind_reply;
    int rc = -1;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        if (frame.kind == zlink::spot_actor_gateway::packet_session_to_actor) {
            if (part_count_ != 2) {
                errno = EPROTO;
                rc = -1;
            } else {
                rc = enqueue_actor_gateway_session_to_actor_locked (node, source_node_rid_, frame,
                                                                    &parts_[1], &readable_actor);
            }
        } else if (frame.kind == zlink::spot_actor_gateway::packet_actor_to_session) {
            if (part_count_ != 2) {
                errno = EPROTO;
                rc = -1;
            } else {
                rc = deliver_actor_gateway_actor_to_session_locked (node, frame, &parts_[1]);
            }
        } else if (frame.kind == zlink::spot_actor_gateway::packet_server_to_actor_no_bind) {
            if (part_count_ < 2) {
                errno = EPROTO;
                rc = -1;
            } else {
                rc = enqueue_actor_gateway_no_bind_locked (
                  node, source_node_rid_, frame, &parts_[1], part_count_ - 1, &readable_actor);
            }
            prepare_no_bind_reply_after_enqueue (node, source_node_rid_, frame, rc, errno,
                                                 &no_bind_reply);
        } else if (frame.kind == zlink::spot_actor_gateway::packet_entry_join_request) {
            rc = enqueue_actor_gateway_entry_join_request_locked (
              node, source_node_rid_, frame, part_count_ > 1 ? &parts_[1] : NULL,
              part_count_ > 1 ? part_count_ - 1 : 0, true, &join_notify_spot);
        } else if (frame.kind == zlink::spot_actor_gateway::packet_entry_join_reply) {
            rc = process_actor_gateway_entry_join_reply_locked (
              node, source_node_rid_, frame, part_count_ > 1 ? &parts_[1] : NULL,
              part_count_ > 1 ? part_count_ - 1 : 0, true, &completed_join,
              &source_actor_to_remove);
        } else if (frame.kind == zlink::spot_actor_gateway::packet_spot_join_request) {
            rc = enqueue_actor_gateway_entry_join_request_locked (
              node, source_node_rid_, frame, part_count_ > 1 ? &parts_[1] : NULL,
              part_count_ > 1 ? part_count_ - 1 : 0, false, &join_notify_spot);
        } else if (frame.kind == zlink::spot_actor_gateway::packet_spot_join_reply) {
            rc = process_actor_gateway_entry_join_reply_locked (
              node, source_node_rid_, frame, part_count_ > 1 ? &parts_[1] : NULL,
              part_count_ > 1 ? part_count_ - 1 : 0, false, &completed_join,
              &source_actor_to_remove);
        } else {
            errno = EPROTO;
            rc = -1;
        }
    }

    if (actor_gateway_debug_on) {
        std::fprintf (
          stderr, "[actor-gateway] processed kind=%u actor=%s rc=%d errno=%d readable=%d\n",
          static_cast<unsigned> (frame.kind), frame.actor_id, rc, errno, readable_actor ? 1 : 0);
    }
    if (rc == 0 && readable_actor)
        notify_actor_readable (readable_actor);
    if (rc == 0 && join_notify_spot)
        zlink_spot_notify_dispatch_info (join_notify_spot,
                                         ZLINK_SPOT_DISPATCH_EVENT_ACTOR_JOIN_READABLE,
                                         ZLINK_SPOT_DISPATCH_SUBJECT_SPOT, join_notify_spot);
    if (rc == 0 && source_actor_to_remove) {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        std::unique_ptr<actor_handle_t> retired =
          remove_actor_locked (source_actor_to_remove, false);
        LIBZLINK_UNUSED (retired);
    }
    if (no_bind_reply.should_send && valid_routing_id (&no_bind_reply.source_node_rid)) {
        zlink_submit_result_t send_rc = send_no_bind_reply_from_owner (
          node, no_bind_reply.source_node_rid, no_bind_reply.target_node_rid,
          no_bind_reply.caller_endpoint_rid, no_bind_reply.actor_id, no_bind_reply.generation,
          no_bind_reply.request_id, no_bind_reply.result, NULL, 0);
        if (send_rc == ZLINK_SUBMIT_OK)
            rc = 0;
        else
            rc = -1;
    }
    if (rc == 0 && completed_join) {
        complete_join_request (completed_join, ZLINK_REQUEST_OK);
        release_join_request_after_completion (completed_join);
    }
    return rc;
}

void zlink_actor_run_lifecycle_for_spot (void *spot_)
{
    drain_lifecycle_events_for_spot (as_spot_handle (spot_));
}

extern "C" zlink_config_result_t
zlink_spot_node_actor_new (void *node_, const char *actor_id_, zlink_actor_ref_t *actor_out_)
{
    return zlink_spot_node_actor_new_with_request (node_, actor_id_, NULL, 0, actor_out_);
}

extern "C" zlink_config_result_t
zlink_spot_node_actor_new_with_request (void *node_,
                                        const char *actor_id_,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        zlink_actor_ref_t *actor_out_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!valid_actor_id (actor_id_) || !actor_out_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!valid_multipart_payload (parts_, part_count_))
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    if (!node->routed_enabled ()) {
        errno = ENOTSUP;
        return ZLINK_CONFIG_NOT_SUPPORTED;
    }
    zlink::spot_owned_msg_parts_t request_parts;
    const zlink_submit_result_t adopt_rc =
      adopt_multipart_payload (&request_parts, parts_, part_count_);
    if (adopt_rc != ZLINK_SUBMIT_OK) {
        zlink::spot_clear_msg_parts (&request_parts);
        return zlink::config_result_internal::from_errno (errno);
    }

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node->node_routing_id (&node_rid) != 0) {
        zlink::spot_clear_msg_parts (&request_parts);
        return zlink::config_result_internal::from_errno (errno);
    }

    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    actor_handle_t *actor = create_actor_locked (node, node_rid, actor_id_);
    if (!actor) {
        zlink::spot_clear_msg_parts (&request_parts);
        return zlink::config_result_internal::from_errno (errno);
    }
    fill_ref (actor, actor_out_);
    actor->join_epoch = next_commit_epoch_locked ();
    create_active_route_locked (actor);
    zlink_actor_ref_t zero_actor;
    zlink_routing_id_t zero_spot;
    memset (&zero_actor, 0, sizeof (zero_actor));
    memset (&zero_spot, 0, sizeof (zero_spot));
    const zlink_spot_actor_lifecycle_info_t info = make_lifecycle_info (
      zero_actor, *actor_out_, zero_spot, actor_current_spot_rid_locked (actor), actor->join_epoch);
    schedule_lifecycle_event_locked (actor->joined_spot_state, ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED,
                                     info, &request_parts);
    zlink::spot_clear_msg_parts (&request_parts);
    return ZLINK_CONFIG_OK;
}

void register_actor_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    actor_runtime ().nodes.register_spot (spot_);
}

void register_actor_spot_node (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node_->node_routing_id (&node_rid) != 0)
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    actor_runtime ().nodes.register_node (node_, node_rid);
}

void erase_actor_spot_node (zlink::spot_node_t *node_)
{
    if (!node_)
        return;
    std::vector<std::unique_ptr<actor_handle_t>> actors_to_delete;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        actor_runtime ().routes.erase_disconnected_for_node (node_);
        actor_runtime ().nodes.erase_node_routes (node_);
        while (!zlink::spot_node_access_t::actors_by_id (node_).empty ()) {
            actor_handle_t *actor =
              zlink::spot_node_access_t::actors_by_id (node_).begin ()->second;
            actors_to_delete.push_back (remove_actor_locked (actor, false));
        }
        actor_runtime ().sessions.erase_stream_owners_for_node (node_);
        actor_runtime ().nodes.erase_known_node (node_);
    }
}

void note_actor_spot_node_peer_disconnected (zlink::spot_node_t *node_,
                                             const zlink_routing_id_t *target_node_rid_)
{
    if (!node_ || !valid_routing_id (target_node_rid_))
        return;
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    actor_runtime ().routes.mark_disconnected (node_, *target_node_rid_);
}

void erase_actor_spot_facade (spot_handle_t *spot_)
{
    if (!spot_)
        return;
    std::deque<queued_join_request_t *> pending;
    const std::shared_ptr<spot_logical_state_t> logical_state = spot_->logical_state;
    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        spot_handle_t *replacement =
          actor_runtime ().nodes.find_replacement_spot (spot_, logical_state);
        actor_runtime ().nodes.erase_spot (spot_);
        if (replacement) {
            replace_live_join_spot_locked (spot_, replacement);
            return;
        }
        spot_logical_state_t *key = join_queue_key (logical_state);
        if (logical_state && logical_state->entry) {
            (void) key;
            replace_live_join_spot_locked (spot_, NULL);
            actor_runtime ().lifecycle.clear (logical_state.get ());
            return;
        }
        actor_runtime ().lifecycle.clear (logical_state.get ());
        take_join_queue_locked (key, &pending);
        collect_live_join_requests_for_state_locked (logical_state, &pending);
    }
    for (std::deque<queued_join_request_t *>::iterator it = pending.begin (); it != pending.end ();
         ++it) {
        {
            std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
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
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    if (spot->logical_state && spot->logical_state->entry)
        return 0;
    if (actor_runtime ().nodes.has_peer_spot_facade (spot))
        return 0;
    std::vector<actor_handle_t *> actors;
    actor_runtime ().nodes.collect_actor_handles (&actors);
    for (std::vector<actor_handle_t *>::const_iterator it = actors.begin (); it != actors.end ();
         ++it) {
        if ((*it)->joined_spot_state && (*it)->joined_spot_state == spot->logical_state)
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
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    std::vector<actor_handle_t *> actors;
    actor_runtime ().nodes.collect_actor_handles (&actors);
    for (std::vector<actor_handle_t *>::const_iterator it = actors.begin (); it != actors.end ();
         ++it) {
        actor_handle_t *actor = *it;
        if (!actor->pending_remote_join && actor->joined_spot_state
            && actor->joined_spot_state == spot->logical_state && !actor->queue.empty ()) {
            zlink_spot_notify_dispatch_info (spot, ZLINK_SPOT_DISPATCH_EVENT_ACTOR_READABLE,
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
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    return zlink::spot_node_access_t::actors_by_id (
             static_cast<zlink::spot_node_t *> (node_))
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
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        drain_queued_join_requests_for_stream_locked (stream_, &aborted_joins);
        std::vector<queued_join_request_t *> received_aborts;
        collect_received_join_requests_for_stream_locked (stream_, aborted_joins, &received_aborts);
        for (std::vector<queued_join_request_t *>::iterator it = received_aborts.begin ();
             it != received_aborts.end (); ++it) {
            queued_join_request_t *request = *it;
            remove_pending_join_request_locked (request);
            clear_actor_bound_session_locked (request->actor, true);
            retire_join_request_locked (request);
            aborted_joins.push_back (request);
        }
        actor_runtime ().sessions.clear_stream (stream_);
    }
    for (std::deque<queued_join_request_t *>::iterator it = aborted_joins.begin ();
         it != aborted_joins.end (); ++it) {
        complete_join_request (*it, ZLINK_REQUEST_TERMINATED);
        release_join_request_after_completion (*it);
    }
}

extern "C" zlink_config_result_t
zlink_spot_node_actor_lookup (void *node_, const char *actor_id_, zlink_actor_ref_t *out_)
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

    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    std::map<std::string, actor_handle_t *> &actors =
      zlink::spot_node_access_t::actors_by_id (static_cast<zlink::spot_node_t *> (node_));
    const std::map<std::string, actor_handle_t *>::const_iterator actor_it =
      actors.find (actor_id_);
    if (actor_it == actors.end () || actor_it->second->pending_remote_join) {
        errno = ENOENT;
        return ZLINK_CONFIG_NOT_FOUND;
    }
    fill_ref (actor_it->second, out_);
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_submit_result_t
zlink_remote_actor_get_ref (void *node_,
                            const zlink_routing_id_t *target_node_rid_,
                            const char *actor_id_,
                            zlink_actor_lookup_handler_fn handler_,
                            void *userdata_,
                            uint32_t timeout_ms_)
{
    if (!node_ || !valid_routing_id (target_node_rid_) || !valid_actor_id (actor_id_)
        || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    actor_lookup_operation_arg_t *arg = new_actor_lookup_operation_arg ();
    if (!arg)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    arg->request_node = static_cast<zlink::spot_node_t *> (node_);
    arg->target_node_rid = *target_node_rid_;
    strncpy (arg->actor_id, actor_id_, ZLINK_ACTOR_ID_MAX - 1);
    return zlink::spot_actor_async::schedule_lookup_operation (handler_, userdata_, timeout_ms_,
                                                               run_actor_lookup_operation, arg,
                                                               cleanup_actor_lookup_operation_arg);
}

extern "C" zlink_submit_result_t zlink_spot_node_actor_destroy (void *node_,
                                                                const zlink_actor_ref_t *actor_,
                                                                zlink_reply_handler_fn handler_,
                                                                void *userdata_,
                                                                uint32_t timeout_ms_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!actor_ || !valid_actor_id (actor_->actor_id) || !valid_routing_id (&actor_->node_rid)
        || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (zlink::spot_actor_lifecycle::reenters_same_actor (actor_)) {
        errno = EDEADLK;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    actor_reply_operation_arg_t *arg = new_actor_reply_operation_arg (run_destroy_operation_locked);
    if (!arg)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    arg->request_node = static_cast<zlink::spot_node_t *> (node_);
    arg->actor = *actor_;
    return schedule_actor_reply_operation (handler_, userdata_, timeout_ms_, arg);
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
        || !valid_actor_id (actor_ref_->actor_id) || !valid_routing_id (&actor_ref_->node_rid)
        || !valid_routing_id (dest_node_rid_) || !valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!valid_multipart_payload (parts_, part_count_))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if (!is_registered_spot_node_handle (node_)) {
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
        } else if (actor_has_pending_join_locked (actor)) {
            errno = EBUSY;
            return ZLINK_SUBMIT_INVALID_STATE;
        }
    }
    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_join_result (parts_, part_count_, handler_, userdata_,
                                               immediate_result);

    {
        std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
        actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
        if (!actor)
            immediate_result = actor_missing_request_result_from_errno ();
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
            request->source_spot_rid = actor_current_spot_rid_locked (actor);
            fill_ref (actor, &request->source_actor_ref);
            request->target_actor_ref.node_rid = *dest_node_rid_;
            strncpy (request->target_actor_ref.actor_id, actor->actor_id.c_str (),
                     ZLINK_ACTOR_ID_MAX - 1);
            request->handler = handler_;
            request->userdata = userdata_;
            request->join_epoch = next_commit_epoch_locked ();
            index_join_request_locked (request);
            external_gateway_join = true;
            external_source_node = actor_runtime ().nodes.resolve_node_by_rid (actor->node_rid);
            external_source_node_rid = actor->node_rid;
        } else if (immediate_result != ZLINK_REQUEST_OK) {
            /* Complete outside the actor lock. */
        } else {
            std::shared_ptr<spot_logical_state_t> target_state =
              zlink::spot_node_access_t::lookup_spot_state (target_node, dest_spot_rid_);
            spot = find_spot_facade_for_state_locked (target_node, target_state);
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
                const zlink_routing_id_t current_spot = actor_current_spot_rid_locked (actor);
                if (!actor_runtime ().routes.active_matches (actor))
                    create_active_route_locked (actor);
                return complete_idempotent_join_async (parts_, part_count_, handler_, userdata_,
                                                       &current_ref, &current_spot,
                                                       actor->join_epoch);
            } else if (!same_routing_id (actor->node_rid, *dest_node_rid_)
                       && actor_route_disconnected_locked (actor->node, *dest_node_rid_)) {
                immediate_result = ZLINK_REQUEST_NOT_CONNECTED;
            } else if (!same_routing_id (actor->node_rid, *dest_node_rid_)) {
                std::map<std::string, actor_handle_t *> &target_actors =
                  zlink::spot_node_access_t::actors_by_id (target_node);
                if (target_actors.count (actor->actor_id) != 0
                    || node_has_pending_join_actor_locked (target_node, actor->actor_id.c_str ())) {
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
            request->remote = !same_routing_id (actor->node_rid, *dest_node_rid_);
            request->target_node_rid = *dest_node_rid_;
            request->source_spot_rid = actor_current_spot_rid_locked (actor);
            if (request->remote) {
                request->target_actor_ref.node_rid = *dest_node_rid_;
                strncpy (request->target_actor_ref.actor_id, actor->actor_id.c_str (),
                         ZLINK_ACTOR_ID_MAX - 1);
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
              adopt_multipart_payload (&request->message_parts, parts_, part_count_);
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
        return complete_immediate_join_result (parts_, part_count_, handler_, userdata_,
                                               immediate_result);

    if (external_gateway_join) {
        const zlink_submit_result_t send_rc = send_actor_gateway_multipart_from_source (
          external_source_node, external_source_node_rid, *dest_node_rid_,
          zlink::spot_actor_gateway::packet_spot_join_request, *dest_spot_rid_,
          actor_ref_->actor_id, actor_ref_->generation, request->join_epoch, 0, parts_, part_count_,
          flags_);
        if (send_rc != ZLINK_SUBMIT_OK) {
            const zlink_request_result_t failure = errno_to_request_result (errno);
            {
                std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
                retire_join_request_locked (request);
            }
            complete_join_request (request, failure);
            release_join_request_after_completion (request);
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
    if (!node_ || !actor_ || !dest_node_rid_ || !handler_ || !valid_actor_id (actor_->actor_id)
        || !valid_routing_id (&actor_->node_rid) || !valid_routing_id (dest_node_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!valid_multipart_payload (parts_, part_count_))
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    if ((flags_ & ~ZLINK_DONTWAIT) != 0) {
        errno = ENOTSUP;
        return ZLINK_SUBMIT_NOT_SUPPORTED;
    }
    if (!is_registered_spot_node_handle (node_)) {
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
        } else if (actor_has_pending_join_locked (actor)) {
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
                request->source_spot_rid = actor_current_spot_rid_locked (actor);
                fill_ref (actor, &request->source_actor_ref);
                request->target_actor_ref.node_rid = *dest_node_rid_;
                strncpy (request->target_actor_ref.actor_id, actor->actor_id.c_str (),
                         ZLINK_ACTOR_ID_MAX - 1);
                request->entry_handler = handler_;
                request->userdata = userdata_;
                request->entry_spot_join = true;
                request->join_epoch = next_commit_epoch_locked ();
                index_join_request_locked (request);
                external_gateway_join = true;
                external_source_node = source_node;
                external_source_node_rid = actor->node_rid;
                external_source_spot_rid = request->source_spot_rid;
            } else if (!same_routing_id (actor->node_rid, *dest_node_rid_)
                       && actor_route_disconnected_locked (actor->node, *dest_node_rid_)) {
                immediate_result = ZLINK_REQUEST_NOT_CONNECTED;
            } else {
                std::shared_ptr<spot_logical_state_t> target_state =
                  zlink::spot_node_access_t::entry_spot_state (target_node);
                spot = find_spot_facade_for_state_locked (target_node, target_state);
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
                    request->remote = !same_routing_id (actor->node_rid, *dest_node_rid_);
                    request->target_node_rid = *dest_node_rid_;
                    request->source_spot_rid = actor_current_spot_rid_locked (actor);
                    request->entry_handler = handler_;
                    request->userdata = userdata_;
                    request->entry_spot_join = true;
                    request->join_epoch = next_commit_epoch_locked ();
                    if (request->remote) {
                        std::map<std::string, actor_handle_t *> &target_actors =
                          zlink::spot_node_access_t::actors_by_id (target_node);
                        if (target_actors.count (actor->actor_id) != 0
                            || node_has_pending_join_actor_locked (target_node,
                                                                   actor->actor_id.c_str ())) {
                            delete request;
                            request = NULL;
                            immediate_result = ZLINK_REQUEST_CONFLICT;
                        } else {
                            request->target_actor_ref.node_rid = *dest_node_rid_;
                            strncpy (request->target_actor_ref.actor_id, actor->actor_id.c_str (),
                                     ZLINK_ACTOR_ID_MAX - 1);
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
                        }
                    } else {
                        fill_ref (actor, &request->target_actor_ref);
                    }
                    if (request) {
                        const zlink_submit_result_t adopt_rc =
                          adopt_multipart_payload (&request->message_parts, parts_, part_count_);
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
            }
        }
    }

    if (immediate_result != ZLINK_REQUEST_OK)
        return complete_immediate_entry_join_result (parts_, part_count_, handler_, userdata_,
                                                     dest_node_rid_, immediate_result);

    if (external_gateway_join) {
        const zlink_submit_result_t send_rc = send_actor_gateway_multipart_from_source (
          external_source_node, external_source_node_rid, *dest_node_rid_,
          zlink::spot_actor_gateway::packet_entry_join_request, external_source_spot_rid,
          actor_->actor_id, actor_->generation, request->join_epoch, 0, parts_, part_count_,
          flags_);
        if (send_rc != ZLINK_SUBMIT_OK) {
            const zlink_request_result_t failure = errno_to_request_result (errno);
            {
                std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
                retire_join_request_locked (request);
            }
            complete_join_request (request, failure);
            release_join_request_after_completion (request);
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
zlink_spot_node_actor_leave_spot (void *node_,
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
    if (!actor_ref_ || !dest_spot_rid_ || !valid_actor_id (actor_ref_->actor_id)
        || !valid_routing_id (dest_spot_rid_) || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (zlink::spot_actor_lifecycle::reenters_same_actor (actor_ref_)) {
        errno = EDEADLK;
        return ZLINK_SUBMIT_INVALID_STATE;
    }

    actor_reply_operation_arg_t *arg = new_actor_reply_operation_arg (run_leave_operation_locked);
    if (!arg)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    arg->request_node = static_cast<zlink::spot_node_t *> (node_);
    arg->actor = *actor_ref_;
    arg->rid = *dest_spot_rid_;
    return schedule_actor_reply_operation (handler_, userdata_, timeout_ms_, arg);
}

extern "C" zlink_recv_result_t zlink_spot_node_actor_recv_part (void *node_,
                                                                const zlink_actor_ref_t *actor_ref_,
                                                                zlink_actor_recv_info_t *info_out_,
                                                                zlink_msg_t *part_out_,
                                                                zlink_part_flag_t *has_more_out_,
                                                                zlink_recv_flags_t flags_)
{
    if (!node_ || !actor_ref_ || !info_out_ || !part_out_ || !has_more_out_) {
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

    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
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

extern "C" zlink_submit_result_t zlink_stream_bind_actor (void *stream_,
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

    actor_reply_operation_arg_t *arg = new_actor_reply_operation_arg (run_bind_operation_locked);
    if (!arg)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    arg->stream = stream_;
    arg->actor = *actor_ref_;
    arg->rid = *session_rid_;
    return schedule_actor_reply_operation (handler_, userdata_, timeout_ms_, arg);
}

extern "C" zlink_submit_result_t zlink_stream_unbind_actor (void *stream_,
                                                            const zlink_routing_id_t *session_rid_,
                                                            const char *actor_id_,
                                                            zlink_reply_handler_fn handler_,
                                                            void *userdata_,
                                                            uint32_t timeout_ms_)
{
    if (!stream_ || !valid_routing_id (session_rid_) || !valid_actor_id (actor_id_) || !handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    actor_reply_operation_arg_t *arg = new_actor_reply_operation_arg (run_unbind_operation_locked);
    if (!arg)
        return ZLINK_SUBMIT_OUT_OF_MEMORY;
    arg->stream = stream_;
    arg->rid = *session_rid_;
    strncpy (arg->actor_id, actor_id_, ZLINK_ACTOR_ID_MAX - 1);
    return schedule_actor_reply_operation (handler_, userdata_, timeout_ms_, arg);
}

extern "C" zlink_submit_result_t
zlink_stream_send_bound_actor_part (void *stream_,
                                    const zlink_routing_id_t *session_rid_,
                                    const char *actor_id_,
                                    zlink_msg_t *part_,
                                    zlink_send_flags_t flags_,
                                    zlink_part_flag_t part_flag_)
{
    LIBZLINK_UNUSED (flags_);
    if (!stream_ || !valid_routing_id (session_rid_) || !valid_actor_id (actor_id_) || !part_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_stream_socket (stream_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    actor_handle_t *actor = NULL;
    {
        std::unique_lock<std::timed_mutex> lock (actor_runtime ().mutex, std::defer_lock);
        if (!lock.try_lock ()) {
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        actor_session_state_t::binding_map_t::iterator binding_it =
          actor_runtime ().sessions.find_binding (stream_, session_rid_);
        if (binding_it == actor_runtime ().sessions.bindings_end ()) {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
        session_binding_t &binding = binding_it->second;
        zlink::spot_node_t *stream_owner =
          actor_runtime ().sessions.stream_owner (stream_, actor_runtime ().nodes);
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
        const zlink_submit_result_t enqueue_rc = enqueue_bound_actor_part_locked (
          stream_owner, &binding, &it->second, session_rid_, actor_id_, part_, part_flag_, &actor);
        if (enqueue_rc != ZLINK_SUBMIT_OK)
            return enqueue_rc;
    }
    if (actor && !actor->pending_remote_join)
        notify_actor_readable (actor);
    return ZLINK_SUBMIT_OK;
}

namespace zlink
{
}

extern "C" zlink_submit_result_t
zlink_spot_node_actor_send_bound_session_msg (void *node_,
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
    zlink_routing_id_t remote_session_node_rid;
    memset (&remote_session_node_rid, 0, sizeof (remote_session_node_rid));
    zlink::spot_node_t *request_node = static_cast<zlink::spot_node_t *> (node_);
    uint64_t actor_generation = 0;
    bool remote_session = false;
    {
        std::unique_lock<std::timed_mutex> lock (actor_runtime ().mutex, std::defer_lock);
        if (!lock.try_lock ()) {
            errno = EAGAIN;
            return ZLINK_SUBMIT_BACKPRESSURED;
        }
        const actor_resolution_t resolved =
          resolve_actor_for_request_locked (request_node, actor_ref_);
        if (resolved.result != ZLINK_REQUEST_OK)
            return request_result_to_submit_result (resolved.result);
        actor_handle_t *actor = resolved.actor;
        actor_generation = actor->generation;
        if (actor->bound_stream) {
            const zlink_submit_result_t bound_rc =
              validate_actor_bound_session_locked (actor, &stream, &session_rid);
            if (bound_rc != ZLINK_SUBMIT_OK)
                return bound_rc;
        } else if (valid_routing_id (&actor->bound_session_node_rid)
                   && valid_routing_id (&actor->bound_session_rid)) {
            remote_session = true;
            remote_session_node_rid = actor->bound_session_node_rid;
            session_rid = actor->bound_session_rid;
        } else {
            errno = ENOENT;
            return ZLINK_SUBMIT_NOT_FOUND;
        }
    }

    if (remote_session) {
        return send_actor_gateway_packet (
          request_node, remote_session_node_rid, zlink::spot_actor_gateway::packet_actor_to_session,
          session_rid, actor_ref_->actor_id, actor_generation, message_, flags_, ZLINK_PART_FINAL);
    }

    zlink_msg_t copied_message;
    const zlink_submit_result_t copy_rc = copy_msg_for_stream_send (message_, &copied_message);
    if (copy_rc != ZLINK_SUBMIT_OK)
        return copy_rc;

    const zlink_submit_result_t send_rc =
      send_copied_msg_to_bound_stream (stream, &session_rid, &copied_message, flags_);
    if (send_rc != ZLINK_SUBMIT_OK) {
        (void) zlink_msg_close (&copied_message);
        return send_rc;
    }
    (void) zlink_msg_close (message_);
    (void) zlink_msg_init (message_);
    return ZLINK_SUBMIT_OK;
}

extern "C" zlink_submit_result_t
zlink_spot_node_actor_forward_bound_session_part (void *node_,
                                                  const zlink_actor_ref_t *actor_ref_,
                                                  const zlink_routing_id_t *source_node_rid_,
                                                  const zlink_routing_id_t *source_session_rid_,
                                                  zlink_msg_t *message_,
                                                  zlink_send_flags_t flags_,
                                                  zlink_part_flag_t part_flag_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!actor_ref_ || !source_node_rid_ || !source_session_rid_ || !message_
        || !valid_actor_id (actor_ref_->actor_id) || !valid_routing_id (&actor_ref_->node_rid)
        || actor_ref_->generation == 0 || !valid_routing_id (source_node_rid_)
        || !valid_routing_id (source_session_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    return send_actor_gateway_packet_from_source (
      static_cast<zlink::spot_node_t *> (node_), *source_node_rid_, actor_ref_->node_rid,
      zlink::spot_actor_gateway::packet_session_to_actor, *source_session_rid_,
      actor_ref_->actor_id, actor_ref_->generation, message_, flags_, part_flag_);
}

extern "C" zlink_submit_result_t
zlink_spot_node_send_to_actor (void *node_,
                               const zlink_actor_ref_t *actor_ref_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_reply_handler_fn completion_,
                               void *userdata_,
                               zlink_send_flags_t flags_,
                               uint32_t timeout_ms_)
{
    return submit_actor_no_bind (node_, actor_ref_, parts_, part_count_, completion_, userdata_,
                                 flags_, timeout_ms_, true);
}

extern "C" zlink_submit_result_t
zlink_spot_node_request_to_actor (void *node_,
                                  const zlink_actor_ref_t *actor_ref_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_reply_handler_fn callback_,
                                  void *userdata_,
                                  zlink_send_flags_t flags_,
                                  uint32_t timeout_ms_)
{
    return submit_actor_no_bind (node_, actor_ref_, parts_, part_count_, callback_, userdata_,
                                 flags_, timeout_ms_, false);
}

extern "C" zlink_submit_result_t zlink_spot_node_actor_reply_no_bind (
  void *node_,
  const zlink_actor_recv_info_t *info_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_request_result_t result_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }
    if (!info_ || (part_count_ > 0 && !parts_) || !valid_multipart_payload (parts_, part_count_)
        || !(info_->flags & ZLINK_ACTOR_RECV_INFO_NO_BIND) || info_->request_id == 0
        || !valid_actor_id (info_->actor.actor_id) || !valid_routing_id (&info_->actor.node_rid)
        || info_->actor.generation == 0 || !valid_routing_id (&info_->source_node_rid)
        || !valid_routing_id (&info_->source_session_rid)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::spot_node_t *owner_node = static_cast<zlink::spot_node_t *> (node_);
    zlink_routing_id_t owner_node_rid;
    memset (&owner_node_rid, 0, sizeof (owner_node_rid));
    if (owner_node->node_routing_id (&owner_node_rid) != 0)
        return errno_to_submit_result (errno);
    if (!same_routing_id (owner_node_rid, info_->actor.node_rid)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    return send_no_bind_reply_from_owner (owner_node, owner_node_rid, info_->source_node_rid,
                                          info_->source_session_rid, info_->actor.actor_id,
                                          info_->actor.generation, info_->request_id, result_,
                                          parts_, part_count_);
}

extern "C" zlink_config_result_t
zlink_spot_node_actor_bind_remote_session (void *node_,
                                           const zlink_actor_ref_t *actor_ref_,
                                           const zlink_routing_id_t *source_node_rid_,
                                           const zlink_routing_id_t *source_session_rid_)
{
    if (!node_) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }
    if (!actor_ref_ || !source_node_rid_ || !source_session_rid_
        || !valid_actor_id (actor_ref_->actor_id) || !valid_routing_id (&actor_ref_->node_rid)
        || actor_ref_->generation == 0 || !valid_routing_id (source_node_rid_)
        || !valid_routing_id (source_session_rid_)) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    if (!is_registered_spot_node_handle (node_)) {
        errno = EFAULT;
        return ZLINK_CONFIG_INVALID_HANDLE;
    }

    zlink::spot_node_t *request_node = static_cast<zlink::spot_node_t *> (node_);
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    const actor_resolution_t resolved =
      resolve_actor_for_request_locked (request_node, actor_ref_, true);
    if (resolved.result != ZLINK_REQUEST_OK) {
        errno = resolved.result == ZLINK_REQUEST_NOT_CONNECTED ? ENOTCONN : ENOENT;
        return zlink::config_result_internal::from_errno (errno);
    }

    actor_handle_t *actor = resolved.actor;
    actor->bound_session_node = NULL;
    actor->bound_session_node_rid = *source_node_rid_;
    actor->bound_stream = NULL;
    actor->bound_session_rid = *source_session_rid_;
    actor->last_changed_ms = now_ms ();
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_recv_result_t zlink_spot_recv_actor_lifecycle (
  void *spot_, zlink_spot_actor_lifecycle_event_t *event_out_, zlink_recv_flags_t flags_)
{
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const zlink_recv_result_t result =
      zlink_spot_recv_actor_lifecycle_with_request (spot_, event_out_, &parts, &part_count, flags_);
    if (result == ZLINK_RECV_OK)
        zlink_multipart_close (parts, part_count);
    return result;
}

extern "C" zlink_recv_result_t
zlink_spot_recv_actor_lifecycle_with_request (void *spot_,
                                              zlink_spot_actor_lifecycle_event_t *event_out_,
                                              zlink_msg_t **parts_out_,
                                              size_t *part_count_out_,
                                              zlink_recv_flags_t flags_)
{
    if (!spot_ || !event_out_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::recv_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->logical_state) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }

    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    lifecycle_event_t event;
    if (!actor_runtime ().lifecycle.pop (spot->logical_state.get (), &event)) {
        errno = EAGAIN;
        return ZLINK_RECV_NO_DATA;
    }
    if (zlink::recv_tls_view::begin (parts_out_, part_count_out_) != 0)
        return ZLINK_RECV_INTERNAL_ERROR;
    for (zlink::spot_owned_msg_parts_t::iterator it = event.request_parts.begin ();
         it != event.request_parts.end (); ++it) {
        if (zlink::recv_tls_view::push (&(*it)) != 0) {
            zlink::recv_tls_view::abort ();
            return ZLINK_RECV_INTERNAL_ERROR;
        }
    }
    if (zlink::recv_tls_view::commit (parts_out_, part_count_out_) != 0) {
        zlink::recv_tls_view::abort ();
        return ZLINK_RECV_INTERNAL_ERROR;
    }
    zlink::spot_clear_msg_parts (&event.request_parts);
    memset (event_out_, 0, sizeof (*event_out_));
    event_out_->kind = event.kind;
    event_out_->info = event.info;
    return ZLINK_RECV_OK;
}

extern "C" zlink_config_result_t zlink_stream_bound_actors (void *stream_,
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

    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    actor_session_state_t::binding_map_t::const_iterator binding_it =
      actor_runtime ().sessions.find_binding (stream_, session_rid_);
    const size_t capacity = entries_ ? *count_ : 0;
    size_t written = 0;
    if (binding_it != actor_runtime ().sessions.bindings_end ()) {
        const session_binding_t &binding = binding_it->second;
        for (std::map<std::string, session_binding_t::actor_entry_t>::const_iterator it =
               binding.actors.begin ();
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
        std::unique_lock<std::timed_mutex> lock (actor_runtime ().mutex, std::defer_lock);
        const bool locked = timeout_ms_ == 0
                              ? lock.try_lock ()
                              : lock.try_lock_for (std::chrono::milliseconds (timeout_ms_));
        if (!locked) {
            errno = ETIMEDOUT;
            return ZLINK_REQUEST_TIMED_OUT;
        }
        actor_handle_t *actor = resolve_actor_ref_locked (actor_ref_);
        if (!actor)
            return actor_missing_request_result_from_errno ();
        const bool has_local_session = actor->bound_stream != NULL;
        const bool has_remote_session = valid_routing_id (&actor->bound_session_node_rid)
                                        && valid_routing_id (&actor->bound_session_rid);
        if (!has_local_session && !has_remote_session) {
            errno = ENOENT;
            return ZLINK_REQUEST_NOT_FOUND;
        }

        zlink_actor_ref_t actor_ref;
        fill_ref (actor, &actor_ref);
        const zlink_routing_id_t spot_rid = actor_current_spot_rid_locked (actor);
        const zlink_spot_actor_lifecycle_info_t lifecycle_info =
          make_lifecycle_info (actor_ref, actor_ref, spot_rid, spot_rid, actor->join_epoch);
        std::shared_ptr<spot_logical_state_t> lifecycle_spot = actor->joined_spot_state;
        if (has_local_session)
            actor_runtime ().sessions.detach_actor (actor, true, false);
        clear_actor_bound_session_locked (actor, true);
        schedule_lifecycle_event_locked (lifecycle_spot, ZLINK_SPOT_ACTOR_LIFECYCLE_DISCONNECTED,
                                         lifecycle_info);
        if (!actor->queue.empty ())
            readable_actor = actor;
    }
    if (readable_actor)
        notify_actor_readable (readable_actor);
    return ZLINK_REQUEST_OK;
}

extern "C" zlink_config_result_t
zlink_spot_node_spots (void *node_, zlink_spot_node_spot_entry_t *entries_, size_t *count_)
{
    if (!node_ || !count_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    std::vector<actor_spot_snapshot_t> spots;
    std::shared_ptr<spot_logical_state_t> entry_state =
      zlink::spot_node_access_t::entry_spot_state (static_cast<zlink::spot_node_t *> (node_));
    actor_runtime ().nodes.collect_spots_for_node (static_cast<zlink::spot_node_t *> (node_),
                                                   entry_state, &spots);
    if (!entries_) {
        *count_ = spots.size ();
        return ZLINK_CONFIG_OK;
    }
    std::vector<actor_handle_t *> actors;
    actor_runtime ().nodes.collect_actor_handles (&actors);
    const size_t limit = std::min (*count_, spots.size ());
    for (size_t i = 0; i != limit; ++i) {
        memset (&entries_[i], 0, sizeof (entries_[i]));
        entries_[i].spot_rid =
          spots[i].state ? spots[i].state->routing_id : spots[i].facade->spot_routing_id;
        entries_[i].spot_kind = spot_kind_for_state (spots[i].state);
        entries_[i].dispatch_handler_attached =
          spots[i].facade && spot_dispatch_handler_attached (spots[i].facade) ? 1u : 0u;
        for (std::vector<actor_handle_t *>::const_iterator actor_it = actors.begin ();
             actor_it != actors.end (); ++actor_it) {
            if (!(*actor_it)->pending_remote_join && (*actor_it)->joined_spot_state
                && (*actor_it)->joined_spot_state == spots[i].state)
                ++entries_[i].joined_actor_count;
        }
        entries_[i].pending_actor_join_count =
          actor_runtime ().joins.pending_count_for_spot (spots[i].state.get ());
        entries_[i].route_synced = 0u;
        entries_[i].last_changed_ms = now_ms ();
    }
    *count_ = limit;
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_config_result_t
zlink_spot_node_actors (void *node_, zlink_spot_node_actor_entry_t *entries_, size_t *count_)
{
    if (!node_ || !count_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    std::vector<actor_handle_t *> actors;
    std::map<std::string, actor_handle_t *> &node_actors =
      zlink::spot_node_access_t::actors_by_id (static_cast<zlink::spot_node_t *> (node_));
    for (std::map<std::string, actor_handle_t *>::const_iterator it = node_actors.begin ();
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
            entries_[i].current_spot_rid = actors[i]->joined_spot_state->routing_id;
            entries_[i].current_spot_kind = spot_kind_for_state (actors[i]->joined_spot_state);
        }
        entries_[i].route_synced = actor_runtime ().routes.active_matches (actors[i]) ? 1u : 0u;
        entries_[i].pending_message_count = static_cast<uint32_t> (actors[i]->queue.size ());
        entries_[i].last_changed_ms = actors[i]->last_changed_ms;
    }
    *count_ = limit;
    return ZLINK_CONFIG_OK;
}

extern "C" zlink_config_result_t
zlink_spot_actors (void *spot_, zlink_actor_ref_t *entries_, size_t *count_)
{
    if (!spot_ || !count_) {
        errno = EINVAL;
        return ZLINK_CONFIG_INVALID_ARGUMENT;
    }
    std::lock_guard<std::timed_mutex> lock (actor_runtime ().mutex);
    std::vector<actor_handle_t *> actors;
    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    actor_runtime ().nodes.collect_actor_handles (&actors);
    std::vector<actor_handle_t *> matching;
    for (std::vector<actor_handle_t *>::const_iterator it = actors.begin (); it != actors.end ();
         ++it) {
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
