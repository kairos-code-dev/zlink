/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "api/request_timeout_scheduler_internal.hpp"
#include "api/internal_pair_queue_internal.hpp"
#include "api/part_helper_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"
#include "api/socket_message_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/status_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "api/handler_result_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "utils/random.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;

bool spot_direct_route_debug_enabled ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE") != NULL;
}

bool spot_direct_route_wait_trace_enabled ()
{
    return std::getenv ("ZLINK_TRACE_SPOT_DIRECT_ROUTE_WAIT") != NULL;
}

bool spot_direct_route_enabled ()
{
    return std::getenv ("ZLINK_ENABLE_SPOT_DIRECT_ROUTE") != NULL;
}

using zlink::spot_reqrep_internal::g_spot_recv_source_rid;
using zlink::spot_reqrep_internal::g_spot_recv_spot_rid;
using zlink::spot_reqrep_internal::g_spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::g_spot_state_identity_index;
using zlink::spot_reqrep_internal::g_router_state_identity_index;
using zlink::spot_reqrep_internal::has_valid_routing_id;
using zlink::spot_reqrep_internal::init_buffer_frame;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::pending_reply_t;
using zlink::spot_reqrep_internal::pending_spot_key_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::router_state_identity_index_t;
using zlink::spot_reqrep_internal::resolve_spot_ctx;
using zlink::spot_reqrep_internal::resolve_spot_identity;
using zlink::spot_reqrep_internal::routing_id_key;
using zlink::spot_reqrep_internal::routing_pair_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_state_identity_index_t;
using zlink::spot_reqrep_internal::spot_state_spot_index_t;
using zlink::spot_reqrep_internal::bind_router_state_rid;
using zlink::spot_reqrep_internal::erase_spot_owner_state;
using zlink::spot_reqrep_internal::find_or_create_router_state;
using zlink::spot_reqrep_internal::find_or_create_spot_state;
using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::install_spot_dispatch_event_task;
using zlink::spot_reqrep_internal::maybe_dispatch_spot_info;
using zlink::spot_reqrep_internal::close_spot_dispatch_parts;
using zlink::spot_reqrep_internal::close_spot_subscribe_dispatch_queue;
using zlink::spot_reqrep_internal::queue_spot_message;
using zlink::spot_reqrep_internal::queue_spot_subscribe_message;
using zlink::spot_reqrep_internal::recv_internal_spot_queue;
using zlink::spot_reqrep_internal::recv_internal_spot_subscribe_queue;
using zlink::spot_reqrep_internal::recv_combined_router_message;
using zlink::spot_reqrep_internal::build_spot_request_reply_message;
using zlink::spot_reqrep_internal::build_spot_routed_message;
using zlink::spot_reqrep_internal::routed_spot_delivery_direct;
using zlink::spot_reqrep_internal::routed_spot_delivery_reply;
using zlink::spot_reqrep_internal::routed_spot_delivery_request;
using zlink::spot_reqrep_internal::router_spot_delivery_direct;
using zlink::spot_reqrep_internal::router_spot_delivery_reply;
using zlink::spot_reqrep_internal::router_spot_delivery_request;
using zlink::spot_reqrep_internal::dispatch_local_reply;
using zlink::spot_reqrep_internal::dispatch_local_request;
using zlink::spot_reqrep_internal::dispatch_local_built_message;
using zlink::spot_reqrep_internal::process_parsed_route_combined_for_local_delivery;
using zlink::spot_reqrep_internal::process_route_combined_for_local_delivery;
using zlink::spot_reqrep_internal::register_router_spot_pending_request;
using zlink::spot_reqrep_internal::register_spot_pending_request;
using zlink::spot_reqrep_internal::resolve_runtime_for_spot_destination;
using zlink::spot_reqrep_internal::resolve_spot_node_routing_id;
using zlink::spot_reqrep_internal::should_process_spot_routed_locally;
using zlink::spot_reqrep_internal::try_find_spot_state;
using zlink::spot_reqrep_internal::validate_request_parts;

enum : uint8_t
{
    zmp_spot_routed_protocol_id = 0x02,
    zmp_protocol_version = 0x01,
    zmp_spot_class = 0x01,
    zmp_router_class = 0x02
};

const size_t spot_routed_control_part_count = 8;

struct channel_reply_bridge_ctx_t
{
    std::weak_ptr<spot_request_reply_state_t> state;
    void *dealer;
    zlink_reply_handler_fn handler;
    void *userdata;
};

int enqueue_runtime_route_ingress_once (zlink::spot_runtime_t *runtime_,
                                        std::vector<zlink_msg_t> *parts_,
                                        zlink_send_flags_t flags_);
int send_combined_parts_locked (zlink::socket_base_t *socket_,
                                std::vector<zlink_msg_t> *parts_,
                                zlink_send_flags_t flags_);
int recv_combined_plain_message (zlink::socket_base_t *socket_,
                                 std::vector<zlink_msg_t> *out_);

