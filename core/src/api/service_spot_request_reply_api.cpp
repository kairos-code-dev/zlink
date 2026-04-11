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
#include "api/service_spot_request_reply_internal.hpp"
#include "api/socket_api_internal.hpp"
#include "api/status_internal.hpp"
#include "api/submit_result_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "core/recv_tls_view.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_pub.hpp"
#include "utils/random.hpp"

namespace
{
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

std::map<void *, std::shared_ptr<spot_request_reply_state_t> > g_spot_owner_states;

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

std::shared_ptr<spot_request_reply_state_t> try_find_spot_state (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return std::shared_ptr<spot_request_reply_state_t> ();

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    std::map<void *, std::shared_ptr<spot_request_reply_state_t> >::iterator it =
      g_spot_owner_states.find (spot_);
    return it != g_spot_owner_states.end ()
             ? it->second
             : std::shared_ptr<spot_request_reply_state_t> ();
}

void maybe_dispatch_spot_event (spot_request_reply_state_t *state_,
                                zlink_spot_dispatch_event_t event_)
{
    zlink_spot_dispatch_event_handler_fn handler = NULL;
    void *userdata = NULL;
    void *owner = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->dispatch_event_handler;
        userdata = state_->dispatch_event_handler_userdata;
        owner = state_->owner;
    }

    if (handler)
        handler (owner, event_, userdata);
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

int queue_spot_message (
  spot_request_reply_state_t *state_,
  const zlink_routing_id_t *source_rid_,
  const zlink_routing_id_t *spot_rid_,
  uint64_t request_seq_,
  zlink_msg_t *parts_,
  size_t part_count_)
{
    if (!state_ || !parts_) {
        errno = EFAULT;
        return -1;
    }

    if (zlink::internal_pair_queue::ensure (resolve_spot_ctx (state_->owner),
                                    "zlink.spot.routed.recv",
                                    &state_->recv_queue)
        != 0)
        return -1;

    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);
    const void *source_data =
      has_valid_routing_id (source_rid_) ? source_rid_->data : NULL;
    const size_t source_size =
      has_valid_routing_id (source_rid_) ? source_rid_->size : 0;
    const void *spot_data =
      has_valid_routing_id (spot_rid_) ? spot_rid_->data : NULL;
    const size_t spot_size = has_valid_routing_id (spot_rid_) ? spot_rid_->size
                                                              : 0;
    if (zlink::internal_pair_queue::send_buffer_frame (
          state_->recv_queue.tx, source_data, source_size, ZLINK_SNDMORE)
        != 0
        || zlink::internal_pair_queue::send_buffer_frame (
             state_->recv_queue.tx, spot_data, spot_size, ZLINK_SNDMORE)
             != 0
        || zlink::internal_pair_queue::send_buffer_frame (
             state_->recv_queue.tx, seq_buf, sizeof (seq_buf), ZLINK_SNDMORE)
             != 0) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        return -1;
    }
    for (size_t i = 0; i < part_count_; ++i) {
        const int flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        if (state_->recv_queue.tx->send (
              reinterpret_cast<zlink::msg_t *> (&parts_[i]), flags)
            != 0) {
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            return -1;
        }
    }

    maybe_dispatch_spot_event (state_, ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE);
    return 0;
}

