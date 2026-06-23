/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/socket/socket_api_internal.hpp"
#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_utils_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "services/actor/service_spot_actor_internal.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/pubsub/spot_subject_access.hpp"
#include "utils/clock.hpp"
#include "utils/debug_log.hpp"

namespace
{
namespace routed_protocol = zlink::spot_routed_protocol;

const bool spot_direct_route_debug_on = zlink::debug_env_enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE");

using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::process_parsed_route_combined_for_local_delivery;
using zlink::spot_reqrep_internal::process_route_combined_for_local_delivery;
using zlink::spot_reqrep_internal::resolve_runtime_for_spot_destination;
using zlink::spot_reqrep_internal::routed_spot_delivery_kind_t;
using zlink::spot_reqrep_internal::router_spot_delivery_kind_t;
using zlink::spot_reqrep_internal::routing_id_from_key;

bool spot_direct_route_debug_enabled ()
{
    return spot_direct_route_debug_on;
}

const size_t routed_send_drain_batch_limit = 2048;
const size_t routed_send_drain_batch_bytes_limit = 16 * 1024 * 1024;

bool is_queue_drain_retry_error (int err_)
{
    return err_ == EAGAIN || err_ == ENOTCONN || err_ == EHOSTUNREACH;
}

uint64_t routed_send_retry_deadline_ms ()
{
    return zlink::clock_t ().now_ms () + 10;
}

size_t routed_parts_encoded_bytes (const std::vector<zlink_msg_t> &parts_)
{
    size_t bytes = 0;
    for (std::vector<zlink_msg_t>::const_iterator it = parts_.begin (); it != parts_.end (); ++it)
        bytes += sizeof (uint32_t) + zlink_msg_size (const_cast<zlink_msg_t *> (&(*it)));
    return bytes > 0 ? bytes : 1;
}

bool routed_queue_has_room (
  const zlink::spot_data_plane_runtime_state_t::routed_send_queue_t &queue_,
  int hwm_,
  size_t byte_limit_,
  size_t message_bytes_)
{
    if (hwm_ == 0)
        return true;
    const size_t message_limit = static_cast<size_t> (hwm_ > 0 ? hwm_ : 1);
    if (queue_.messages.size () >= message_limit)
        return false;
    if (queue_.messages.empty ())
        return true;
    if (message_bytes_ > byte_limit_)
        return false;
    return queue_.queued_bytes <= byte_limit_ - message_bytes_;
}

bool routed_queue_can_resume (
  const zlink::spot_data_plane_runtime_state_t::routed_send_queue_t &queue_,
  int hwm_,
  size_t byte_limit_)
{
    if (hwm_ == 0)
        return true;
    const size_t message_limit = static_cast<size_t> (hwm_ > 0 ? hwm_ : 1);
    return queue_.messages.size () <= message_limit / 2 && queue_.queued_bytes <= byte_limit_ / 2;
}

std::string routing_id_debug_string (const zlink_routing_id_t &route_id_)
{
    return std::string (reinterpret_cast<const char *> (route_id_.data), route_id_.size);
}

bool combined_starts_with_peer_pub_route (const std::vector<zlink_msg_t> &combined_)
{
    if (combined_.empty ())
        return false;

    zlink::msg_t *kind_msg =
      reinterpret_cast<zlink::msg_t *> (const_cast<zlink_msg_t *> (&combined_[0]));
    return kind_msg->check ()
           && zlink::spot_control_protocol::is_peer_pub_route_topic (
             static_cast<const char *> (kind_msg->data ()), kind_msg->size ());
}

int send_routed_router_once (zlink::spot_runtime_t *runtime_,
                               const zlink_routing_id_t *route_id_,
                               zlink_msg_t *combined_,
                               size_t combined_count_,
                               zlink_send_flags_t flags_)
{
    if (!runtime_ || !runtime_->routed_router || !route_id_ || !combined_ || combined_count_ == 0) {
        errno = EFAULT;
        return -1;
    }
    if (route_id_->size == 0 || route_id_->size > sizeof (route_id_->data)) {
        errno = EINVAL;
        return -1;
    }

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (runtime_->routed_router);
    runtime_->routed_router->set_all_pipes_nodelay ();
    if ((flags_ & ZLINK_DONTWAIT) == 0) {
        if (zlink::wait_socket_events_internal (runtime_->routed_router, ZLINK_POLLOUT, 100)
            <= 0) {
            if (spot_direct_route_debug_enabled ()) {
                const std::string route_id = routing_id_debug_string (*route_id_);
                std::fprintf (
                  stderr, "[spot-direct] routed-router wait pollout failed errno=%d route=%s\n",
                  errno, route_id.c_str ());
            }
            errno = errno != 0 ? errno : EAGAIN;
            return -1;
        }
    }

    const int rc = zlink::logical_multipart_send_routed (
      runtime_->routed_router, route_id_, combined_, combined_count_, flags_);
    if (spot_direct_route_debug_enabled ()) {
        const std::string route_id = routing_id_debug_string (*route_id_);
        std::fprintf (stderr,
                      "[spot-direct] routed-router send rc=%d errno=%d route=%s parts=%zu\n", rc,
                      errno, route_id.c_str (), combined_count_);
    }
    return rc;
}

int send_routed_router_once (zlink::spot_runtime_t *runtime_,
                               const zlink_routing_id_t *route_id_,
                               std::vector<zlink_msg_t> *combined_,
                               zlink_send_flags_t flags_)
{
    if (!combined_ || combined_->empty ()) {
        errno = EFAULT;
        return -1;
    }
    return send_routed_router_once (runtime_, route_id_, &(*combined_)[0], combined_->size (),
                                    flags_);
}

int send_routed_router_once (zlink::spot_runtime_t *runtime_,
                               const std::string &route_id_,
                               zlink_msg_t *combined_,
                               size_t combined_count_,
                               zlink_send_flags_t flags_)
{
    zlink_routing_id_t route_id;
    if (!routing_id_from_key (route_id_, &route_id)) {
        errno = EINVAL;
        return -1;
    }
    return send_routed_router_once (runtime_, &route_id, combined_, combined_count_, flags_);
}

int send_routed_router_once (zlink::spot_runtime_t *runtime_,
                               const std::string &route_id_,
                               std::vector<zlink_msg_t> *combined_,
                               zlink_send_flags_t flags_)
{
    zlink_routing_id_t route_id;
    if (!routing_id_from_key (route_id_, &route_id)) {
        errno = EINVAL;
        return -1;
    }
    return send_routed_router_once (runtime_, &route_id, combined_, flags_);
}

int dispatch_routed_router_delivery_with_envelope (zlink::spot_node_t *origin_node_,
                                                     zlink_send_flags_t flags_,
                                                     zlink_msg_t *combined_,
                                                     size_t combined_count_,
                                                     const parsed_spot_envelope_t &envelope_)
{
    if (!origin_node_ || !combined_ || combined_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (origin_node_);
    if (!runtime || !runtime->routed_router) {
        errno = ENOTCONN;
        return -1;
    }

    if (envelope_.destination_node_rid_value.size > 0) {
        return send_routed_router_once (runtime, &envelope_.destination_node_rid_value, combined_,
                                        combined_count_, flags_);
    }

    if (envelope_.destination_class == routed_protocol::router_endpoint_class
        && envelope_.destination_endpoint_rid_value.size > 0) {
        return send_routed_router_once (runtime, &envelope_.destination_endpoint_rid_value,
                                        combined_, combined_count_, flags_);
    }

    const std::vector<std::string> candidate_route_ids =
      runtime->external_route_ids_for_destination (envelope_.destination_node_rid);
    if (candidate_route_ids.empty ()) {
        if (spot_direct_route_debug_enabled ()) {
            std::fprintf (stderr, "[spot-direct] no external route for destination=%s\n",
                          envelope_.destination_node_rid.c_str ());
        }
        errno = EHOSTUNREACH;
        return -1;
    }

    return send_routed_router_once (runtime, candidate_route_ids.front (), combined_,
                                    combined_count_, flags_);
}

int dispatch_routed_router_delivery_with_envelope (zlink::spot_node_t *origin_node_,
                                                     zlink_send_flags_t flags_,
                                                     std::vector<zlink_msg_t> *combined_,
                                                     const parsed_spot_envelope_t &envelope_)
{
    if (!combined_ || combined_->empty ()) {
        errno = EFAULT;
        return -1;
    }
    return dispatch_routed_router_delivery_with_envelope (
      origin_node_, flags_, &(*combined_)[0], combined_->size (), envelope_);
}

int dispatch_routed_router_delivery (zlink::spot_node_t *origin_node_,
                                       zlink_send_flags_t flags_,
                                       zlink_msg_t *combined_,
                                       size_t combined_count_)
{
    if (!combined_ || combined_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    zlink_routing_id_t destination_node_rid;
    if (zlink::spot_reqrep_internal::peek_spot_routed_destination_node_rid (
          combined_, combined_count_, &destination_node_rid)) {
        return send_routed_router_once (zlink::spot_node_access_t::runtime (origin_node_),
                                        &destination_node_rid, combined_, combined_count_, flags_);
    }

    parsed_spot_envelope_t envelope;
    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (combined_, combined_count_,
                                                                  &envelope)) {
        errno = EPROTO;
        return -1;
    }

    return dispatch_routed_router_delivery_with_envelope (origin_node_, flags_, combined_,
                                                          combined_count_, envelope);
}

int dispatch_routed_router_delivery (zlink::spot_node_t *origin_node_,
                                       zlink_send_flags_t flags_,
                                       std::vector<zlink_msg_t> *combined_)
{
    if (!combined_ || combined_->empty ()) {
        errno = EFAULT;
        return -1;
    }
    return dispatch_routed_router_delivery (origin_node_, flags_, &(*combined_)[0],
                                            combined_->size ());
}

bool has_local_spot_route_target (uint8_t destination_class_,
                                  const std::string &destination_node_rid_,
                                  const std::string &destination_endpoint_rid_)
{
    return destination_class_ == routed_protocol::spot_endpoint_class
             ? static_cast<bool> (
                 find_spot_state_by_identity (destination_node_rid_, destination_endpoint_rid_))
             : static_cast<bool> (find_router_state_by_rid (destination_endpoint_rid_));
}

int dispatch_local_spot_routed_delivery (routed_spot_delivery_kind_t kind_,
                                         const std::string &destination_endpoint_rid_,
                                         std::vector<zlink_msg_t> *combined_)
{
    if (kind_ == zlink::spot_reqrep_internal::routed_spot_delivery_request)
        return zlink::spot_reqrep_internal::dispatch_local_request (destination_endpoint_rid_,
                                                                    combined_);
    if (kind_ == zlink::spot_reqrep_internal::routed_spot_delivery_reply)
        return zlink::spot_reqrep_internal::dispatch_local_reply (combined_);
    return process_route_combined_for_local_delivery (combined_);
}

int dispatch_local_router_spot_delivery (router_spot_delivery_kind_t kind_,
                                         std::vector<zlink_msg_t> *combined_)
{
    if (kind_ == zlink::spot_reqrep_internal::router_spot_delivery_request)
        return zlink::spot_reqrep_internal::dispatch_local_request (std::string (), combined_);
    if (kind_ == zlink::spot_reqrep_internal::router_spot_delivery_reply)
        return zlink::spot_reqrep_internal::dispatch_local_reply (combined_);
    return process_route_combined_for_local_delivery (combined_);
}

int dispatch_router_socket_spot_delivery (void *router_,
                                          const std::string &destination_node_rid_,
                                          zlink_send_flags_t flags_,
                                          std::vector<zlink_msg_t> *combined_)
{
    if (!router_ || destination_node_rid_.empty () || !combined_ || combined_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER) {
        errno = EINVAL;
        return -1;
    }

    zlink_routing_id_t target_rid;
    if (!routing_id_from_key (destination_node_rid_, &target_rid)) {
        errno = EINVAL;
        return -1;
    }

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (handle.socket);
    handle.socket->set_all_pipes_nodelay ();
    return zlink::logical_multipart_send_routed (handle.socket, &target_rid, &(*combined_)[0],
                                                 combined_->size (), flags_);
}

bool router_has_transport_endpoint (void *router_)
{
    socket_handle_t handle = as_socket_handle (router_);
    if (!handle.socket || handle.socket->socket_type () != ZLINK_CORE_SOCKET_ROUTER)
        return false;
    if (handle.socket->socket_has_manual_connect_endpoints ())
        return true;
    std::set<std::string> bound_endpoints;
    handle.socket->socket_bound_endpoints (&bound_endpoints);
    return !bound_endpoints.empty ();
}

int process_routed_send_entry_on_data_plane (zlink::spot_runtime_t *runtime_,
                                             zlink_send_flags_t flags_,
                                             std::vector<zlink_msg_t> *combined_)
{
    if (!runtime_ || !runtime_->owner || !combined_ || combined_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    parsed_spot_envelope_t envelope;
    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (&(*combined_)[0],
                                                                  combined_->size (), &envelope)) {
        errno = EPROTO;
        return -1;
    }

    if (zlink::spot_reqrep_internal::should_process_spot_routed_locally (runtime_->owner,
                                                                         envelope)) {
        if (envelope.destination_class == zlink::spot_routed_protocol::actor_gateway_endpoint_class) {
            return zlink::spot_actor_internal::process_gateway_delivery (
              runtime_->owner, &envelope.source_node_rid_value, envelope.payload_parts,
              envelope.payload_part_count);
        }
        return process_parsed_route_combined_for_local_delivery (combined_, envelope);
    }

    return dispatch_routed_router_delivery_with_envelope (runtime_->owner, flags_, combined_,
                                                            envelope);
}

void spot_routed_router_dispatch (const zlink_routing_id_t *,
                                    zlink_msg_t *parts_,
                                    size_t part_count_,
                                    void *userdata_)
{
    if (spot_direct_route_debug_enabled ()) {
        std::fprintf (stderr, "[spot-direct] routed-router dispatch parts=%zu\n",
                      part_count_);
    }
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (userdata_);
    if (!node || !parts_ || part_count_ == 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    std::vector<zlink_msg_t> combined;
    combined.reserve (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        combined.push_back (zlink_msg_t ());
        zlink_msg_init (&combined.back ());
        if (zlink_msg_move (&combined.back (), &parts_[i]) != 0) {
            zlink::request_reply::consume_send_frames_from (parts_, i, part_count_);
            zlink::request_reply::close_built_parts (&combined);
            return;
        }
    }

    zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (node);
    if (!runtime) {
        zlink::request_reply::close_built_parts (&combined);
        return;
    }

    if (combined_starts_with_peer_pub_route (combined)) {
        if (zlink::spot_reqrep_internal::enqueue_runtime_routed_router_ingress (runtime,
                                                                                  &combined)
            != 0) {
            zlink::request_reply::close_built_parts (&combined);
        }
        return;
    }

    (void) zlink::spot_reqrep_internal::process_routed_router_combined_for_data_plane (node,
                                                                                         &combined);
}
}

extern "C" int zlink_spot_install_routed_router_dispatch (void *node_, void *socket_)
{
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    zlink::socket_base_t *socket = static_cast<zlink::socket_base_t *> (socket_);
    if (!node || !socket) {
        errno = EFAULT;
        return -1;
    }

    if (socket->socket_msg_dispatch_active ()) {
        if (spot_direct_route_debug_enabled ()) {
            std::fprintf (stderr,
                          "[spot-direct] routed-router dispatch already active socket=%d\n",
                          socket->socket_id ());
        }
        return 0;
    }

    const int rc =
      socket->socket_set_msg_handler_with_userdata (&spot_routed_router_dispatch, NULL, node);
    if (spot_direct_route_debug_enabled ()) {
        std::fprintf (stderr, "[spot-direct] routed-router dispatch install rc=%d errno=%d "
                              "socket=%d\n",
                      rc, errno, socket->socket_id ());
    }
    return rc;
}

int zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
  zlink::spot_node_t *origin_node_,
  routed_spot_delivery_kind_t kind_,
  bool local_target_,
  const std::string &destination_endpoint_rid_,
  zlink_send_flags_t flags_,
  int sndtimeo_ms_,
  std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    int rc = -1;
    if (origin_node_) {
        zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (origin_node_);
        if (runtime) {
            rc = local_target_
                   ? enqueue_runtime_routed_send (runtime, combined_, flags_, sndtimeo_ms_)
                   : dispatch_routed_router_delivery (origin_node_, flags_, combined_);
        }
    } else if (local_target_) {
        rc = dispatch_local_spot_routed_delivery (kind_, destination_endpoint_rid_, combined_);
    }
    return rc;
}

int zlink::spot_reqrep_internal::dispatch_spot_routed_delivery_direct (
  zlink::spot_node_t *origin_node_,
  zlink_send_flags_t flags_,
  zlink_msg_t *combined_,
  size_t combined_count_)
{
    return dispatch_routed_router_delivery (origin_node_, flags_, combined_, combined_count_);
}

int zlink::spot_reqrep_internal::dispatch_router_spot_delivery (
  void *router_,
  const std::string &destination_node_rid_,
  const std::string &destination_spot_rid_,
  router_spot_delivery_kind_t kind_,
  zlink_send_flags_t flags_,
  int sndtimeo_ms_,
  std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    int rc =
      dispatch_router_socket_spot_delivery (router_, destination_node_rid_, flags_, combined_);
    const bool has_router_transport = router_has_transport_endpoint (router_);
    if (rc != 0 && !has_router_transport
        && (errno == ENOTCONN || errno == EHOSTUNREACH || errno == EAGAIN || errno == EINVAL
            || errno == EFAULT)) {
        zlink::spot_runtime_t *runtime =
          resolve_runtime_for_spot_destination (destination_node_rid_, destination_spot_rid_);
        rc = runtime ? enqueue_runtime_routed_send (runtime, combined_, flags_, sndtimeo_ms_)
                     : dispatch_local_router_spot_delivery (kind_, combined_);
    }
    if (rc != 0 && !has_router_transport && errno != ENOTCONN && errno != EHOSTUNREACH
        && errno != EAGAIN) {
        rc = dispatch_local_router_spot_delivery (kind_, combined_);
    }
    return rc;
}

int zlink::spot_reqrep_internal::enqueue_runtime_routed_send (zlink::spot_runtime_t *runtime_,
                                                              std::vector<zlink_msg_t> *parts_,
                                                              zlink_send_flags_t flags_,
                                                              int sndtimeo_ms_)
{
    if (!runtime_ || !parts_ || parts_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::spot_data_plane_runtime_state_t::routed_send_queue_t &queue =
      runtime_->execution.data_plane_state.routed_send;
    const int hwm = spot_node_router_admission_hwm (runtime_->runtime_tuning_snapshot ());
    const size_t entry_bytes = routed_parts_encoded_bytes (*parts_);
    const size_t message_limit = static_cast<size_t> (hwm > 0 ? hwm : 1);
    const size_t byte_limit =
      hwm == 0 ? 0
               : (message_limit > SIZE_MAX / entry_bytes ? SIZE_MAX : message_limit * entry_bytes);
    std::unique_lock<std::mutex> lock (queue.mutex);
    if (!routed_queue_has_room (queue, hwm, byte_limit, entry_bytes))
        queue.backpressure_active = true;
    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    if (dontwait || sndtimeo_ms_ == 0) {
        if (!queue.closed && !routed_queue_has_room (queue, hwm, byte_limit, entry_bytes)) {
            errno = EAGAIN;
            return -1;
        }
    } else if (sndtimeo_ms_ < 0) {
        while (!queue.closed && !routed_queue_has_room (queue, hwm, byte_limit, entry_bytes))
            queue.cv.wait (lock);
    } else {
        const std::chrono::steady_clock::time_point deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (sndtimeo_ms_);
        while (!queue.closed && !routed_queue_has_room (queue, hwm, byte_limit, entry_bytes)) {
            if (queue.cv.wait_until (lock, deadline) == std::cv_status::timeout
                && !routed_queue_has_room (queue, hwm, byte_limit, entry_bytes)) {
                errno = EAGAIN;
                return -1;
            }
        }
    }
    if (queue.closed) {
        errno = ESHUTDOWN;
        return -1;
    }

    zlink::spot_data_plane_runtime_state_t::routed_send_entry_t entry;
    entry.flags = flags_;
    entry.parts.swap (*parts_);
    queue.queued_bytes += entry_bytes;
    queue.messages.push_back (std::move (entry));
    if (!routed_queue_has_room (queue, hwm, byte_limit, 1))
        queue.backpressure_active = true;
    queue.retry_after_ms = 0;
    if (!queue.signal_armed && queue.signaler.valid ()) {
        queue.signal_armed = true;
        queue.signaler.send ();
    }
    return 0;
}

int zlink::spot_reqrep_internal::drain_runtime_routed_send_queue (zlink::spot_runtime_t *runtime_)
{
    if (!runtime_)
        return 0;

    std::deque<zlink::spot_data_plane_runtime_state_t::routed_send_entry_t> local;
    bool notify_recovery = false;
    {
        zlink::spot_data_plane_runtime_state_t::routed_send_queue_t &queue =
          runtime_->execution.data_plane_state.routed_send;
        std::lock_guard<std::mutex> lock (queue.mutex);
        if (queue.retry_after_ms != 0 && zlink::clock_t ().now_ms () < queue.retry_after_ms)
            return 0;
        queue.retry_after_ms = 0;
        size_t moved_bytes = 0;
        size_t moved_count = 0;
        while (!queue.messages.empty ()) {
            const size_t entry_bytes = routed_parts_encoded_bytes (queue.messages.front ().parts);
            if (moved_count > 0
                && (moved_count >= routed_send_drain_batch_limit
                    || moved_bytes + entry_bytes > routed_send_drain_batch_bytes_limit))
                break;
            moved_bytes += entry_bytes;
            local.push_back (std::move (queue.messages.front ()));
            queue.messages.pop_front ();
            ++moved_count;
        }
        queue.queued_bytes =
          queue.queued_bytes > moved_bytes ? queue.queued_bytes - moved_bytes : 0;
        const int hwm = spot_node_router_admission_hwm (runtime_->runtime_tuning_snapshot ());
        if (queue.backpressure_active && routed_queue_can_resume (queue, hwm, 1)) {
            queue.backpressure_active = false;
            notify_recovery = true;
        }
        if (!queue.messages.empty () && !queue.signal_armed && queue.signaler.valid ()) {
            queue.signal_armed = true;
            queue.signaler.send ();
        }
        queue.cv.notify_all ();
    }
    if (notify_recovery)
        notify_spot_send_ready_recovery (runtime_->owner);

    while (!local.empty ()) {
        zlink::spot_data_plane_runtime_state_t::routed_send_entry_t &entry = local.front ();
        const int rc =
          process_routed_send_entry_on_data_plane (runtime_, entry.flags, &entry.parts);
        const int saved_errno = errno;
        if (rc != 0) {
            if (is_queue_drain_retry_error (saved_errno)) {
                std::deque<zlink::spot_data_plane_runtime_state_t::routed_send_entry_t> retry;
                retry.push_back (std::move (entry));
                local.pop_front ();
                while (!local.empty ()) {
                    retry.push_back (std::move (local.front ()));
                    local.pop_front ();
                }
                zlink::spot_data_plane_runtime_state_t::routed_send_queue_t &queue =
                  runtime_->execution.data_plane_state.routed_send;
                std::lock_guard<std::mutex> lock (queue.mutex);
                if (!queue.closed) {
                    while (!retry.empty ()) {
                        queue.queued_bytes += routed_parts_encoded_bytes (retry.back ().parts);
                        queue.messages.push_front (std::move (retry.back ()));
                        retry.pop_back ();
                    }
                    queue.retry_after_ms = routed_send_retry_deadline_ms ();
                    queue.cv.notify_all ();
                    return 0;
                }
                while (!retry.empty ()) {
                    zlink::request_reply::close_built_parts (&retry.front ().parts);
                    retry.pop_front ();
                }
                errno = ESHUTDOWN;
                return -1;
            }
            zlink::request_reply::close_built_parts (&entry.parts);
            local.pop_front ();
            errno = saved_errno;
            return -1;
        }
        zlink::request_reply::close_built_parts (&entry.parts);
        local.pop_front ();
    }
    return 0;
}

int zlink::spot_reqrep_internal::enqueue_runtime_routed_router_ingress (
  zlink::spot_runtime_t *runtime_, std::vector<zlink_msg_t> *parts_)
{
    if (!runtime_ || !parts_ || parts_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::spot_data_plane_runtime_state_t::routed_router_ingress_queue_t &queue =
      runtime_->execution.data_plane_state.routed_router_ingress;
    std::lock_guard<std::mutex> lock (queue.mutex);
    if (queue.closed) {
        errno = ESHUTDOWN;
        return -1;
    }

    zlink::spot_data_plane_runtime_state_t::routed_router_ingress_entry_t entry;
    entry.parts.swap (*parts_);
    queue.messages.push_back (std::move (entry));
    if (!queue.signal_armed && queue.signaler.valid ()) {
        queue.signal_armed = true;
        queue.signaler.send ();
    }
    return 0;
}

int zlink::spot_reqrep_internal::drain_runtime_routed_router_ingress_queue (
  zlink::spot_runtime_t *runtime_)
{
    if (!runtime_)
        return 0;

    std::deque<zlink::spot_data_plane_runtime_state_t::routed_router_ingress_entry_t> local;
    {
        zlink::spot_data_plane_runtime_state_t::routed_router_ingress_queue_t &queue =
          runtime_->execution.data_plane_state.routed_router_ingress;
        std::lock_guard<std::mutex> lock (queue.mutex);
        local.swap (queue.messages);
    }

    while (!local.empty ()) {
        zlink::spot_data_plane_runtime_state_t::routed_router_ingress_entry_t &entry =
          local.front ();
        const int rc =
          process_routed_router_combined_for_data_plane (runtime_->owner, &entry.parts);
        const int saved_errno = errno;
        if (rc != 0) {
            zlink::request_reply::close_built_parts (&entry.parts);
            local.pop_front ();
            errno = saved_errno;
            return -1;
        }
        local.pop_front ();
    }
    return 0;
}