int validate_request_send_flags (zlink_send_flags_t flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

int request_result_to_errno (zlink_request_result_t result_)
{
    switch (result_) {
    case ZLINK_REQUEST_OK:
        return 0;
    case ZLINK_REQUEST_TIMED_OUT:
        return ETIMEDOUT;
    case ZLINK_REQUEST_NOT_FOUND:
        return ENOENT;
    case ZLINK_REQUEST_TERMINATED:
        return ETERM;
    case ZLINK_REQUEST_PROTOCOL_ERROR:
        return EPROTO;
    case ZLINK_REQUEST_INTERNAL_ERROR:
    default:
        return EIO;
    }
}

bool has_local_spot_route_target (uint8_t destination_class_,
                                  const std::string &destination_node_rid_,
                                  const std::string &destination_endpoint_rid_)
{
    return destination_class_ == zmp_spot_class
             ? static_cast<bool> (find_spot_state_by_identity (
                 destination_node_rid_, destination_endpoint_rid_))
             : static_cast<bool> (
                 find_router_state_by_rid (destination_endpoint_rid_));
}

bool spot_destination_is_admitted (void *spot_,
                                   const zlink_routing_id_t *dest_node_rid_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    return !spot || !spot->node || spot->node->peer_is_admitted (dest_node_rid_);
}

void notify_spot_dispatch_info (void *spot_,
                                zlink_spot_dispatch_event_t event_,
                                zlink_spot_dispatch_subject_kind_t subject_kind_,
                                void *subject_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      try_find_spot_state (spot_);
    if (!state)
        return;
    maybe_dispatch_spot_info (state.get (), event_, subject_kind_, subject_);
}

extern "C" void zlink_spot_notify_dispatch_event (
  void *spot_,
  zlink_spot_dispatch_event_t event_)
{
    notify_spot_dispatch_info (spot_, event_,
                               ZLINK_SPOT_DISPATCH_SUBJECT_SPOT, spot_);
}

extern "C" void zlink_spot_notify_dispatch_info (
  void *spot_,
  zlink_spot_dispatch_event_t event_,
  zlink_spot_dispatch_subject_kind_t subject_kind_,
  void *subject_)
{
    notify_spot_dispatch_info (spot_, event_, subject_kind_, subject_);
}

void channel_reply_bridge_completion (zlink_request_result_t result_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      void *userdata_)
{
    std::unique_ptr<channel_reply_bridge_ctx_t> bridge (
      static_cast<channel_reply_bridge_ctx_t *> (userdata_));
    if (!bridge.get ())
        return;

    std::shared_ptr<spot_request_reply_state_t> state = bridge->state.lock ();
    if (!state)
        return;

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->completion_state.pending_channel_requests > 0)
            --state->completion_state.pending_channel_requests;
    }

    const int errnum = request_result_to_errno (result_);
    (void) zlink::spot_reqrep_internal::queue_spot_channel_reply_completion (
      state, bridge->dealer, bridge->handler, bridge->userdata, errnum, parts_,
      part_count_);
}

int dispatch_spot_message (spot_request_reply_state_t *state_,
                           const zlink_routing_id_t *source_rid_,
                           const zlink_routing_id_t *spot_rid_,
                           uint64_t request_seq_,
                           zlink_msg_t *parts_,
                           size_t part_count_)
{
    zlink_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->recv.request_handler;
        handler_userdata = state_->recv.request_handler_userdata;
    }

    if (handler) {
        handler (source_rid_, spot_rid_, request_seq_, parts_, part_count_,
                 handler_userdata);
        return 0;
    }

    if (queue_spot_message (state_, source_rid_, spot_rid_, request_seq_,
                            parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }
    return 0;
}

int dispatch_router_spot_message (router_spot_request_reply_state_t *state_,
                                  const zlink_routing_id_t *source_node_rid_,
                                  const zlink_routing_id_t *source_spot_rid_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_)
{
    if (!state_ || !state_->owner) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        errno = EFAULT;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (state_->owner);
    if (!handle.socket) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> router_state =
      reqrep::find_or_create_request_reply_state (handle);
    if (reqrep::dispatch_router_message (
          router_state.get (), source_node_rid_, source_spot_rid_,
          request_seq_, parts_, part_count_)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }

    return 0;
}

void routing_id_from_string (const std::string &value_, zlink_routing_id_t *out_)
{
    if (!out_)
        return;

    memset (out_, 0, sizeof (*out_));
    if (value_.empty ())
        return;

    const size_t size =
      value_.size () > sizeof (out_->data) ? sizeof (out_->data) : value_.size ();
    memcpy (out_->data, value_.data (), size);
    out_->size = static_cast<uint8_t> (size);
}

