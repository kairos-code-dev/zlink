/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <cstdio>
#include <vector>

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "core/multipart_send_txn.hpp"
#include "core/recv_internal.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_message_parts_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"

namespace
{
using zlink::spot_reqrep_internal::find_router_state_by_rid;
using zlink::spot_reqrep_internal::find_spot_state_by_identity;
using zlink::spot_reqrep_internal::enqueue_runtime_internal_router_once;
using zlink::spot_reqrep_internal::parsed_spot_envelope_t;
using zlink::spot_reqrep_internal::process_parsed_route_combined_for_local_delivery;
using zlink::spot_reqrep_internal::process_route_combined_for_local_delivery;
using zlink::spot_reqrep_internal::resolve_runtime_for_spot_destination;
using zlink::spot_reqrep_internal::routed_spot_delivery_kind_t;
using zlink::spot_reqrep_internal::router_spot_delivery_kind_t;

enum : uint8_t
{
    zmp_spot_class = 0x01,
    zmp_router_class = 0x02
};

bool spot_direct_route_debug_enabled ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE") != NULL;
}

bool routing_id_from_string (const std::string &value_,
                             zlink_routing_id_t *out_)
{
    if (!out_ || value_.empty () || value_.size () > sizeof (out_->data))
        return false;
    memset (out_, 0, sizeof (*out_));
    out_->size = static_cast<uint8_t> (value_.size ());
    memcpy (out_->data, value_.data (), value_.size ());
    return true;
}

int send_external_router_once (zlink::spot_runtime_t *runtime_,
                               const std::string &route_id_,
                               std::vector<zlink_msg_t> *combined_,
                               zlink_send_flags_t flags_)
{
    if (!runtime_ || !runtime_->external_router || !combined_) {
        errno = EFAULT;
        return -1;
    }

    zlink_routing_id_t route_id;
    if (!routing_id_from_string (route_id_, &route_id)) {
        errno = EINVAL;
        return -1;
    }

    zlink::spot_data_plane_forwarder_t::pump_socket_commands (
      runtime_->external_router);
    runtime_->external_router->set_all_pipes_nodelay ();
    const long wait_timeout_ms = (flags_ & ZLINK_DONTWAIT) != 0 ? 0 : 100;
    if (zlink::wait_socket_events_internal (runtime_->external_router,
                                            ZLINK_POLLOUT, wait_timeout_ms)
        <= 0) {
        errno = errno != 0 ? errno : EAGAIN;
        return -1;
    }

    return zlink::logical_multipart_send_routed (
      runtime_->external_router, &route_id, &(*combined_)[0],
      combined_->size (), flags_ | ZLINK_DONTWAIT);
}

int dispatch_external_router_delivery (zlink::spot_node_t *origin_node_,
                                       zlink_send_flags_t flags_,
                                       std::vector<zlink_msg_t> *combined_)
{
    if (!origin_node_ || !combined_ || combined_->empty ()) {
        errno = EFAULT;
        return -1;
    }

    zlink::spot_runtime_t *runtime =
      zlink::spot_node_access_t::runtime (origin_node_);
    if (!runtime || !runtime->external_router) {
        errno = ENOTCONN;
        return -1;
    }

    parsed_spot_envelope_t envelope;
    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (
          &(*combined_)[0], combined_->size (), &envelope)) {
        errno = EPROTO;
        return -1;
    }

    const std::vector<std::string> candidate_route_ids =
      runtime->external_route_ids_for_destination (envelope.destination_node_rid);

    if (candidate_route_ids.empty ()) {
        errno = EHOSTUNREACH;
        return -1;
    }

    return send_external_router_once (runtime, candidate_route_ids.front (),
                                      combined_, flags_);
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

int dispatch_local_spot_routed_delivery (
  routed_spot_delivery_kind_t kind_,
  const std::string &destination_endpoint_rid_,
  std::vector<zlink_msg_t> *combined_)
{
    if (kind_ == zlink::spot_reqrep_internal::routed_spot_delivery_request)
        return zlink::spot_reqrep_internal::dispatch_local_request (
          destination_endpoint_rid_, combined_);
    if (kind_ == zlink::spot_reqrep_internal::routed_spot_delivery_reply)
        return zlink::spot_reqrep_internal::dispatch_local_reply (combined_);
    return process_route_combined_for_local_delivery (combined_);
}

int dispatch_local_router_spot_delivery (
  router_spot_delivery_kind_t kind_,
  std::vector<zlink_msg_t> *combined_)
{
    if (kind_ == zlink::spot_reqrep_internal::router_spot_delivery_request)
        return zlink::spot_reqrep_internal::dispatch_local_request (
          std::string (), combined_);
    if (kind_ == zlink::spot_reqrep_internal::router_spot_delivery_reply)
        return zlink::spot_reqrep_internal::dispatch_local_reply (combined_);
    return process_route_combined_for_local_delivery (combined_);
}

int dispatch_remote_spot_routed_delivery (
  zlink::spot_node_t *origin_node_,
  zlink_send_flags_t flags_,
  std::vector<zlink_msg_t> *combined_)
{
    if (!origin_node_ || !combined_) {
        errno = EFAULT;
        return -1;
    }

    const int rc =
      dispatch_external_router_delivery (origin_node_, flags_, combined_);
    if (spot_direct_route_debug_enabled ()) {
        std::fprintf (stderr,
                      "[spot-direct] external-router route rc=%d errno=%d\n",
                      rc,
                      errno);
    }
    return rc;
}