int recv_internal_spot_queue (zlink::internal_pair_queue::queue_t *queue_,
                              const zlink_routing_id_t **source_rid_out_,
                              const zlink_routing_id_t **spot_rid_out_,
                              uint64_t *request_seq_out_,
                              zlink_msg_t **parts_out_,
                              size_t *part_count_out_,
                              int flags_)
{
    if (!queue_ || !queue_->rx || !source_rid_out_ || !spot_rid_out_
        || !request_seq_out_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink_msg_t source_frame;
    zlink_msg_t spot_frame;
    zlink_msg_t seq_frame;
    zlink_msg_t *first_payload = NULL;
    zlink_msg_init (&source_frame);
    zlink_msg_init (&spot_frame);
    zlink_msg_init (&seq_frame);

    if (zlink::recv_tls_view::begin_with_first_slot (
          parts_out_, part_count_out_, &first_payload)
        != 0)
        return -1;

    while (queue_->rx->recv (reinterpret_cast<zlink::msg_t *> (&source_frame),
                             flags_)
           != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        if ((flags_ & ZLINK_DONTWAIT) != 0 || saved_errno != EAGAIN) {
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
        if (zlink::wait_socket_events_internal (queue_->rx, ZLINK_POLLIN, -1)
            <= 0) {
            zlink::recv_tls_view::abort ();
            errno = saved_errno;
            return -1;
        }
        zlink_msg_init (&source_frame);
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, &spot_frame, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, &seq_frame, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }
    if (zlink_msg_size (&seq_frame) != 8) {
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = EPROTO;
        return -1;
    }
    if (zlink::internal_pair_queue::recv_followup_with_retry (
          queue_->rx, first_payload, flags_)
        != 0) {
        const int saved_errno = errno;
        zlink_msg_close (&source_frame);
        zlink_msg_close (&spot_frame);
        zlink_msg_close (&seq_frame);
        zlink::recv_tls_view::abort ();
        errno = saved_errno;
        return -1;
    }

    g_spot_recv_source_rid.size =
      static_cast<uint8_t> (std::min (zlink_msg_size (&source_frame),
                                      sizeof (g_spot_recv_source_rid.data)));
    if (g_spot_recv_source_rid.size > 0) {
        memcpy (g_spot_recv_source_rid.data, zlink_msg_data (&source_frame),
                g_spot_recv_source_rid.size);
    }
    g_spot_recv_spot_rid.size =
      static_cast<uint8_t> (std::min (zlink_msg_size (&spot_frame),
                                      sizeof (g_spot_recv_spot_rid.data)));
    if (g_spot_recv_spot_rid.size > 0) {
        memcpy (g_spot_recv_spot_rid.data, zlink_msg_data (&spot_frame),
                g_spot_recv_spot_rid.size);
    }
    *source_rid_out_ = &g_spot_recv_source_rid;
    *spot_rid_out_ = g_spot_recv_spot_rid.size > 0 ? &g_spot_recv_spot_rid
                                                    : NULL;
    *request_seq_out_ = zlink::request_reply::decode_u64_be (
      static_cast<const unsigned char *> (zlink_msg_data (&seq_frame)));
    zlink_msg_close (&source_frame);
    zlink_msg_close (&spot_frame);
    zlink_msg_close (&seq_frame);

    if (!zlink::internal_pair_queue::frame_has_more (*first_payload))
        return zlink::recv_tls_view::commit_reserved_single (parts_out_,
                                                             part_count_out_);
    return zlink::internal_pair_queue::export_followup_sequence_from_reserved_first (
      queue_->rx, parts_out_, part_count_out_);
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
    zlink_router_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state_->mutex);
        handler = state_->handler;
        handler_userdata = state_->handler_userdata;
    }

    if (handler) {
        handler (source_node_rid_, source_spot_rid_, request_seq_, parts_,
                 part_count_, handler_userdata);
        return 0;
    }

    if (zlink::internal_pair_queue::ensure (
          state_->owner ? as_socket_handle (state_->owner).socket->get_ctx ()
                        : NULL,
          "zlink.router.spot.recv", &state_->recv_queue)
        != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }

    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);
    const void *source_data =
      has_valid_routing_id (source_node_rid_) ? source_node_rid_->data : NULL;
    const size_t source_size =
      has_valid_routing_id (source_node_rid_) ? source_node_rid_->size : 0;
    const void *spot_data =
      has_valid_routing_id (source_spot_rid_) ? source_spot_rid_->data : NULL;
    const size_t spot_size =
      has_valid_routing_id (source_spot_rid_) ? source_spot_rid_->size : 0;
    if (zlink::internal_pair_queue::send_buffer_frame (
          state_->recv_queue.tx, source_data, source_size, ZLINK_SNDMORE)
        != 0
        || zlink::internal_pair_queue::send_buffer_frame (
             state_->recv_queue.tx, spot_data, spot_size, ZLINK_SNDMORE)
             != 0
        || zlink::internal_pair_queue::send_buffer_frame (
             state_->recv_queue.tx, seq_buf, sizeof (seq_buf), ZLINK_SNDMORE)
             != 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return -1;
    }
    for (size_t i = 0; i < part_count_; ++i) {
        const int flags = (i + 1 < part_count_) ? ZLINK_SNDMORE : 0;
        if (state_->recv_queue.tx->send (
              reinterpret_cast<zlink::msg_t *> (&parts_[i]), flags)
            != 0) {
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            return -1;
        }
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
        || (protocol_id->flags () & zlink::msg_t::command) == 0
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

void refresh_spot_identity_index (spot_handle_t *spot_,
                                  const std::shared_ptr<spot_request_reply_state_t> &state_)
{
    if (!spot_ || !state_)
        return;

    routing_pair_t identity;
    if (!resolve_spot_identity (spot_, &identity))
        return;

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    g_spot_state_identity_index[make_spot_identity_key (identity.node_rid,
                                                        identity.spot_rid)] =
      state_;
}

std::shared_ptr<spot_request_reply_state_t> find_or_create_spot_state (void *spot_)
{
    spot_handle_t *spot = as_spot_handle (spot_);
    if (!spot)
        return std::shared_ptr<spot_request_reply_state_t> ();

    std::shared_ptr<spot_request_reply_state_t> state;
    {
        std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
        std::map<void *, std::shared_ptr<spot_request_reply_state_t> >::iterator
          it = g_spot_owner_states.find (spot_);
        if (it != g_spot_owner_states.end ())
            state = it->second;
        if (!state) {
            state.reset (new spot_request_reply_state_t (spot_));
            g_spot_owner_states[spot_] = state;
        }
    }
    if (!state) {
        state.reset (new spot_request_reply_state_t (spot_));
    }
    refresh_spot_identity_index (spot, state);
    return state;
}

std::shared_ptr<router_spot_request_reply_state_t>
find_or_create_router_state (void *router_)
{
    const socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket)
        return std::shared_ptr<router_spot_request_reply_state_t> ();

    std::shared_ptr<router_spot_request_reply_state_t> state =
      std::static_pointer_cast<router_spot_request_reply_state_t> (
        handle.socket->router_spot_request_reply_state ());
    if (!state) {
        state.reset (new router_spot_request_reply_state_t (router_));
        handle.socket->set_router_spot_request_reply_state (state);
    }
    return state;
}

