/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "api/request_timeout_scheduler_internal.hpp"
#include "api/internal_pair_queue_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"
#include "api/service_api_internal.hpp"
#include "api/service_spot_dispatch_context_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/socket_request_reply_internal.hpp"
#include "api/status_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "api/handler_result_internal.hpp"
#include "api/recv_result_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/ctx.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_pub.hpp"
#include "utils/random.hpp"

namespace
{
namespace reqrep = zlink::socket_reqrep_internal;

using zlink::spot_reqrep_internal::g_spot_recv_source_rid;
using zlink::spot_reqrep_internal::g_spot_recv_spot_rid;
using zlink::spot_reqrep_internal::g_spot_request_reply_index_mutex;
using zlink::spot_reqrep_internal::g_spot_state_identity_index;
using zlink::spot_reqrep_internal::g_router_state_identity_index;
using zlink::spot_reqrep_internal::init_buffer_frame;
using zlink::spot_reqrep_internal::make_spot_identity_key;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::pending_reply_t;
using zlink::spot_reqrep_internal::pending_spot_key_t;
using zlink::spot_reqrep_internal::router_spot_request_reply_state_t;
using zlink::spot_reqrep_internal::router_state_identity_index_t;
using zlink::spot_reqrep_internal::spot_request_reply_state_t;
using zlink::spot_reqrep_internal::spot_state_identity_index_t;
using zlink::spot_reqrep_internal::bind_router_state_rid;
using zlink::spot_reqrep_internal::erase_spot_owner_state;
using zlink::spot_reqrep_internal::find_or_create_router_state;
using zlink::spot_reqrep_internal::find_or_create_spot_state;
using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::install_spot_dispatch_event_task;
using zlink::spot_reqrep_internal::maybe_dispatch_spot_event;
using zlink::spot_reqrep_internal::close_spot_dispatch_parts;
using zlink::spot_reqrep_internal::queue_spot_message;
using zlink::spot_reqrep_internal::queue_spot_subscribe_message;
using zlink::spot_reqrep_internal::recv_internal_spot_queue;
using zlink::spot_reqrep_internal::recv_internal_spot_subscribe_queue;
using zlink::spot_reqrep_internal::recv_combined_router_message;
using zlink::spot_reqrep_internal::build_spot_request_reply_message;
using zlink::spot_reqrep_internal::build_spot_routed_message;
using zlink::spot_reqrep_internal::dispatch_local_reply;
using zlink::spot_reqrep_internal::dispatch_local_request;
using zlink::spot_reqrep_internal::dispatch_local_built_message;
using zlink::spot_reqrep_internal::process_route_combined_for_local_delivery;
using zlink::spot_reqrep_internal::register_router_spot_pending_request;
using zlink::spot_reqrep_internal::register_spot_pending_request;
using zlink::spot_reqrep_internal::resolve_runtime_for_spot_destination;
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

struct routing_pair_t
{
    std::string node_rid;
    std::string spot_rid;
};

int enqueue_runtime_route_ingress_once (zlink::spot_runtime_t *runtime_,
                                        std::vector<zlink_msg_t> *parts_,
                                        zlink_send_flags_t flags_);

int validate_request_send_flags (zlink_send_flags_t flags_)
{
    if (flags_ != 0 && flags_ != ZLINK_DONTWAIT) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

zlink::ctx_t *resolve_spot_ctx (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::ctx (spot->node);
}

zlink::spot_runtime_t *resolve_spot_runtime (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot || !spot->node) {
        errno = EFAULT;
        return NULL;
    }
    return zlink::spot_node_access_t::runtime (spot->node);
}

zlink::spot_runtime_t *resolve_active_spot_runtime (void *spot_)
{
    zlink::spot_runtime_t *runtime = resolve_spot_runtime (spot_);
    if (!runtime || !runtime->execution.data_plane_running
        || !runtime->route_ingress
        || !runtime->node_router)
        return NULL;
    return runtime;
}

bool has_valid_routing_id (const zlink_routing_id_t *peer_rid_)
{
    return peer_rid_ && peer_rid_->size > 0
           && peer_rid_->size <= sizeof (peer_rid_->data);
}

bool spot_destination_is_admitted (void *spot_,
                                   const zlink_routing_id_t *dest_node_rid_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    return !spot || !spot->node || spot->node->peer_is_admitted (dest_node_rid_);
}

void notify_spot_dispatch_event (void *spot_,
                                 zlink_spot_dispatch_event_t event_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      try_find_spot_state (spot_);
    if (!state)
        return;
    maybe_dispatch_spot_event (state.get (), event_);
}

extern "C" void zlink_spot_notify_dispatch_event (
  void *spot_,
  zlink_spot_dispatch_event_t event_)
{
    notify_spot_dispatch_event (spot_, event_);
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
        handler = state_->request_handler;
        handler_userdata = state_->request_handler_userdata;
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

std::string routing_id_key (const zlink_routing_id_t *peer_rid_)
{
    if (!has_valid_routing_id (peer_rid_))
        return std::string ();

    return std::string (reinterpret_cast<const char *> (peer_rid_->data),
                        peer_rid_->size);
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

bool parse_spot_routed_envelope (zlink_msg_t *parts_,
                                 size_t part_count_,
                                 parsed_spot_envelope_t *out_)
{
    if (!parts_ || !out_ || part_count_ < spot_routed_control_part_count)
        return false;

    zlink::msg_t *protocol_id =
      reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (!protocol_id->check ()
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[0], zmp_spot_routed_protocol_id)
        || !zlink::request_reply::frame_is_single_byte_value (
          &parts_[1], zmp_protocol_version)) {
        return false;
    }

    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[2],
                                                              zmp_router_class)) {
        return false;
    }
    if (!zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                           zmp_spot_class)
        && !zlink::request_reply::frame_is_single_byte_value (&parts_[5],
                                                              zmp_router_class)) {
        return false;
    }

    out_->source_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[2]))[0];
    out_->source_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[3])),
      zlink_msg_size (&parts_[3]));
    out_->source_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[4])),
      zlink_msg_size (&parts_[4]));
    out_->destination_class =
      static_cast<const unsigned char *> (zlink_msg_data (&parts_[5]))[0];
    out_->destination_node_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[6])),
      zlink_msg_size (&parts_[6]));
    out_->destination_endpoint_rid.assign (
      static_cast<const char *> (zlink_msg_data (&parts_[7])),
      zlink_msg_size (&parts_[7]));
    out_->payload_parts = parts_ + spot_routed_control_part_count;
    out_->payload_part_count = part_count_ - spot_routed_control_part_count;
    return true;
}