int send_combined_parts_locked (zlink::socket_base_t *socket_,
                                std::vector<zlink_msg_t> *parts_,
                                zlink_send_flags_t flags_)
{
    if (!socket_ || !parts_ || parts_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (socket_);
    socket_->set_all_pipes_nodelay ();
    return zlink::logical_multipart_send (socket_, &(*parts_)[0], parts_->size (),
                                          flags_);
}

int recv_combined_plain_message (zlink::socket_base_t *socket_,
                                 std::vector<zlink_msg_t> *out_)
{
    if (!socket_ || !out_) {
        errno = EFAULT;
        return -1;
    }

    out_->clear ();

    zlink_msg_t first;
    zlink_msg_init (&first);
    if (zlink::recv_msg_internal (socket_, &first, ZLINK_DONTWAIT) != 0) {
        zlink_msg_close (&first);
        return -1;
    }

    out_->push_back (first);
    while (zlink::internal_pair_queue::frame_has_more (out_->back ())) {
        zlink_msg_t next;
        zlink_msg_init (&next);
        if (zlink::internal_pair_queue::recv_followup_with_retry (
              socket_, &next, ZLINK_DONTWAIT)
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            out_->clear ();
            errno = saved_errno;
            return -1;
        }
        out_->push_back (next);
    }

    return 0;
}

int enqueue_spot_state_route_ingress (
  spot_request_reply_state_t *state_,
  zlink::spot_runtime_t *runtime_,
  std::vector<zlink_msg_t> *parts_,
  zlink_send_flags_t flags_)
{
    if (!state_ || !runtime_ || !parts_) {
        errno = EFAULT;
        return -1;
    }
    return enqueue_runtime_route_ingress_once (runtime_, parts_, flags_);
}

int enqueue_runtime_route_ingress_once (zlink::spot_runtime_t *runtime_,
                                        std::vector<zlink_msg_t> *parts_,
                                        zlink_send_flags_t flags_)
{
    if (!runtime_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    zlink::socket_base_t *socket = NULL;
    if (runtime_->ensure_sender_socket (
          zlink::spot_runtime_sender_route_ingress, &socket)
        != 0)
        return -1;

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (socket);
    socket->set_all_pipes_nodelay ();
    const long wait_timeout_ms = (flags_ & ZLINK_DONTWAIT) != 0 ? 0 : 100;
    if (zlink::wait_socket_events_internal (socket, ZLINK_POLLOUT, wait_timeout_ms)
        <= 0) {
        errno = errno != 0 ? errno : EAGAIN;
        return -1;
    }

    return send_combined_parts_locked (socket, parts_, flags_);
}

int enqueue_runtime_node_router_once (zlink::spot_runtime_t *runtime_,
                                      std::vector<zlink_msg_t> *parts_,
                                      zlink_send_flags_t flags_)
{
    if (!runtime_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    zlink::socket_base_t *socket = NULL;
    if (runtime_->ensure_sender_socket (
          zlink::spot_runtime_sender_node_router, &socket)
        != 0)
        return -1;

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (socket);
    socket->set_all_pipes_nodelay ();
    const long wait_timeout_ms = (flags_ & ZLINK_DONTWAIT) != 0 ? 0 : 100;
    if (zlink::wait_socket_events_internal (socket, ZLINK_POLLOUT, wait_timeout_ms)
        <= 0) {
        errno = errno != 0 ? errno : EAGAIN;
        return -1;
    }

    return send_combined_parts_locked (socket, parts_, flags_);
}

uint64_t allocate_request_seq (uint64_t *next_request_seq_,
                               const std::unordered_set<uint64_t> &pending_sequences_)
{
    if (!next_request_seq_) {
        errno = EFAULT;
        return 0;
    }

    const uint64_t start = *next_request_seq_ == 0 ? 1 : *next_request_seq_;
    uint64_t candidate = start;

    do {
        if (candidate == 0)
            candidate = 1;

        if (pending_sequences_.count (candidate) == 0) {
            uint64_t next = candidate + 1;
            if (next == 0)
                next = 1;
            *next_request_seq_ = next;
            return candidate;
        }

        ++candidate;
        if (candidate == 0)
            candidate = 1;
    } while (candidate != start);

    errno = EBUSY;
    return 0;
}

int start_spot_request_common (void *spot_,
                               uint8_t destination_class_,
                               const std::string &destination_node_rid_,
                               const std::string &destination_endpoint_rid_,
                               uint8_t pending_source_class_,
                               const std::string &pending_source_rid_,
                               const std::string &pending_source_spot_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               zlink_send_flags_t flags_,
                               uint32_t timeout_ms_,
                               zlink_reply_handler_fn handler_,
                               void *userdata_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return -1;

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    pending_spot_key_t key;
    uint64_t request_seq = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        request_seq =
          allocate_request_seq (&state->requests.next_request_seq,
                                state->requests.pending_sequences);
        if (request_seq == 0)
            return -1;

        key.source_class = pending_source_class_;
        key.source_rid = pending_source_rid_;
        key.source_spot_rid = pending_source_spot_rid_;
        key.request_seq = request_seq;
    }
    if (register_spot_pending_request (state, key, timeout_ms_, handler_,
                                       userdata_)
        != 0)
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          destination_class_, destination_node_rid_, destination_endpoint_rid_,
          zlink::request_reply::request_type, key.request_seq, parts_,
          part_count_, &combined)
        != 0) {
        erase_spot_pending_request (state, key);
        return -1;
    }

    const bool local_target = has_local_spot_route_target (
      destination_class_, destination_node_rid_, destination_endpoint_rid_);
    int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
      spot ? spot->node : NULL, routed_spot_delivery_request, local_target,
      destination_class_ == zmp_router_class ? destination_endpoint_rid_
                                             : std::string (),
      flags_,
      &combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        erase_spot_pending_request (state, key);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    return 0;
}

int start_spot_request_to_spot (void *spot_,
                                const zlink_routing_id_t *dest_node_rid_,
                                const zlink_routing_id_t *dest_spot_rid_,
                                zlink_msg_t *parts_,
                                size_t part_count_,
                                zlink_send_flags_t flags_,
                                uint32_t timeout_ms_,
                                zlink_reply_handler_fn handler_,
                                void *userdata_)
{
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || !handler_) {
        errno = EINVAL;
        return -1;
    }
    if (!spot_destination_is_admitted (spot_, dest_node_rid_)) {
        errno = ECONNREFUSED;
        return -1;
    }

    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);
    return start_spot_request_common (
      spot_, zmp_spot_class, destination_node_rid, destination_spot_rid,
      zmp_spot_class, destination_node_rid, destination_spot_rid, parts_,
      part_count_, flags_, timeout_ms_, handler_, userdata_);
}

int start_spot_request_to_router (void *spot_,
                                  const zlink_routing_id_t *peer_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_send_flags_t flags_,
                                  uint32_t timeout_ms_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_)
{
    if (!handler_ || !has_valid_routing_id (peer_rid_)) {
        errno = EINVAL;
        return -1;
    }

    const std::string peer_rid = routing_id_key (peer_rid_);
    return start_spot_request_common (
      spot_, zmp_router_class, std::string (), peer_rid, zmp_router_class,
      peer_rid, std::string (), parts_,
      part_count_, flags_, timeout_ms_, handler_, userdata_);
}