std::shared_ptr<spot_request_reply_state_t>
find_spot_state_by_identity (const std::string &node_rid_,
                             const std::string &spot_rid_)
{
    const std::string key = make_spot_identity_key (node_rid_, spot_rid_);
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    spot_state_identity_index_t::iterator it = g_spot_state_identity_index.find (key);
    if (it == g_spot_state_identity_index.end ())
        return std::shared_ptr<spot_request_reply_state_t> ();
    std::shared_ptr<spot_request_reply_state_t> state = it->second.lock ();
    if (!state) {
        g_spot_state_identity_index.erase (it);
        return std::shared_ptr<spot_request_reply_state_t> ();
    }
    return state;
}

std::shared_ptr<router_spot_request_reply_state_t>
find_router_state_by_rid (const std::string &router_rid_)
{
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    router_state_identity_index_t::iterator it =
      g_router_state_identity_index.find (router_rid_);
    if (it == g_router_state_identity_index.end ())
        return std::shared_ptr<router_spot_request_reply_state_t> ();
    std::shared_ptr<router_spot_request_reply_state_t> state = it->second.lock ();
    if (!state) {
        g_router_state_identity_index.erase (it);
        return std::shared_ptr<router_spot_request_reply_state_t> ();
    }
    return state;
}

zlink::spot_runtime_t *resolve_runtime_for_spot_destination (
  const std::string &node_rid_,
  const std::string &spot_rid_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (node_rid_, spot_rid_);
    if (!state)
        return NULL;
    return resolve_active_spot_runtime (state->owner);
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

void bind_router_state_rid (void *router_,
                            const std::string &router_rid_,
                            const std::shared_ptr<router_spot_request_reply_state_t> &state_)
{
    if (!router_ || !state_)
        return;

    {
        std::lock_guard<std::mutex> state_lock (state_->mutex);
        state_->router_rid = router_rid_;
    }

    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    g_router_state_identity_index[router_rid_] = state_;
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

struct spot_timeout_callback_ctx_t
{
    std::shared_ptr<spot_request_reply_state_t> state;
    pending_spot_key_t key;
};

struct router_spot_timeout_callback_ctx_t
{
    std::shared_ptr<router_spot_request_reply_state_t> state;
    uint64_t request_seq;
};

void on_spot_request_timeout (void *userdata_)
{
    std::unique_ptr<spot_timeout_callback_ctx_t> ctx (
      static_cast<spot_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::map<pending_spot_key_t, pending_reply_t>::iterator it =
          ctx->state->pending_replies.find (ctx->key);
        if (it == ctx->state->pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->key.request_seq);
        ctx->state->pending_replies.erase (it);
        found = true;
    }

    if (found)
        zlink::request_reply::complete_reply_callback (
          pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
}

void on_router_spot_request_timeout (void *userdata_)
{
    std::unique_ptr<router_spot_timeout_callback_ctx_t> ctx (
      static_cast<router_spot_timeout_callback_ctx_t *> (userdata_));
    if (!ctx.get () || !ctx->state)
        return;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (ctx->state->mutex);
        std::map<uint64_t, pending_reply_t>::iterator it =
          ctx->state->pending_replies.find (ctx->request_seq);
        if (it == ctx->state->pending_replies.end ())
            return;
        pending = it->second;
        ctx->state->pending_sequences.erase (ctx->request_seq);
        ctx->state->pending_replies.erase (it);
        found = true;
    }

    if (found)
        zlink::request_reply::complete_reply_callback (
          pending.handler, ETIMEDOUT, NULL, 0, pending.userdata);
}

bool parse_combined_local_message (
  std::vector<zlink_msg_t> *combined_,
  parsed_spot_envelope_t *spot_envelope_out_,
  zlink::request_reply::parsed_envelope_t *request_reply_envelope_out_)
{
    if (!combined_ || !spot_envelope_out_ || !request_reply_envelope_out_) {
        errno = EFAULT;
        return false;
    }

    if (!parse_spot_routed_envelope (&(*combined_)[0], combined_->size (),
                                     spot_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    if (!zlink::request_reply::parse_envelope (
          spot_envelope_out_->payload_parts, spot_envelope_out_->payload_part_count,
          request_reply_envelope_out_)) {
        errno = EPROTO;
        return false;
    }

    return true;
}

int recv_combined_router_message (zlink::socket_base_t *socket_,
                                  std::vector<zlink_msg_t> *out_)
{
    if (!socket_ || !out_) {
        errno = EFAULT;
        return -1;
    }

    out_->clear ();

    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));

    zlink_msg_t first;
    zlink_msg_init (&first);
    if (zlink::recv_msg_routed_socket (socket_, &first, &source_rid,
                                       ZLINK_DONTWAIT)
        != 0) {
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

int build_spot_request_reply_message (uint8_t source_class_,
                                      const std::string &source_node_rid_,
                                      const std::string &source_endpoint_rid_,
                                      uint8_t destination_class_,
                                      const std::string &destination_node_rid_,
                                      const std::string &destination_endpoint_rid_,
                                      uint8_t message_type_,
                                      uint64_t request_seq_,
                                      zlink_msg_t *parts_,
                                      size_t part_count_,
                                      std::vector<zlink_msg_t> *out_)
{
    if (!parts_ || part_count_ == 0 || request_seq_ == 0 || !out_) {
        errno = EINVAL;
        return -1;
    }

    const size_t total_part_count =
      spot_routed_control_part_count + zlink::request_reply::control_part_count
      + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char spot_protocol_id = zmp_spot_routed_protocol_id;
    unsigned char version = zmp_protocol_version;
    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;
    unsigned char rr_protocol_id = zlink::request_reply::protocol_id;
    unsigned char rr_type = message_type_;
    unsigned char seq_buf[8];
    zlink::request_reply::encode_u64_be (request_seq_, seq_buf);

    if (zlink::request_reply::init_control_part (&(*out_)[0], &spot_protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&(*out_)[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[2], &source_class, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[3],
                                                    source_node_rid_.data (),
                                                    source_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[4],
                                                    source_endpoint_rid_.data (),
                                                    source_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[5],
                                                    &destination_class, 1)
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[6], destination_node_rid_.data (),
             destination_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[7], destination_endpoint_rid_.data (),
             destination_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[8],
                                                    &rr_protocol_id, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[9], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[10], &rr_type, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[11], seq_buf,
                                                    sizeof (seq_buf))
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[spot_routed_control_part_count
                                     + zlink::request_reply::control_part_count
                                     + i],
                            &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}

int build_spot_routed_message (uint8_t source_class_,
                               const std::string &source_node_rid_,
                               const std::string &source_endpoint_rid_,
                               uint8_t destination_class_,
                               const std::string &destination_node_rid_,
                               const std::string &destination_endpoint_rid_,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               std::vector<zlink_msg_t> *out_)
{
    if (!parts_ || part_count_ == 0 || !out_) {
        errno = EINVAL;
        return -1;
    }

    const size_t total_part_count = spot_routed_control_part_count + part_count_;
    out_->resize (total_part_count);
    for (size_t i = 0; i < total_part_count; ++i)
        zlink_msg_init (&(*out_)[i]);

    unsigned char spot_protocol_id = zmp_spot_routed_protocol_id;
    unsigned char version = zmp_protocol_version;
    unsigned char source_class = source_class_;
    unsigned char destination_class = destination_class_;

    if (zlink::request_reply::init_control_part (&(*out_)[0], &spot_protocol_id, 1)
          != 0
        || zlink::request_reply::init_control_part (&(*out_)[1], &version, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[2], &source_class, 1)
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[3],
                                                    source_node_rid_.data (),
                                                    source_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[4],
                                                    source_endpoint_rid_.data (),
                                                    source_endpoint_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (&(*out_)[5],
                                                    &destination_class, 1)
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[6], destination_node_rid_.data (),
             destination_node_rid_.size ())
             != 0
        || zlink::request_reply::init_control_part (
             &(*out_)[7], destination_endpoint_rid_.data (),
             destination_endpoint_rid_.size ())
             != 0) {
        const int saved_errno = errno;
        zlink::request_reply::close_built_parts (out_);
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = saved_errno;
        return -1;
    }

    for (size_t i = 0; i < part_count_; ++i) {
        if (zlink_msg_move (&(*out_)[spot_routed_control_part_count + i], &parts_[i])
            != 0) {
            const int saved_errno = errno;
            zlink::request_reply::close_built_parts (out_);
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}

int deliver_reply_to_spot (
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (spot_envelope_.destination_node_rid,
                                   spot_envelope_.destination_endpoint_rid);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_spot_key_t key;
    key.source_class = spot_envelope_.source_class;
    key.source_rid = spot_envelope_.source_class == zmp_router_class
                       ? spot_envelope_.source_endpoint_rid
                       : spot_envelope_.source_node_rid;
    key.source_spot_rid = spot_envelope_.source_class == zmp_spot_class
                            ? spot_envelope_.source_endpoint_rid
                            : std::string ();
    key.request_seq = rr_envelope_.request_seq;

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::map<pending_spot_key_t, pending_reply_t>::iterator it =
          state->pending_replies.find (key);
        if (it != state->pending_replies.end ()) {
            pending = it->second;
            state->pending_sequences.erase (key.request_seq);
            state->pending_replies.erase (it);
            found = true;
        }
    }

    zlink::request_timeout::cancel (pending.timeout_task);

    if (!found)
        return 0;

    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts,
          rr_envelope_.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        return -1;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    return 0;
}

int deliver_reply_to_router (const std::string &router_rid_,
                             const zlink::request_reply::parsed_envelope_t
                               &rr_envelope_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        errno = ENOENT;
        return -1;
    }

    pending_reply_t pending;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        std::map<uint64_t, pending_reply_t>::iterator it =
          state->pending_replies.find (rr_envelope_.request_seq);
        if (it != state->pending_replies.end ()) {
            pending = it->second;
            state->pending_sequences.erase (rr_envelope_.request_seq);
            state->pending_replies.erase (it);
            found = true;
        }
    }

    zlink::request_timeout::cancel (pending.timeout_task);

    if (!found)
        return 0;

    int callback_errno = 0;
    zlink_msg_t *callback_parts = rr_envelope_.payload_parts;
    size_t callback_part_count = rr_envelope_.payload_part_count;
    if (zlink::request_reply::decode_reply_completion (
          rr_envelope_.message_type, rr_envelope_.payload_parts,
          rr_envelope_.payload_part_count, &callback_errno, &callback_parts,
          &callback_part_count)
        != 0) {
        return -1;
    }

    zlink::request_reply::complete_reply_callback (
      pending.handler, callback_errno, callback_parts, callback_part_count,
      pending.userdata);
    return 0;
}

int dispatch_local_reply (std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == zmp_spot_class)
        return deliver_reply_to_spot (spot_envelope, rr_envelope);

    return deliver_reply_to_router (spot_envelope.destination_endpoint_rid,
                                    rr_envelope);
}

int synthesize_local_error_reply (const parsed_spot_envelope_t &request_envelope_,
                                  uint64_t request_seq_,
                                  int errnum_)
{
    zlink_msg_t errno_part;
    zlink_msg_init (&errno_part);

    unsigned char errbuf[4];
    zlink::request_reply::encode_u32_be (static_cast<uint32_t> (errnum_),
                                         errbuf);
    if (zlink_msg_init_size (&errno_part, sizeof (errbuf)) != 0)
        return -1;
    memcpy (zlink_msg_data (&errno_part), errbuf, sizeof (errbuf));

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          request_envelope_.destination_class,
          request_envelope_.destination_node_rid,
          request_envelope_.destination_endpoint_rid,
          request_envelope_.source_class, request_envelope_.source_node_rid,
          request_envelope_.source_endpoint_rid,
          zlink::request_reply::error_reply_type,
          request_seq_, &errno_part, 1, &combined)
        != 0) {
        const int saved_errno = errno;
        zlink::request_reply::consume_send_frame (&errno_part);
        errno = saved_errno;
        return -1;
    }

    const int rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
}

