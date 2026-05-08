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
#include "api/service_spot_routed_protocol_internal.hpp"
#include "api/service_spot_request_reply_utils_internal.hpp"
#include "api/socket_message_api_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/status_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "api/handler_result_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "utils/debug_log.hpp"
#include "utils/random.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;

const bool spot_direct_route_debug_on =
  zlink::debug_env_enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE");

bool spot_direct_route_debug_enabled ()
{
    return spot_direct_route_debug_on;
}

using zlink::spot_reqrep_internal::spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::spot_state_identity_index;
using zlink::spot_reqrep_internal::router_state_identity_index;
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

const size_t spot_routed_control_part_count = 8;

int recv_combined_plain_message (zlink::socket_base_t *socket_,
                                 std::vector<zlink_msg_t> *out_);

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
    while (zlink::msg_frame_has_more (out_->back ())) {
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