int start_router_request_to_spot (void *router_,
                                  const zlink_routing_id_t *dest_node_rid_,
                                  const zlink_routing_id_t *dest_spot_rid_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  zlink_send_flags_t flags_,
                                  uint32_t timeout_ms_,
                                  zlink_reply_handler_fn handler_,
                                  void *userdata_)
{
    if (!handler_ || !has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return -1;
    }

    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;
    const std::string router_rid_key = routing_id_key (&router_rid);

    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_or_create_router_state (router_);
    bind_router_state_rid (router_, router_rid_key, state);

    uint64_t request_seq = 0;
    pending_spot_key_t key;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        request_seq =
          allocate_request_seq (&state->requests.next_request_seq,
                                state->requests.pending_sequences);
        if (request_seq == 0)
            return -1;

        key.source_class = zmp_spot_class;
        key.source_rid = destination_node_rid;
        key.source_spot_rid = destination_spot_rid;
        key.request_seq = request_seq;
    }
    if (register_router_spot_pending_request (state, request_seq, key,
                                              timeout_ms_, handler_, userdata_)
        != 0)
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_router_class, std::string (), router_rid_key, zmp_spot_class,
          destination_node_rid, destination_spot_rid,
          zlink::request_reply::request_type, request_seq, parts_, part_count_,
          &combined)
        != 0) {
        pending_reply_t pending;
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            state->requests.pending_sequences.erase (request_seq);
            std::unordered_map<uint64_t, pending_reply_t>::iterator it =
              state->requests.pending_replies.find (request_seq);
            if (it != state->requests.pending_replies.end ()) {
                pending = it->second;
                state->requests.pending_replies.erase (it);
            }
        }
        zlink::request_timeout::cancel (pending.timeout_task);
        return -1;
    }

    int rc = zlink::spot_reqrep_internal::dispatch_router_spot_delivery (
      destination_node_rid, destination_spot_rid,
      router_spot_delivery_request, flags_, &combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        pending_reply_t pending;
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            state->requests.pending_sequences.erase (request_seq);
            std::unordered_map<uint64_t, pending_reply_t>::iterator it =
              state->requests.pending_replies.find (request_seq);
            if (it != state->requests.pending_replies.end ()) {
                pending = it->second;
                state->requests.pending_replies.erase (it);
            }
        }
        zlink::request_timeout::cancel (pending.timeout_task);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    return 0;
}

int prepare_staged_send_step (
  void *handle_,
  const zlink::part_helper_internal::send_sequence_spec_t &spec_,
  std::shared_ptr<zlink::part_helper_internal::handle_state_t> *state_out_,
  bool *first_part_out_)
{
    if (!state_out_ || !first_part_out_) {
        errno = EFAULT;
        return -1;
    }

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> state =
      zlink::part_helper_internal::find_or_create_handle_state (handle_);
    if (!state)
        return -1;

    std::unique_lock<std::mutex> lock (state->mutex);
    const std::thread::id current_thread = std::this_thread::get_id ();
    while (state->send.active && state->send.owner_thread != current_thread) {
        if (!zlink::part_helper_internal::aggregate_send_mode_active ()) {
            errno = EINVAL;
            return -1;
        }
        state->cv.wait (lock);
    }

    if (!state->send.active) {
        state->send.active = true;
        state->send.spec = spec_;
        state->send.sink_socket = NULL;
        state->send.owner_thread = current_thread;
        *first_part_out_ = true;
    } else {
        if (!zlink::part_helper_internal::send_spec_equals (
              state->send.spec, spec_)) {
            errno = EINVAL;
            return -1;
        }
        *first_part_out_ = false;
    }

    *state_out_ = state;
    return 0;
}

int stage_staged_send_part (
  zlink::part_helper_internal::handle_state_t *state_,
  zlink_msg_t *part_)
{
    if (!state_ || !part_) {
        errno = EFAULT;
        return -1;
    }

    state_->send.buffered_parts.resize (state_->send.buffered_parts.size () + 1);
    zlink_msg_t &slot = state_->send.buffered_parts.back ();
    zlink_msg_init (&slot);
    if (zlink_msg_move (&slot, part_) != 0) {
        zlink_msg_close (&slot);
        state_->send.buffered_parts.pop_back ();
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int move_staged_parts_for_submit (
  const std::shared_ptr<zlink::part_helper_internal::handle_state_t> &state_,
  zlink_msg_t *part_,
  std::vector<zlink_msg_t> *parts_out_)
{
    if (!state_ || !part_ || !parts_out_) {
        errno = EFAULT;
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        parts_out_->swap (state_->send.buffered_parts);
    }

    parts_out_->resize (parts_out_->size () + 1);
    zlink_msg_t &slot = parts_out_->back ();
    zlink_msg_init (&slot);
    if (zlink_msg_move (&slot, part_) != 0) {
        zlink_msg_close (&slot);
        parts_out_->pop_back ();
        errno = EFAULT;
        return -1;
    }

    return 0;
}

}

bool in_spot_dispatch_event_callback (void *spot_)
{
    return spot_ != NULL
           && zlink::spot_dispatch_event_callback_context_t::current_handle ()
                == spot_;
}

int spot_dispatch_queue_subscribe_message (
  void *spot_,
  const zlink_routing_id_t *source_rid_,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      try_find_spot_state (spot_);
    if (!state) {
        close_spot_dispatch_parts (parts_, part_count_);
        errno = 0;
        return 0;
    }

    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (!state->dispatch.handler) {
            close_spot_dispatch_parts (parts_, part_count_);
            errno = 0;
            return 0;
        }
    }

    return queue_spot_subscribe_message (state.get (), source_rid_, topic_,
                                         topic_len_, parts_, part_count_);
}

int spot_dispatch_subscribe_recv_internal (
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t **parts_out_,
  size_t *part_count_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      try_find_spot_state (spot_);
    if (!state) {
        errno = EFAULT;
        return -1;
    }

    if (!in_spot_dispatch_event_callback (spot_)) {
        errno = EBUSY;
        return -1;
    }

    if (validate_recv_flags (flags_) != 0)
        return -1;

    return recv_internal_spot_subscribe_queue (&state->recv.subscribe_queue,
                                               source_rid_out_, parts_out_,
                                               part_count_out_, topic_id_out_,
                                               topic_id_len_out_, flags_);
}