bool resolve_spot_identity (void *spot_, routing_pair_t *out_)
{
    if (!out_) {
        errno = EFAULT;
        return false;
    }

    if (spot_handle_t *spot = as_spot_handle (spot_)) {
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return false;

        zlink::spot_pub_t *spot_pub = ensure_spot_pub (spot);
        zlink::spot_pub_t *node_pub =
          spot->node ? spot->node->ensure_default_pub () : NULL;
        if (!spot_pub || !node_pub)
            return false;

        zlink_routing_id_t node_rid;
        zlink_routing_id_t spot_rid;
        memset (&node_rid, 0, sizeof (node_rid));
        memset (&spot_rid, 0, sizeof (spot_rid));
        if (node_pub->routing_id (&node_rid) != 0
            || spot_pub->routing_id (&spot_rid) != 0) {
            return false;
        }

        out_->node_rid = routing_id_key (&node_rid);
        out_->spot_rid = routing_id_key (&spot_rid);
        return !out_->node_rid.empty () && !out_->spot_rid.empty ();
    }

    errno = EFAULT;
    return false;
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
                               const std::set<uint64_t> &pending_sequences_)
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
          allocate_request_seq (&state->next_request_seq, state->pending_sequences);
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

    zlink::spot_runtime_t *runtime =
      destination_class_ == zmp_spot_class
        ? resolve_runtime_for_spot_destination (destination_node_rid_,
                                                destination_endpoint_rid_)
        : resolve_active_spot_runtime (spot_);
    const bool local_target =
      destination_class_ == zmp_spot_class
        ? static_cast<bool> (find_spot_state_by_identity (
            destination_node_rid_, destination_endpoint_rid_))
        : static_cast<bool> (
            find_router_state_by_rid (destination_endpoint_rid_));
    int rc = local_target
               ? dispatch_local_request (destination_class_ == zmp_router_class
                                           ? destination_endpoint_rid_
                                           : std::string (),
                                         &combined)
               : (runtime ? enqueue_spot_state_route_ingress (state.get (),
                                                              runtime, &combined,
                                                              flags_)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_request (destination_class_ == zmp_router_class
                                       ? destination_endpoint_rid_
                                       : std::string (),
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

    return start_spot_request_common (
      spot_, zmp_spot_class, routing_id_key (dest_node_rid_),
      routing_id_key (dest_spot_rid_), zmp_spot_class,
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_), parts_,
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
    if (!has_valid_routing_id (peer_rid_) || !handler_) {
        errno = EINVAL;
        return -1;
    }

    return start_spot_request_common (
      spot_, zmp_router_class, std::string (), routing_id_key (peer_rid_),
      zmp_router_class, routing_id_key (peer_rid_), std::string (), parts_,
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

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return -1;

    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_or_create_router_state (router_);
    bind_router_state_rid (router_, routing_id_key (&router_rid), state);

    uint64_t request_seq = 0;
    pending_spot_key_t key;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        request_seq =
          allocate_request_seq (&state->next_request_seq, state->pending_sequences);
        if (request_seq == 0)
            return -1;

        key.source_class = zmp_spot_class;
        key.source_rid = routing_id_key (dest_node_rid_);
        key.source_spot_rid = routing_id_key (dest_spot_rid_);
        key.request_seq = request_seq;
    }
    if (register_router_spot_pending_request (state, request_seq, key,
                                              timeout_ms_, handler_, userdata_)
        != 0)
        return -1;

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_router_class, std::string (), state->router_rid, zmp_spot_class,
          routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_),
          zlink::request_reply::request_type, request_seq, parts_, part_count_,
          &combined)
        != 0) {
        pending_reply_t pending;
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            state->pending_sequences.erase (request_seq);
            std::map<uint64_t, pending_reply_t>::iterator it =
              state->pending_replies.find (request_seq);
            if (it != state->pending_replies.end ()) {
                pending = it->second;
                state->pending_replies.erase (it);
            }
        }
        zlink::request_timeout::cancel (pending.timeout_task);
        return -1;
    }

    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? dispatch_local_request (std::string (), &combined)
               : (runtime ? enqueue_runtime_route_ingress_once (runtime,
                                                                &combined,
                                                                flags_)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_request (std::string (), &combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        pending_reply_t pending;
        {
            std::lock_guard<std::mutex> lock (state->mutex);
            state->pending_sequences.erase (request_seq);
            std::map<uint64_t, pending_reply_t>::iterator it =
              state->pending_replies.find (request_seq);
            if (it != state->pending_replies.end ()) {
                pending = it->second;
                state->pending_replies.erase (it);
            }
        }
        zlink::request_timeout::cancel (pending.timeout_task);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
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
  zlink_send_flags_t flags_)
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

    if (zlink::internal_pair_queue::ensure (resolve_spot_ctx (spot_),
                                            "zlink.spot.subscribe.recv",
                                            &state->subscribe_queue)
        != 0) {
        return -1;
    }

    return recv_internal_spot_subscribe_queue (&state->subscribe_queue,
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
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        parsed_spot_envelope_t spot_envelope;
        int rc = -1;
        if (!parse_spot_routed_envelope (&combined[0], combined.size (),
                                         &spot_envelope)) {
            const int saved_errno = errno != 0 ? errno : EPROTO;
            zlink::request_reply::close_built_parts (&combined);
            errno = saved_errno;
            return -1;
        }

        if (spot_envelope.destination_class == zmp_router_class) {
            zlink::spot_runtime_t *runtime =
              zlink::spot_node_access_t::runtime (
                static_cast<zlink::spot_node_t *> (node_));
            rc =
              runtime ? enqueue_runtime_node_router_once (
                          runtime, &combined, ZLINK_SEND_FLAGS_NONE)
                      : -1;
        } else {
            rc = process_route_combined_for_local_delivery (&combined);
        }

        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        if (rc != 0) {
            errno = saved_errno;
            return -1;
        }
    }
}