void spot_external_router_dispatch (const zlink_routing_id_t *,
                               zlink_msg_t *parts_,
                               size_t part_count_,
                               void *userdata_)
{
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (userdata_);
    if (!node || !parts_ || part_count_ == 0) {
        zlink::request_reply::close_request_reply_parts (parts_, part_count_);
        return;
    }

    zlink::msg_t *kind_msg = reinterpret_cast<zlink::msg_t *> (&parts_[0]);
    if (kind_msg->check ()
        && zlink::spot_control_protocol::is_peer_pub_route_topic (
          static_cast<const char *> (kind_msg->data ()), kind_msg->size ())) {
        if (part_count_ < 2) {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
            return;
        }

        zlink::msg_t *topic_msg = reinterpret_cast<zlink::msg_t *> (&parts_[1]);
        if (!topic_msg->check ()) {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
            return;
        }

        std::string topic (static_cast<const char *> (topic_msg->data ()),
                           topic_msg->size ());
        zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (node);
        if (!runtime) {
            zlink::request_reply::close_request_reply_parts (parts_, part_count_);
            return;
        }

        if (spot_direct_route_debug_enabled ()) {
            std::fprintf (stderr,
                          "[spot-direct] peer-pub dispatch topic=%s parts=%zu node=%p\n",
                          topic.c_str (),
                          part_count_ > 2 ? part_count_ - 2 : 0,
                          static_cast<void *> (node));
        }

        zlink::spot_owned_msg_parts_t payload;
        for (size_t i = 2; i < part_count_; ++i) {
            payload.push_back (zlink_msg_t ());
            zlink_msg_init (&payload.back ());
            if (zlink_msg_move (&payload.back (), &parts_[i]) != 0) {
                zlink::request_reply::consume_send_frames_from (parts_, i,
                                                                part_count_);
                zlink::spot_clear_msg_parts (&payload);
                zlink::request_reply::close_request_reply_parts (parts_, 2);
                return;
            }
        }

        zlink::request_reply::close_request_reply_parts (parts_, 2);
        const int rc = zlink::spot_data_plane_forwarder_t::forward_local_fanout (
          runtime, &runtime->execution.data_plane_state, topic, payload);
        if (spot_direct_route_debug_enabled ()) {
            std::fprintf (stderr,
                          "[spot-direct] peer-pub forward rc=%d errno=%d targets=%zu\n",
                          rc,
                          errno,
                          runtime->execution.data_plane_state.local_fanout.targets.size ());
        }
        zlink::spot_clear_msg_parts (&payload);
        return;
    }

    std::vector<zlink_msg_t> combined;
    combined.reserve (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        combined.push_back (zlink_msg_t ());
        zlink_msg_init (&combined.back ());
        if (zlink_msg_move (&combined.back (), &parts_[i]) != 0) {
            zlink::request_reply::consume_send_frames_from (parts_, i,
                                                            part_count_);
            zlink::request_reply::close_built_parts (&combined);
            return;
        }
    }

    parsed_spot_envelope_t spot_envelope;
    int rc = -1;
    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (
          &combined[0], combined.size (), &spot_envelope)) {
        if (spot_direct_route_debug_enabled ()) {
            std::fprintf (
              stderr,
              "[spot-direct] dispatch parse failed parts=%zu\n",
              combined.size ());
        }
        zlink::request_reply::close_built_parts (&combined);
        return;
    }

    if (zlink::spot_reqrep_internal::should_process_spot_routed_locally (
          node, spot_envelope)) {
        rc = process_parsed_route_combined_for_local_delivery (&combined,
                                                               spot_envelope);
    } else {
        rc = 0;
    }

    const int saved_errno = errno;
    zlink::request_reply::close_built_parts (&combined);
    errno = saved_errno;
    LIBZLINK_UNUSED (rc);
}
}

extern "C" int zlink_spot_install_external_router_dispatch (void *node_,
                                                        void *socket_)
{
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (socket_);
    if (!node || !socket) {
        errno = EFAULT;
        return -1;
    }

    if (socket->socket_msg_dispatch_active ())
        return 0;

    return socket->socket_set_msg_handler_with_userdata (
      &spot_external_router_dispatch, NULL, node);
}

int zlink::spot_reqrep_internal::dispatch_spot_routed_delivery (
  zlink::spot_node_t *origin_node_,
  routed_spot_delivery_kind_t kind_,
  bool local_target_,
  const std::string &destination_endpoint_rid_,
  zlink_send_flags_t flags_,
  std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    int rc = -1;
    if (local_target_) {
        rc = dispatch_local_spot_routed_delivery (
          kind_, destination_endpoint_rid_, combined_);
    } else if (origin_node_) {
        rc = dispatch_remote_spot_routed_delivery (
          origin_node_, flags_, combined_);
    }
    return rc;
}

int zlink::spot_reqrep_internal::dispatch_router_spot_delivery (
  const std::string &destination_node_rid_,
  const std::string &destination_spot_rid_,
  router_spot_delivery_kind_t kind_,
  zlink_send_flags_t flags_,
  std::vector<zlink_msg_t> *combined_)
{
    if (!combined_) {
        errno = EFAULT;
        return -1;
    }

    const bool local_target = has_local_spot_route_target (
      zmp_spot_class, destination_node_rid_, destination_spot_rid_);
    zlink::spot_runtime_t *runtime =
      local_target ? NULL
                   : resolve_runtime_for_spot_destination (
                       destination_node_rid_, destination_spot_rid_);
    int rc =
      local_target
        ? dispatch_local_router_spot_delivery (kind_, combined_)
        : (runtime
             ? enqueue_runtime_internal_router_once (runtime, combined_, flags_)
             : -1);
    if (rc != 0 && !local_target
        && errno != ENOTCONN && errno != EHOSTUNREACH && errno != EAGAIN) {
        rc = dispatch_local_router_spot_delivery (kind_, combined_);
    }
    return rc;
}