extern "C" int zlink_spot_process_route_ingress (void *node_, void *socket_)
{
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (socket_);
    if (!socket) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::vector<zlink_msg_t> combined;
        if (recv_combined_router_message (socket, &combined) != 0) {
            if (spot_direct_route_debug_enabled () && errno == EAGAIN) {
                std::vector<std::string> remote_endpoints;
                socket->socket_peer_remote_endpoints (&remote_endpoints);
                if (!remote_endpoints.empty ()) {
                    static std::atomic<int> g_ingress_eagain_logs (0);
                    if (g_ingress_eagain_logs.fetch_add (
                          1, std::memory_order_acq_rel)
                        < 32) {
                        std::fprintf (
                          stderr,
                          "[spot-direct] ingress recv eagain socket=%d peer=%s\n",
                          socket->socket_id (),
                          remote_endpoints.front ().c_str ());
                    }
                }
            }
            if (spot_direct_route_debug_enabled () && errno != EAGAIN) {
                std::fprintf (stderr,
                              "[spot-direct] ingress recv failed errno=%d socket=%d\n",
                              errno,
                              socket->socket_id ());
            }
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        if (spot_direct_route_debug_enabled ()) {
            std::fprintf (stderr,
                          "[spot-direct] ingress recv parts=%zu socket=%d\n",
                          combined.size (),
                          socket->socket_id ());
        }

        parsed_spot_envelope_t spot_envelope;
        int rc = -1;
        if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (
              &combined[0], combined.size (), &spot_envelope)) {
            const int saved_errno = errno != 0 ? errno : EPROTO;
            if (spot_direct_route_debug_enabled ()) {
                std::fprintf (
                  stderr,
                  "[spot-direct] ingress parse failed errno=%d parts=%zu socket=%d\n",
                  saved_errno,
                  combined.size (),
                  socket->socket_id ());
            }
            zlink::request_reply::close_built_parts (&combined);
            errno = saved_errno;
            return -1;
        }

        if (should_process_spot_routed_locally (
              static_cast<zlink::spot_node_t *> (node_), spot_envelope)) {
            rc = process_parsed_route_combined_for_local_delivery (&combined,
                                                                   spot_envelope);
        } else {
            if (spot_direct_route_debug_enabled ()) {
                std::string local_node_rid;
                (void) resolve_spot_node_routing_id (
                  static_cast<zlink::spot_node_t *> (node_), &local_node_rid);
                std::fprintf (
                  stderr,
                  "[spot-direct] drop non-local routed envelope on routed ingress node=%s dest_node=%s dest_endpoint=%s\n",
                  local_node_rid.c_str (),
                  spot_envelope.destination_node_rid.c_str (),
                  spot_envelope.destination_endpoint_rid.c_str ());
            }
            rc = 0;
        }

        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        if (rc != 0) {
            errno = saved_errno;
            return -1;
        }
    }
}

extern "C" int zlink_spot_process_peer_route_ingress (void *node_, void *socket_)
{
    return zlink_spot_process_route_ingress (node_, socket_);
}

extern "C" int zlink_spot_process_node_router (void *node_, void *socket_)
{
    return zlink_spot_process_route_ingress (node_, socket_);
}

zlink_submit_result_t spot_send_channel_impl (void *spot_,
                                              const char *channel_name_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!channel_name_ || channel_name_[0] == '\0') {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::socket_base_t *router =
      zlink::spot_node_access_t::select_service_router (spot->node,
                                                        channel_name_);
    if (!router)
        return zlink::submit_result_internal::from_errno (errno);
    return zlink::submit_result_internal::from_rc (
      zlink_socket_send_internal (router, parts_, part_count_, flags_));
}


zlink_submit_result_t spot_request_channel_impl (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!channel_name_ || channel_name_[0] == '\0') {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    zlink::socket_base_t *router =
      zlink::spot_node_access_t::select_service_router (spot->node,
                                                        channel_name_);
    if (!router)
        return zlink::submit_result_internal::from_errno (errno);

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->completion_state.channel_reply_sources.count (router) == 0) {
            state->completion_state.channel_reply_sources[router] =
              std::shared_ptr<zlink::spot_reqrep_internal::spot_channel_reply_source_t> (
                new zlink::spot_reqrep_internal::spot_channel_reply_source_t (
                  router));
        }
        ++state->completion_state.pending_channel_requests;
    }

    std::shared_ptr<reqrep::socket_request_reply_state_t> socket_state =
      reqrep::find_or_create_request_reply_state (make_socket_handle (router));
    reqrep::register_spot_channel_dispatch_observer (socket_state, spot_);

    std::unique_ptr<channel_reply_bridge_ctx_t> bridge (
      new (std::nothrow) channel_reply_bridge_ctx_t ());
    if (!bridge.get ()) {
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            if (state->completion_state.pending_channel_requests > 0)
                --state->completion_state.pending_channel_requests;
        }
        errno = ENOMEM;
        return zlink::submit_result_internal::from_errno (errno);
    }
    bridge->state = state;
    bridge->dealer = router;
    bridge->handler = handler_;
    bridge->userdata = userdata_;

    const int rc = reqrep::start_request (
      make_socket_handle (router), NULL, parts_, part_count_, flags_,
      timeout_ms_, &channel_reply_bridge_completion, bridge.release ());
    if (rc != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->completion_state.pending_channel_requests > 0)
            --state->completion_state.pending_channel_requests;
    }
    return zlink::submit_result_internal::from_request_submit_rc (rc);
}

zlink_submit_result_t spot_request_spot_impl (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    return zlink::submit_result_internal::from_request_submit_rc (
      start_spot_request_to_spot (spot_, dest_node_rid_, dest_spot_rid_, parts_,
                                  part_count_, flags_, timeout_ms_, handler_,
                                  userdata_));
}


zlink_submit_result_t spot_request_router_impl (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return ZLINK_SUBMIT_INVALID_HANDLE;
    }

    return zlink::submit_result_internal::from_request_submit_rc (
      start_spot_request_to_router (spot_, peer_rid_, parts_, part_count_,
                                    flags_, timeout_ms_, handler_, userdata_));
}



zlink_submit_result_t spot_reply_spot_impl (void *spot_,
                                            const zlink_routing_id_t *dest_node_rid_,
                                            const zlink_routing_id_t *dest_spot_rid_,
                                            uint64_t request_seq_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || request_seq_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          zmp_spot_class, destination_node_rid, destination_spot_rid,
          zlink::request_reply::reply_type,
          request_seq_, parts_, part_count_, &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);
    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    const bool local_target = has_local_spot_route_target (
      zmp_spot_class, destination_node_rid, destination_spot_rid);
    int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
      spot ? spot->node : NULL, routed_spot_delivery_reply, local_target,
      std::string (), ZLINK_DONTWAIT, &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