int dispatch_spot_request_to_spot (const parsed_spot_envelope_t &spot_envelope_,
                                   const zlink::request_reply::parsed_envelope_t
                                     &rr_envelope_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (spot_envelope_.destination_node_rid,
                                   spot_envelope_.destination_endpoint_rid);
    if (!state) {
        // Local short-circuit still preserves wire semantics by building the
        // same request envelope and completing the requester through a
        // protocol-level error reply instead of a direct local failure.
        return synthesize_local_error_reply (spot_envelope_,
                                             rr_envelope_.request_seq,
                                             ENOENT);
    }

    zlink_routing_id_t source_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (
      spot_envelope_.source_class == zmp_router_class
        ? spot_envelope_.source_endpoint_rid
        : spot_envelope_.source_node_rid,
      &source_rid);
    routing_id_from_string (
      spot_envelope_.source_class == zmp_spot_class
        ? spot_envelope_.source_endpoint_rid
        : std::string (),
      &source_spot_rid);

    return dispatch_spot_message (
      state.get (), &source_rid, &source_spot_rid, rr_envelope_.request_seq,
      rr_envelope_.payload_parts, rr_envelope_.payload_part_count);
}

int dispatch_spot_request_to_router (
  const std::string &router_rid_,
  const parsed_spot_envelope_t &spot_envelope_,
  const zlink::request_reply::parsed_envelope_t &rr_envelope_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        return synthesize_local_error_reply (spot_envelope_,
                                             rr_envelope_.request_seq,
                                             ENOENT);
    }

    zlink_router_spot_handler_fn handler = NULL;
    void *handler_userdata = NULL;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        handler = state->handler;
        handler_userdata = state->handler_userdata;
    }

    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (spot_envelope_.source_node_rid, &source_node_rid);
    routing_id_from_string (spot_envelope_.source_endpoint_rid,
                            &source_spot_rid);
    if (handler) {
        handler (&source_node_rid, &source_spot_rid, rr_envelope_.request_seq,
                 rr_envelope_.payload_parts, rr_envelope_.payload_part_count,
                 handler_userdata);
        return 0;
    }

    return dispatch_router_spot_message (
      state.get (), &source_node_rid, &source_spot_rid,
      rr_envelope_.request_seq, rr_envelope_.payload_parts,
      rr_envelope_.payload_part_count);
}