extern "C" int zlink_spot_process_node_router (void *node_, void *socket_)
{
    return zlink_spot_process_route_ingress (node_, socket_);
}

zlink_submit_result_t zlink_spot_send_router (void *spot_,
                                              const zlink_routing_id_t *peer_rid_,
                                              zlink_msg_t *parts_,
                                              size_t part_count_,
                                              zlink_send_flags_t flags_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (peer_rid_)) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }
    if (validate_recv_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return zlink::submit_result_internal::from_errno (errno);

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (zmp_spot_class, source_identity.node_rid,
                                   source_identity.spot_rid, zmp_router_class,
                                   std::string (), routing_id_key (peer_rid_),
                                   parts_, part_count_, &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    const bool local_target =
      static_cast<bool> (find_router_state_by_rid (routing_id_key (peer_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL : resolve_active_spot_runtime (spot_);
    int rc = local_target
               ? process_route_combined_for_local_delivery (&combined)
               : (runtime ? enqueue_spot_state_route_ingress (state.get (),
                                                              runtime, &combined,
                                                              flags_)
                          : -1);
    if (rc != 0 && !local_target)
        rc = process_route_combined_for_local_delivery (&combined);
    if (rc != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (&combined);
        errno = saved_errno;
        return zlink::submit_result_internal::from_errno (saved_errno);
    }
    zlink::request_reply::close_built_parts (&combined);
    errno = 0;
    return ZLINK_SUBMIT_OK;
}

zlink_submit_result_t zlink_spot_send_channel (void *spot_,
                                               const char *channel_name_,
                                               zlink_msg_t *parts_,
                                               size_t part_count_,
                                               zlink_send_flags_t flags_)
{
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
    return zlink_send (router, parts_, part_count_, flags_);
}

zlink_submit_result_t zlink_spot_request_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    return zlink::submit_result_internal::from_request_submit_rc (
      start_spot_request_to_router (spot_, peer_rid_, parts_, part_count_,
                                    flags_, timeout_ms_, handler_, userdata_));
}

zlink_submit_result_t zlink_spot_request_channel (
  void *spot_,
  const char *channel_name_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_)
{
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

    return zlink::submit_result_internal::from_request_submit_rc (
      reqrep::start_request (make_socket_handle (router), NULL, parts_,
                             part_count_, flags_, timeout_ms_, handler_,
                             userdata_));
}

zlink_submit_result_t zlink_spot_reply_spot (void *spot_,
                                             const zlink_routing_id_t *dest_node_rid_,
                                             const zlink_routing_id_t *dest_spot_rid_,
                                             uint64_t request_seq_,
                                             zlink_msg_t *parts_,
                                             size_t part_count_)
{
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

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          zmp_spot_class, routing_id_key (dest_node_rid_),
          routing_id_key (dest_spot_rid_), zlink::request_reply::reply_type,
          request_seq_, parts_, part_count_, &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);
    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? dispatch_local_reply (&combined)
               : (runtime ? enqueue_spot_state_route_ingress (
                            state.get (), runtime, &combined,
                            ZLINK_SEND_FLAGS_NONE)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

zlink_submit_result_t zlink_spot_reply_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (peer_rid_) || request_seq_ == 0) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    routing_pair_t source_identity;
    if (!resolve_spot_identity (spot_, &source_identity))
        return zlink::submit_result_internal::from_errno (errno);

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_spot_class, source_identity.node_rid, source_identity.spot_rid,
          zmp_router_class, std::string (), routing_id_key (peer_rid_),
          zlink::request_reply::reply_type, request_seq_, parts_, part_count_,
          &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);
    const bool local_target =
      static_cast<bool> (find_router_state_by_rid (routing_id_key (peer_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL : resolve_active_spot_runtime (spot_);
    int rc = local_target
               ? dispatch_local_reply (&combined)
               : (runtime ? enqueue_runtime_route_ingress_once (
                            runtime, &combined, ZLINK_SEND_FLAGS_NONE)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_reply (&combined);
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
    if (state->request_handler || state->dispatch.handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return ZLINK_HANDLER_BUSY;
    }

    state->request_handler = handler_;
    state->request_handler_userdata = userdata_;
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
        if (state->request_handler || state->dispatch.handler) {
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

zlink_recv_result_t zlink_spot_recv (void *spot_,
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
    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->request_handler
        || (state->dispatch.handler
            && !in_spot_dispatch_event_callback (spot_))) {
        errno = EBUSY;
        return ZLINK_RECV_BUSY;
    }
    if (zlink::internal_pair_queue::ensure (resolve_spot_ctx (spot_),
                                            "zlink.spot.routed.recv",
                                            &state->recv_queue)
        != 0)
        return ZLINK_RECV_TERMINATED;
    lock.unlock ();
    return zlink::recv_result_internal::from_rc (
      recv_internal_spot_queue (&state->recv_queue, source_rid_out_,
                                spot_rid_out_, request_seq_out_,
                                parts_out_, part_count_out_, flags_));
}

zlink_submit_result_t zlink_router_request_spot (
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

zlink_submit_result_t zlink_router_reply_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
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

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return zlink::submit_result_internal::from_errno (errno);

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_router_class, std::string (), routing_id_key (&router_rid),
          zmp_spot_class, routing_id_key (dest_node_rid_),
          routing_id_key (dest_spot_rid_), zlink::request_reply::reply_type,
          request_seq_, parts_, part_count_, &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);
    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? dispatch_local_reply (&combined)
               : (runtime ? enqueue_runtime_route_ingress_once (
                            runtime, &combined, ZLINK_SEND_FLAGS_NONE)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

zlink_submit_result_t zlink_router_send_spot (
  void *router_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_send_flags_t flags_)
{
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

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return zlink::submit_result_internal::from_errno (errno);

    std::vector<zlink_msg_t> combined;
    if (build_spot_routed_message (zmp_router_class, std::string (),
                                   routing_id_key (&router_rid), zmp_spot_class,
                                   routing_id_key (dest_node_rid_),
                                   routing_id_key (dest_spot_rid_), parts_,
                                   part_count_, &combined)
        != 0)
        return zlink::submit_result_internal::from_errno (errno);
    const bool local_target = static_cast<bool> (find_spot_state_by_identity (
      routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_)));
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       routing_id_key (dest_node_rid_),
                       routing_id_key (dest_spot_rid_));
    int rc = local_target
               ? process_route_combined_for_local_delivery (&combined)
               : (runtime ? enqueue_runtime_route_ingress_once (runtime,
                                                                &combined,
                                                                flags_)
                          : -1);
    if (rc != 0 && !local_target)
        rc = process_route_combined_for_local_delivery (&combined);
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
    state->default_timeout_ms = static_cast<uint32_t> (timeout_ms);
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
        timeout_ms = static_cast<int> (state->default_timeout_ms);
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

    std::vector<std::shared_ptr<zlink::request_timeout::task_t> > timeout_tasks;
    zlink::service_control_runtime_t *dispatch_runtime = NULL;
    uint64_t dispatch_task_id = 0;
    std::shared_ptr<spot_request_reply_state_t> state =
      try_find_spot_state (spot);
    if (state) {
        {
            std::lock_guard<std::mutex> dispatch_lock (
              state->dispatch.mutex);
            state->dispatch.pending_event_mask = 0;
            state->dispatch.running = false;
        }

        std::lock_guard<std::mutex> state_lock (state->mutex);
        for (std::map<pending_spot_key_t, pending_reply_t>::iterator it =
               state->pending_replies.begin ();
             it != state->pending_replies.end (); ++it) {
            timeout_tasks.push_back (it->second.timeout_task);
        }
        state->pending_replies.clear ();
        state->pending_sequences.clear ();
        state->request_handler = NULL;
        state->request_handler_userdata = NULL;
        state->dispatch.handler = NULL;
        state->dispatch.handler_userdata = NULL;
        dispatch_runtime = state->dispatch.runtime;
        dispatch_task_id = state->dispatch.task_id;
        state->dispatch.runtime = NULL;
        state->dispatch.task_id = 0;
    }
    if (dispatch_runtime && dispatch_task_id != 0)
        (void) dispatch_runtime->remove_task (dispatch_task_id);
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        zlink::internal_pair_queue::close (&state->subscribe_queue);
        zlink::internal_pair_queue::close (&state->recv_queue);
    }
    for (size_t i = 0; i < timeout_tasks.size (); ++i)
        zlink::request_timeout::cancel (timeout_tasks[i]);
    erase_spot_owner_state (spot_);
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    for (spot_state_identity_index_t::iterator it =
           g_spot_state_identity_index.begin ();
         it != g_spot_state_identity_index.end ();) {
        std::shared_ptr<spot_request_reply_state_t> indexed = it->second.lock ();
        if (!indexed || indexed == state)
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

    std::vector<std::shared_ptr<zlink::request_timeout::task_t> > timeout_tasks;
    std::shared_ptr<router_spot_request_reply_state_t> state =
      std::static_pointer_cast<router_spot_request_reply_state_t> (
        handle.socket->router_spot_request_reply_state ());
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        for (std::map<uint64_t, pending_reply_t>::iterator it =
               state->pending_replies.begin ();
             it != state->pending_replies.end (); ++it) {
            timeout_tasks.push_back (it->second.timeout_task);
        }
        state->pending_replies.clear ();
        state->pending_sequences.clear ();
    }
    for (size_t i = 0; i < timeout_tasks.size (); ++i)
        zlink::request_timeout::cancel (timeout_tasks[i]);
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