zlink_submit_result_t spot_send_spot_impl (void *spot_,
                                           const zlink_routing_id_t *dest_node_rid_,
                                           const zlink_routing_id_t *dest_spot_rid_,
                                           zlink_msg_t *parts_,
                                           size_t part_count_,
                                           zlink_send_flags_t flags_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_)
        || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          zmp_spot_class, destination_node_rid, destination_spot_rid, parts_,
          part_count_, &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);

    const bool local_target = has_local_spot_route_target (
      zmp_spot_class, destination_node_rid, destination_spot_rid);
    int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
      spot ? spot->node : NULL, routed_spot_delivery_direct, local_target,
      destination_spot_rid, flags_, &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}



zlink_submit_result_t spot_reply_router_impl (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (spot_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return zlink::submit_result_internal::from_errno (errno);

    spot_handle_t *spot = as_spot_handle (spot_);
    const std::string peer_rid = routing_id_key (peer_rid_);
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          zmp_router_class, std::string (), peer_rid,
          zlink::request_reply::reply_type, request_seq_, parts_, part_count_,
          &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);
    const bool local_target = has_local_spot_route_target (
      zmp_router_class, std::string (), peer_rid);
    int rc = zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
      spot ? spot->node : NULL, routed_spot_delivery_reply, local_target,
      peer_rid, ZLINK_DONTWAIT, &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}


zlink_handler_result_t zlink_spot_handler (void *spot_,
                                           zlink_spot_handler_fn handler_,
                                           void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return zlink::handler_result_internal::from_rc (-1);

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->recv.request_handler || state->dispatch.handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return ZLINK_HANDLER_BUSY;
    }

    state->recv.request_handler = handler_;
    state->recv.request_handler_userdata = userdata_;
    return ZLINK_HANDLER_OK;
}

zlink_handler_result_t zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return ZLINK_HANDLER_INVALID_ARGUMENT;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return zlink::handler_result_internal::from_rc (-1);

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        if (state->recv.request_handler || state->dispatch.handler) {
            spot_revert_callback_transition (as_spot_handle (spot_));
            errno = EBUSY;
            return ZLINK_HANDLER_BUSY;
        }

        state->dispatch.handler = handler_;
        state->dispatch.handler_userdata = userdata_;
        if (install_spot_dispatch_event_task (state.get ()) != 0) {
            state->dispatch.handler = NULL;
            state->dispatch.handler_userdata = NULL;
            spot_revert_callback_transition (as_spot_handle (spot_));
            return zlink::handler_result_internal::from_rc (-1);
        }
    }

    if (spot_install_dispatch_event_sub_handler (as_spot_handle (spot_)) != 0) {
        zlink::service_control_runtime_t *dispatch_runtime = NULL;
        uint64_t dispatch_task_id = 0;
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            state->dispatch.handler = NULL;
            state->dispatch.handler_userdata = NULL;
            dispatch_runtime = state->dispatch.runtime;
            dispatch_task_id = state->dispatch.task_id;
            state->dispatch.runtime = NULL;
            state->dispatch.task_id = 0;
        }
        if (dispatch_runtime && dispatch_task_id != 0)
            (void) dispatch_runtime->remove_task (dispatch_task_id);
        spot_revert_callback_transition (as_spot_handle (spot_));
        return zlink::handler_result_internal::from_rc (-1);
    }
    return ZLINK_HANDLER_OK;
}