int dispatch_local_request (const std::string &router_rid_,
                            std::vector<zlink_msg_t> *combined_)
{
    parsed_spot_envelope_t spot_envelope;
    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (!parse_combined_local_message (combined_, &spot_envelope, &rr_envelope))
        return -1;

    if (spot_envelope.destination_class == zmp_spot_class)
        return dispatch_spot_request_to_spot (spot_envelope, rr_envelope);

    return dispatch_spot_request_to_router (router_rid_, spot_envelope,
                                            rr_envelope);
}

void erase_spot_pending_request (
  const std::shared_ptr<spot_request_reply_state_t> &state_,
  const pending_spot_key_t &key_)
{
    std::lock_guard<std::mutex> lock (state_->mutex);
    state_->pending_sequences.erase (key_.request_seq);
    std::map<pending_spot_key_t, pending_reply_t>::iterator it =
      state_->pending_replies.find (key_);
    if (it == state_->pending_replies.end ())
        return;
    zlink::request_timeout::cancel (it->second.timeout_task);
    state_->pending_replies.erase (it);
}

int dispatch_local_built_message (uint8_t source_class_,
                                  const std::string &source_node_rid_,
                                  const std::string &source_endpoint_rid_,
                                  uint8_t destination_class_,
                                  const std::string &destination_node_rid_,
                                  const std::string &destination_endpoint_rid_,
                                  uint8_t message_type_,
                                  uint64_t request_seq_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_)
{
    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          source_class_, source_node_rid_, source_endpoint_rid_,
          destination_class_, destination_node_rid_, destination_endpoint_rid_,
          message_type_, request_seq_, parts_, part_count_, &combined)
        != 0) {
        return -1;
    }

    const int rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return rc;
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
    pending_reply_t pending;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        const uint64_t request_seq =
          allocate_request_seq (&state->next_request_seq, state->pending_sequences);
        if (request_seq == 0)
            return -1;

        key.source_class = pending_source_class_;
        key.source_rid = pending_source_rid_;
        key.source_spot_rid = pending_source_spot_rid_;
        key.request_seq = request_seq;
        pending.key = key;
        pending.handler = handler_;
        pending.userdata = userdata_;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
        std::unique_ptr<spot_timeout_callback_ctx_t> timeout_ctx (
          new (std::nothrow) spot_timeout_callback_ctx_t ());
        if (!timeout_ctx.get ()) {
            errno = ENOMEM;
            return -1;
        }
        timeout_ctx->state = state;
        timeout_ctx->key = key;
        pending.timeout_task =
          zlink::request_timeout::schedule (resolved_timeout_ms,
                                            &on_spot_request_timeout,
                                            timeout_ctx.release ());
        if (!pending.timeout_task) {
            errno = ENOMEM;
            return -1;
        }
        state->pending_sequences.insert (request_seq);
        state->pending_replies[key] = pending;
    }

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
    pending_reply_t pending;
    uint32_t resolved_timeout_ms = zlink::request_reply::default_timeout_ms;
    {
        std::lock_guard<std::mutex> lock (state->mutex);
        request_seq =
          allocate_request_seq (&state->next_request_seq, state->pending_sequences);
        if (request_seq == 0)
            return -1;

        pending_spot_key_t key;
        key.source_class = zmp_spot_class;
        key.source_rid = routing_id_key (dest_node_rid_);
        key.source_spot_rid = routing_id_key (dest_spot_rid_);
        key.request_seq = request_seq;
        pending.key = key;
        pending.handler = handler_;
        pending.userdata = userdata_;
        resolved_timeout_ms = zlink::request_reply::resolve_timeout_ms (
          timeout_ms_, state->default_timeout_ms);
        std::unique_ptr<router_spot_timeout_callback_ctx_t> timeout_ctx (
          new (std::nothrow) router_spot_timeout_callback_ctx_t ());
        if (!timeout_ctx.get ()) {
            errno = ENOMEM;
            return -1;
        }
        timeout_ctx->state = state;
        timeout_ctx->request_seq = request_seq;
        pending.timeout_task =
          zlink::request_timeout::schedule (resolved_timeout_ms,
                                            &on_router_spot_request_timeout,
                                            timeout_ctx.release ());
        if (!pending.timeout_task) {
            errno = ENOMEM;
            return -1;
        }
        state->pending_sequences.insert (request_seq);
        state->pending_replies[request_seq] = pending;
    }

    std::vector<zlink_msg_t> combined;
    if (build_spot_request_reply_message (
          zmp_router_class, std::string (), state->router_rid, zmp_spot_class,
          routing_id_key (dest_node_rid_), routing_id_key (dest_spot_rid_),
          zlink::request_reply::request_type, request_seq, parts_, part_count_,
          &combined)
        != 0) {
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending_sequences.erase (request_seq);
        state->pending_replies.erase (request_seq);
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
        std::lock_guard<std::mutex> lock (state->mutex);
        state->pending_sequences.erase (request_seq);
        state->pending_replies.erase (request_seq);
        errno = saved_errno;
        return -1;
    }

    zlink::request_reply::close_built_parts (&combined);
    return 0;
}