zlink_recv_result_t spot_recv_impl (void *spot_,
                                    const zlink_routing_id_t **source_rid_out_,
                                    const zlink_routing_id_t **spot_rid_out_,
                                    uint64_t *request_seq_out_,
                                    zlink_msg_t **parts_out_,
                                    size_t *part_count_out_,
                                    zlink_recv_flags_t flags_)
{
    if (!source_rid_out_ || !spot_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (validate_recv_flags (flags_) != 0)
        return ZLINK_RECV_NOT_SUPPORTED;
    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return ZLINK_RECV_INVALID_HANDLE;
    }
    if (spot_require_recv_model (as_spot_handle (spot_)) != 0)
        return ZLINK_RECV_BUSY;

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    if (!state)
        return zlink::recv_result_internal::from_errno (errno);
    if (ensure_spot_recv_ready (state) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->recv.request_handler
        || (state->dispatch.handler
            && !in_spot_dispatch_event_callback (spot_))) {
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }
    lock.unlock ();

    const zlink_recv_flags_t try_flags =
      static_cast<zlink_recv_flags_t> (flags_ | ZLINK_DONTWAIT);
    const bool blocking = (flags_ & ZLINK_DONTWAIT) == 0;
    while (true) {
        (void) drain_spot_reply_completions (state, spot_);
        const int recv_rc = recv_internal_spot_queue (
          state.get (), source_rid_out_, spot_rid_out_, request_seq_out_,
          parts_out_, part_count_out_, try_flags);
        if (recv_rc == 0)
            return ZLINK_RECV_OK;
        if (!blocking || errno != EAGAIN)
            return zlink::recv_result_internal::from_rc (recv_rc);

        bool input_ready = false;
        bool signal_ready = false;
        const int wait_rc = zlink::request_completion::wait_input_or_signal (
          zlink::spot_reqrep_internal::spot_routed_recv_socket (state),
          spot_completion_signal_socket (state),
          -1,
          &input_ready, &signal_ready);
        if (wait_rc <= 0) {
            if (wait_rc == 0)
                errno = EAGAIN;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (signal_ready)
            (void) drain_spot_reply_completions (state, spot_);
    }
}

zlink_recv_result_t zlink_spot_recv_part (
  void *spot_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  zlink_part_flag_t *has_more_out_,
  zlink_recv_flags_t flags_)
{
    if (!spot_ || !source_node_rid_out_ || !source_spot_rid_out_
        || !request_seq_out_ || !part_out_ || !has_more_out_) {
        errno = EFAULT;
        return zlink::recv_result_internal::from_errno (errno);
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::recv_result_internal::from_errno (errno);
    if (!as_spot_handle (spot_))
        return zlink::recv_result_internal::from_errno (EFAULT);

    std::shared_ptr<zlink::part_helper_internal::handle_state_t> helper_state =
      zlink::part_helper_internal::find_or_create_handle_state (spot_);
    if (!helper_state)
        return zlink::recv_result_internal::from_errno (errno);

    bool first_part = false;
    zlink::socket_base_t *source_socket = NULL;
    if (zlink::part_helper_internal::prepare_recv_step (
          spot_, zlink::part_helper_internal::recv_family_spot, source_socket,
          &helper_state, &first_part, &source_socket)
        != 0) {
        return zlink::recv_result_internal::from_errno (errno);
    }

    if (first_part) {
        const zlink_routing_id_t *source_node_rid = NULL;
        const zlink_routing_id_t *source_spot_rid = NULL;
        uint64_t request_seq = 0;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        if (spot_recv_impl (spot_, &source_node_rid, &source_spot_rid,
                            &request_seq, &parts, &part_count, flags_)
            != ZLINK_RECV_OK) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            return zlink::recv_result_internal::from_errno (errno);
        }

        bool move_failed = false;
        {
            std::lock_guard<std::mutex> lock (helper_state->mutex);
            helper_state->recv.return_source_rid_as_null = source_node_rid == NULL;
            helper_state->recv.return_source_spot_rid_as_null =
              source_spot_rid == NULL;
            zlink::part_helper_internal::copy_routing_id (
              source_node_rid, &helper_state->recv.source_node_rid);
            zlink::part_helper_internal::copy_routing_id (
              source_spot_rid, &helper_state->recv.source_spot_rid);
            helper_state->recv.request_seq = request_seq;
            helper_state->recv.buffered_parts.resize (part_count);
            helper_state->recv.next_part_index = 0;
            for (size_t i = 0; i < part_count; ++i) {
                zlink_msg_init (&helper_state->recv.buffered_parts[i]);
                if (zlink_msg_move (&helper_state->recv.buffered_parts[i],
                                    &parts[i])
                    != 0) {
                    move_failed = true;
                    break;
                }
            }
        }
        zlink_multipart_close (parts, part_count);
        if (move_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (helper_state->recv.buffered_parts.empty ()) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (zlink_msg_move (part_out_, &helper_state->recv.buffered_parts[0]) != 0) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
        helper_state->recv.next_part_index = 1;
    } else {
        bool range_failed = false;
        bool move_failed = false;
        {
            std::lock_guard<std::mutex> lock (helper_state->mutex);
            if (helper_state->recv.next_part_index
                >= helper_state->recv.buffered_parts.size ()) {
                range_failed = true;
            } else {
                move_failed =
                  zlink_msg_move (
                    part_out_,
                    &helper_state
                       ->recv.buffered_parts[helper_state->recv.next_part_index])
                  != 0;
                if (!move_failed)
                    ++helper_state->recv.next_part_index;
            }
        }
        if (range_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EPROTO;
            return zlink::recv_result_internal::from_errno (errno);
        }
        if (move_failed) {
            zlink::part_helper_internal::abort_recv_step (helper_state);
            errno = EFAULT;
            return zlink::recv_result_internal::from_errno (errno);
        }
    }

    {
        std::lock_guard<std::mutex> lock (helper_state->mutex);
        *source_node_rid_out_ =
          helper_state->recv.return_source_rid_as_null
            ? NULL
            : &helper_state->recv.source_node_rid;
        *source_spot_rid_out_ =
          helper_state->recv.return_source_spot_rid_as_null
            ? NULL
            : &helper_state->recv.source_spot_rid;
        *request_seq_out_ = helper_state->recv.request_seq;
        *has_more_out_ =
          (helper_state->recv.next_part_index
           < helper_state->recv.buffered_parts.size ())
            ? ZLINK_PART_MORE : ZLINK_PART_FINAL;
    }
    zlink::part_helper_internal::complete_recv_step (helper_state,
                                                     *has_more_out_);
    return ZLINK_RECV_OK;
}

zlink_submit_result_t router_request_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (router_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    return zlink::submit_result_internal::from_request_submit_rc (
      start_router_request_to_spot (router_, dest_node_rid_, dest_spot_rid_,
                                    parts_, part_count_, flags_, timeout_ms_,
                                    handler_, userdata_));
}


zlink_submit_result_t router_reply_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (router_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)
        || request_seq_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return zlink::submit_result_internal::from_errno (errno);
    const std::string router_rid_key = routing_id_key (&router_rid);

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_router_class, std::string (), router_rid_key,
          zmp_spot_class, destination_node_rid, destination_spot_rid,
          zlink::request_reply::reply_type, request_seq_, parts_, part_count_,
          &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);
    int rc = zlink::spot_reqrep_internal::dispatch_router_spot_delivery (
      destination_node_rid, destination_spot_rid, router_spot_delivery_reply,
      ZLINK_DONTWAIT, &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}


zlink_submit_result_t router_send_spot_impl (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
    if (zlink::part_helper_internal::reject_if_send_sequence_open (router_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const std::string destination_node_rid = routing_id_key (dest_node_rid_);
    const std::string destination_spot_rid = routing_id_key (dest_spot_rid_);

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return zlink::submit_result_internal::from_errno (errno);
    const std::string router_rid_key = routing_id_key (&router_rid);

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (zmp_router_class, std::string (),
                                   router_rid_key,
                                   zmp_spot_class, destination_node_rid,
                                   destination_spot_rid, parts_, part_count_,
                                   &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);
    int rc = zlink::spot_reqrep_internal::dispatch_router_spot_delivery (
      destination_node_rid, destination_spot_rid, router_spot_delivery_direct,
      flags_, &combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}


extern "C" int zlink_router_enable_spot_receive (void *router_)
{
    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0) {
        errno = 0;
        return 0;
    }

    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_or_create_router_state (router_);
    bind_router_state_rid (router_, routing_id_key (&router_rid), state);
    errno = 0;
    return 0;
}

extern "C" int zlink_spot_request_reply_set_default_timeout (
  void *spot_,
  const void *optval_,
  size_t optvallen_)
{
    if (!as_spot_handle (spot_)) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || optvallen_ != sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    int timeout_ms = 0;
    memcpy (&timeout_ms, optval_, sizeof (timeout_ms));
    if (timeout_ms < 0) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    state->requests.default_timeout_ms = static_cast<uint32_t> (timeout_ms);
    return 0;
}

extern "C" int zlink_spot_request_reply_get_default_timeout (
  void *spot_,
  void *optval_,
  size_t *optvallen_)
{
    if (!as_spot_handle (spot_)) {
        errno = EINVAL;
        return -1;
    }
    if (!optval_ || !optvallen_ || *optvallen_ < sizeof (int)) {
        errno = EINVAL;
        return -1;
    }

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    int timeout_ms = 0;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        timeout_ms = static_cast<int> (state->requests.default_timeout_ms);
    }

    memcpy (optval_, &timeout_ms, sizeof (timeout_ms));
    *optvallen_ = sizeof (timeout_ms);
    return 0;
}

extern "C" void zlink_spot_request_reply_cleanup_spot (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return;

    zlink::service_control_runtime_t *dispatch_runtime = NULL;
    uint64_t dispatch_task_id = 0;
    std::shared_ptr<spot_request_reply_state_t> state =
      try_find_spot_state (spot);
    zlink::internal_pair_queue::queue_t routed_recv_signal;
    if (state)
        (void) zlink::spot_reqrep_internal::drain_close_spot_request_reply_state (
          spot_);
    if (state) {
        {
            std::lock_guard<std::mutex> dispatch_lock (
              state->dispatch.mutex);
            state->dispatch.subscribe_pending.clear ();
            state->dispatch.routed_pending.clear ();
            state->dispatch.channel_reply_pending.clear ();
            state->dispatch.timer_pending.clear ();
            state->dispatch.queued_keys.clear ();
            state->dispatch.rearm_keys.clear ();
            state->dispatch.active_info_valid = false;
            state->dispatch.running = false;
        }

        std::lock_guard<std::mutex> state_lock (state->mutex);
        state->recv.request_handler = NULL;
        state->recv.request_handler_userdata = NULL;
        state->dispatch.handler = NULL;
        state->dispatch.handler_userdata = NULL;
        dispatch_runtime = state->dispatch.runtime;
        dispatch_task_id = state->dispatch.task_id;
        state->dispatch.runtime = NULL;
        state->dispatch.task_id = 0;
    }
    zlink::spot_reqrep_internal::close_spot_routed_recv_state (
      state, &routed_recv_signal);
    if (routed_recv_signal.rx || routed_recv_signal.tx) {
        if (routed_recv_signal.rx)
            zlink::spot_node_access_t::untrack_owned_socket (
              spot->node, routed_recv_signal.rx);
        if (routed_recv_signal.tx)
            zlink::spot_node_access_t::untrack_owned_socket (
              spot->node, routed_recv_signal.tx);
        zlink::internal_pair_queue::close (&routed_recv_signal);
    }
    if (dispatch_runtime && dispatch_task_id != 0)
        (void) dispatch_runtime->remove_task (dispatch_task_id);
    if (state) {
        zlink::spot_reqrep_internal::unregister_spot_channel_reply_observers (
          state);
        std::vector<std::shared_ptr<
          zlink::spot_reqrep_internal::spot_channel_reply_source_t> > sources;
        zlink::spot_reqrep_internal::clear_spot_channel_reply_sources (state,
                                                                       &sources);
        std::lock_guard<std::mutex> state_lock (state->mutex);
        close_spot_subscribe_dispatch_queue (&state->recv.subscribe_queue);
        zlink::request_completion::close (&state->completion_state.direct);
        for (size_t i = 0; i < sources.size (); ++i) {
            if (sources[i])
                zlink::request_completion::close (&sources[i]->completion);
        }
    }
    erase_spot_owner_state (spot_);
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    for (spot_state_identity_index_t::iterator it =
           g_spot_state_identity_index.begin ();
         it != g_spot_state_identity_index.end ();) {
        spot_state_spot_index_t &spot_index = it->second;
        for (spot_state_spot_index_t::iterator spot_it = spot_index.begin ();
             spot_it != spot_index.end ();) {
            std::shared_ptr<spot_request_reply_state_t> indexed =
              spot_it->second.lock ();
            if (!indexed || indexed == state)
                spot_it = spot_index.erase (spot_it);
            else
                ++spot_it;
        }
        if (spot_index.empty ())
            it = g_spot_state_identity_index.erase (it);
        else
            ++it;
    }
}

extern "C" void zlink_spot_request_reply_cleanup_router (void *router_)
{
    const socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return;

    std::shared_ptr<router_spot_request_reply_state_t> state =
      std::static_pointer_cast<router_spot_request_reply_state_t> (
        handle.socket->router_spot_request_reply_state ());
    if (state)
        (void)
          zlink::spot_reqrep_internal::drain_close_router_spot_request_reply_state (
            router_);
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        zlink::request_completion::close (&state->completion);
    }
    handle.socket->clear_router_spot_request_reply_state ();
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    for (router_state_identity_index_t::iterator it =
           g_router_state_identity_index.begin ();
         it != g_router_state_identity_index.end ();) {
        std::shared_ptr<router_spot_request_reply_state_t> indexed =
          it->second.lock ();
        if (!indexed || indexed == state)
            it = g_router_state_identity_index.erase (it);
        else
            ++it;
    }
}