int dispatch_local_direct_to_spot (uint8_t source_class_,
                                   const std::string &source_node_rid_,
                                   const std::string &source_endpoint_rid_,
                                   const std::string &dest_node_rid_,
                                   const std::string &dest_spot_rid_,
                                   zlink_msg_t *parts_,
                                   size_t part_count_)
{
    std::shared_ptr<spot_request_reply_state_t> state =
      find_spot_state_by_identity (dest_node_rid_, dest_spot_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = 0;
        return 0;
    }

    zlink_routing_id_t source_rid;
    zlink_routing_id_t spot_rid;
    routing_id_from_string (
      source_class_ == zmp_router_class ? source_endpoint_rid_ : source_node_rid_,
      &source_rid);
    routing_id_from_string (
      source_class_ == zmp_spot_class ? source_endpoint_rid_ : std::string (),
      &spot_rid);

    if (dispatch_spot_message (state.get (), &source_rid,
                               spot_rid.size > 0 ? &spot_rid : NULL, 0,
                               parts_, part_count_)
        != 0)
        return -1;
    errno = 0;
    return 0;
}

int dispatch_local_direct_to_router (const std::string &router_rid_,
                                     const std::string &source_node_rid_,
                                     const std::string &source_spot_rid_,
                                     zlink_msg_t *parts_,
                                     size_t part_count_)
{
    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_router_state_by_rid (router_rid_);
    if (!state) {
        zlink::request_reply::consume_send_frames_from (parts_, 0, part_count_);
        errno = ENOENT;
        return -1;
    }

    zlink_routing_id_t source_node_rid;
    zlink_routing_id_t source_spot_rid;
    routing_id_from_string (source_node_rid_, &source_node_rid);
    routing_id_from_string (source_spot_rid_, &source_spot_rid);
    return dispatch_router_spot_message (
      state.get (), &source_node_rid, &source_spot_rid, 0, parts_, part_count_);
}

int process_route_combined_for_local_delivery (std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t spot_envelope;
    if (!parse_spot_routed_envelope (&(*combined_)[0], combined_->size (),
                                     &spot_envelope)) {
        errno = EPROTO;
        return -1;
    }

    zlink::request_reply::parsed_envelope_t rr_envelope;
    if (zlink::request_reply::parse_envelope (spot_envelope.payload_parts,
                                              spot_envelope.payload_part_count,
                                              &rr_envelope)) {
        if (rr_envelope.message_type == zlink::request_reply::request_type) {
            if (spot_envelope.destination_class == zmp_spot_class)
                return dispatch_spot_request_to_spot (spot_envelope, rr_envelope);
            return dispatch_spot_request_to_router (
              spot_envelope.destination_endpoint_rid, spot_envelope, rr_envelope);
        }

        if (spot_envelope.destination_class == zmp_spot_class)
            return deliver_reply_to_spot (spot_envelope, rr_envelope);
        return deliver_reply_to_router (spot_envelope.destination_endpoint_rid,
                                        rr_envelope);
    }

    if (spot_envelope.destination_class == zmp_spot_class) {
        return dispatch_local_direct_to_spot (
          spot_envelope.source_class, spot_envelope.source_node_rid,
          spot_envelope.source_endpoint_rid, spot_envelope.destination_node_rid,
          spot_envelope.destination_endpoint_rid, spot_envelope.payload_parts,
          spot_envelope.payload_part_count);
    }

    return dispatch_local_direct_to_router (
      spot_envelope.destination_endpoint_rid, spot_envelope.source_node_rid,
      spot_envelope.source_endpoint_rid, spot_envelope.payload_parts,
      spot_envelope.payload_part_count);
}
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
              runtime ? enqueue_runtime_node_router_once (runtime, &combined, 0)
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

zlink_submit_result_t zlink_spot_request_spot (
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
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (validate_request_send_flags (flags_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    return zlink::submit_result_internal::from_rc (
      start_spot_request_to_spot (spot_, dest_node_rid_, dest_spot_rid_, parts_,
                                  part_count_, flags_, timeout_ms_, handler_,
                                  userdata_));
}

zlink_submit_result_t zlink_spot_send_spot (void *spot_,
                                            const zlink_routing_id_t *dest_node_rid_,
                                            const zlink_routing_id_t *dest_spot_rid_,
                                            zlink_msg_t *parts_,
                                            size_t part_count_,
                                            zlink_send_flags_t flags_)
{
    LIBZLINK_UNUSED (flags_);
    if (validate_request_parts (parts_, part_count_) != 0)
        return zlink::submit_result_internal::from_errno (errno);
    if (!has_valid_routing_id (dest_node_rid_) || !has_valid_routing_id (dest_spot_rid_)) {
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
                                   source_identity.spot_rid, zmp_spot_class,
                                   routing_id_key (dest_node_rid_),
                                   routing_id_key (dest_spot_rid_), parts_,
                                   part_count_, &combined)
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
               ? process_route_combined_for_local_delivery (&combined)
               : (runtime ? enqueue_spot_state_route_ingress (state.get (),
                                                              runtime, &combined,
                                                              flags_)
                          : -1);
    if (rc != 0 && !local_target)
        rc = process_route_combined_for_local_delivery (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
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
    return zlink::submit_result_internal::from_rc (
      start_spot_request_to_router (spot_, peer_rid_, parts_, part_count_,
                                    flags_, timeout_ms_, handler_, userdata_));
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
               : (runtime ? enqueue_spot_state_route_ingress (state.get (),
                                                              runtime, &combined,
                                                              0)
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
               : (runtime ? enqueue_runtime_route_ingress_once (runtime,
                                                                &combined,
                                                                0)
                          : -1);
    if (rc != 0 && !local_target)
        rc = dispatch_local_reply (&combined);
    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    return zlink::submit_result_internal::from_rc (rc);
}

bool zlink_spot_handler (void *spot_,
                         zlink_spot_handler_fn handler_,
                         void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return false;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return false;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return false;

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return false;
    }

    state->request_handler = handler_;
    state->request_handler_userdata = userdata_;
    return true;
}

bool zlink_spot_dispatch_event_handler (
  void *spot_,
  zlink_spot_dispatch_event_handler_fn handler_,
  void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return false;
    }

    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return false;
    }
    if (spot_transition_to_callback_mode (as_spot_handle (spot_)) != 0)
        return false;

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        spot_revert_callback_transition (as_spot_handle (spot_));
        errno = EBUSY;
        return false;
    }

    state->dispatch_event_handler = handler_;
    state->dispatch_event_handler_userdata = userdata_;
    return true;
}

int zlink_spot_recv (void *spot_,
                     const zlink_routing_id_t **source_rid_out_,
                     const zlink_routing_id_t **spot_rid_out_,
                     uint64_t *request_seq_out_,
                     zlink_msg_t **parts_out_,
                     size_t *part_count_out_,
                     int flags_)
{
    if (!source_rid_out_ || !spot_rid_out_ || !request_seq_out_ || !parts_out_
        || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;
    if (!as_spot_handle (spot_)) {
        errno = EFAULT;
        return -1;
    }
    if (spot_require_recv_model (as_spot_handle (spot_)) != 0)
        return -1;

    std::shared_ptr<spot_request_reply_state_t> state =
      find_or_create_spot_state (spot_);
    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->request_handler || state->dispatch_event_handler) {
        errno = EBUSY;
        return -1;
    }
    if (zlink::internal_pair_queue::ensure (resolve_spot_ctx (spot_),
                                            "zlink.spot.routed.recv",
                                            &state->recv_queue)
        != 0)
        return -1;
    lock.unlock ();
    return recv_internal_spot_queue (&state->recv_queue, source_rid_out_,
                                     spot_rid_out_, request_seq_out_,
                                     parts_out_, part_count_out_, flags_);
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

    return zlink::submit_result_internal::from_rc (
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
               : (runtime ? enqueue_runtime_route_ingress_once (runtime,
                                                                &combined,
                                                                0)
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

bool zlink_router_spot_handler (void *router_,
                                zlink_router_spot_handler_fn handler_,
                                void *userdata_)
{
    if (!handler_) {
        errno = EINVAL;
        return false;
    }

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return false;
    }

    zlink_routing_id_t router_rid;
    memset (&router_rid, 0, sizeof (router_rid));
    if (zlink_get_routing_id (router_, &router_rid) != 0 || router_rid.size == 0)
        return false;

    std::shared_ptr<router_spot_request_reply_state_t> state =
      find_or_create_router_state (router_);
    bind_router_state_rid (router_, routing_id_key (&router_rid), state);
    std::lock_guard<std::mutex> lock (state->mutex);
    if (state->handler) {
        errno = EBUSY;
        return false;
    }
    state->handler = handler_;
    state->handler_userdata = userdata_;
    return true;
}

int zlink_router_spot_recv (void *router_,
                            const zlink_routing_id_t **source_node_rid_out_,
                            const zlink_routing_id_t **source_spot_rid_out_,
                            uint64_t *request_seq_out_,
                            zlink_msg_t **parts_out_,
                            size_t *part_count_out_,
                            int flags_)
{
    if (!source_node_rid_out_ || !source_spot_rid_out_ || !request_seq_out_
        || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }
    if (validate_recv_flags (flags_) != 0)
        return -1;

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
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

    std::unique_lock<std::mutex> lock (state->mutex);
    if (state->handler) {
        errno = EBUSY;
        return -1;
    }

    if (zlink::internal_pair_queue::ensure (handle.socket->get_ctx (),
                                            "zlink.router.spot.recv",
                                            &state->recv_queue)
        != 0)
        return -1;
    lock.unlock ();
    return recv_internal_spot_queue (&state->recv_queue, source_node_rid_out_,
                                     source_spot_rid_out_, request_seq_out_,
                                     parts_out_, part_count_out_, flags_);
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

    std::shared_ptr<spot_request_reply_state_t> state =
      try_find_spot_state (spot);
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        zlink::internal_pair_queue::close (&state->recv_queue);
    }
    std::lock_guard<std::mutex> lock (g_spot_request_reply_index_mutex);
    g_spot_owner_states.erase (spot_);
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

    std::shared_ptr<router_spot_request_reply_state_t> state =
      std::static_pointer_cast<router_spot_request_reply_state_t> (
        handle.socket->router_spot_request_reply_state ());
    if (state) {
        std::lock_guard<std::mutex> state_lock (state->mutex);
        zlink::internal_pair_queue::close (&state->recv_queue);
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
