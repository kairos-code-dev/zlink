/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <atomic>
#include <string>
#include <vector>

#include "api/socket/request_reply_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_routed_protocol_internal.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "services/actor/service_spot_actor_internal.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "sockets/common/socket_base.hpp"
#include "core/recv_internal.hpp"
#include "utils/debug_log.hpp"

#ifndef ZLINK_INTERNAL_EXPORT
#if defined _WIN32 && defined DLL_EXPORT && !defined ZLINK_STATIC
#define ZLINK_INTERNAL_EXPORT __declspec (dllexport)
#else
#define ZLINK_INTERNAL_EXPORT
#endif
#endif

namespace
{
const bool spot_direct_route_debug_on = zlink::debug_env_enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE");

bool spot_direct_route_debug_enabled ()
{
    return spot_direct_route_debug_on;
}

int process_route_combined_message (void *node_,
                                    zlink::socket_base_t *socket,
                                    std::vector<zlink_msg_t> &combined)
{
    if (spot_direct_route_debug_enabled ()) {
        std::fprintf (stderr, "[spot-direct] routed recv parts=%zu socket=%d\n", combined.size (),
                      socket ? socket->socket_id () : -1);
    }

    if (!combined.empty ()) {
        zlink::msg_t *kind_msg = reinterpret_cast<zlink::msg_t *> (&combined[0]);
        if (kind_msg->check ()
            && zlink::spot_control_protocol::is_peer_pub_route_topic (
              static_cast<const char *> (kind_msg->data ()), kind_msg->size ())) {
            zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
            zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (node);
            if (!runtime || combined.size () < 2) {
                zlink::request_reply::close_built_parts (&combined);
                errno = EPROTO;
                return -1;
            }

            zlink::msg_t *topic_msg = reinterpret_cast<zlink::msg_t *> (&combined[1]);
            std::string topic (static_cast<const char *> (topic_msg->data ()), topic_msg->size ());
            zlink::spot_owned_msg_parts_t payload;
            payload.reserve (combined.size () - 2);
            for (size_t i = 2; i < combined.size (); ++i) {
                payload.push_back (zlink_msg_t ());
                zlink_msg_init (&payload.back ());
                if (zlink_msg_move (&payload.back (), &combined[i]) != 0) {
                    zlink::request_reply::close_built_parts (&combined);
                    zlink::spot_clear_msg_parts (&payload);
                    return -1;
                }
            }

            zlink::request_reply::close_built_parts (&combined);
            const int rc = zlink::spot_data_plane_forwarder_t::forward_local_fanout (
              runtime, &runtime->execution.data_plane_state, topic, payload);
            if (spot_direct_route_debug_enabled ()) {
                std::fprintf (
                  stderr, "[spot-direct] ingress peer-pub topic=%s rc=%d errno=%d targets=%zu\n",
                  topic.c_str (), rc, errno,
                  runtime->execution.data_plane_state.local_fanout.targets.size ());
            }
            const int saved_errno = errno;
            zlink::spot_clear_msg_parts (&payload);
            if (rc != 0) {
                errno = saved_errno;
                return -1;
            }
            return 1;
        }
    }

    zlink::spot_reqrep_internal::parsed_spot_envelope_t spot_envelope;
    int rc = -1;
    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (&combined[0], combined.size (),
                                                                  &spot_envelope)) {
        const int saved_errno = errno != 0 ? errno : EPROTO;
        if (spot_direct_route_debug_enabled ()) {
            std::fprintf (stderr,
                          "[spot-direct] ingress parse failed errno=%d parts=%zu socket=%d\n",
                          saved_errno, combined.size (), socket ? socket->socket_id () : -1);
        }
        zlink::request_reply::close_built_parts (&combined);
        errno = saved_errno;
        return -1;
    }

    if (zlink::spot_reqrep_internal::should_process_spot_routed_locally (
          static_cast<zlink::spot_node_t *> (node_), spot_envelope)) {
        if (spot_envelope.destination_class
            == zlink::spot_routed_protocol::actor_gateway_endpoint_class) {
            rc = zlink::spot_actor_internal::process_gateway_delivery (
              node_, &spot_envelope.source_node_rid_value, spot_envelope.payload_parts,
              spot_envelope.payload_part_count);
        } else {
            rc = zlink::spot_reqrep_internal::process_parsed_route_combined_for_local_delivery (
              &combined, spot_envelope);
        }
    } else {
        if (spot_direct_route_debug_enabled ()) {
            std::string local_node_rid;
            (void) zlink::spot_reqrep_internal::resolve_spot_node_routing_id (
              static_cast<zlink::spot_node_t *> (node_), &local_node_rid);
            std::fprintf (stderr,
                          "[spot-direct] drop non-local routed envelope on routed router node=%s "
                          "dest_node=%s dest_endpoint=%s\n",
                          local_node_rid.c_str (), spot_envelope.destination_node_rid.c_str (),
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
    return 1;
}

int recv_combined_routed_router_message (zlink::socket_base_t *socket_,
                                           std::vector<zlink_msg_t> *out_)
{
    if (!socket_ || !out_) {
        errno = EFAULT;
        return -1;
    }

    out_->clear ();
    if (out_->capacity () == 0)
        out_->reserve (4);

    zlink_msg_t first;
    zlink_msg_init (&first);
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    if (zlink::recv_msg_routed_socket (socket_, &first, &source_rid, ZLINK_DONTWAIT) != 0) {
        zlink_msg_close (&first);
        return -1;
    }

    out_->push_back (first);
    while (!out_->empty () && zlink::msg_frame_has_more (out_->back ())) {
        zlink_msg_t next;
        zlink_msg_init (&next);
        if (zlink::internal_pair_queue::recv_followup_with_retry (socket_, &next, ZLINK_DONTWAIT)
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

int zlink::spot_reqrep_internal::process_routed_router_combined_for_data_plane (
  zlink::spot_node_t *node_, std::vector<zlink_msg_t> *combined_)
{
    if (!node_ || !combined_) {
        errno = EFAULT;
        return -1;
    }

    const int processed = process_route_combined_message (node_, NULL, *combined_);
    return processed < 0 ? -1 : 0;
}

extern "C" int zlink_spot_process_routed_router (void *node_, void *socket_)
{
    zlink::socket_base_t *socket = static_cast<zlink::socket_base_t *> (socket_);
    if (!socket) {
        errno = EFAULT;
        return -1;
    }

    std::vector<zlink_msg_t> combined;
    while (true) {
        if (recv_combined_routed_router_message (socket, &combined) != 0) {
            if (spot_direct_route_debug_enabled () && errno == EAGAIN) {
                static std::atomic<int> g_external_eagain_logs (0);
                if (g_external_eagain_logs.fetch_add (1, std::memory_order_acq_rel) < 64) {
                    std::fprintf (stderr, "[spot-direct] routed-router recv eagain socket=%d\n",
                                  socket->socket_id ());
                }
            }
            if (spot_direct_route_debug_enabled () && errno != EAGAIN) {
                std::fprintf (stderr,
                              "[spot-direct] routed-router recv failed errno=%d socket=%d\n",
                              errno, socket->socket_id ());
            }
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        if (spot_direct_route_debug_enabled ()) {
            std::fprintf (stderr, "[spot-direct] routed-router recv ok socket=%d parts=%zu\n",
                          socket->socket_id (), combined.size ());
        }
        if (process_route_combined_message (node_, socket, combined) < 0)
            return -1;
    }
}

extern "C" ZLINK_INTERNAL_EXPORT int zlink_spot_drain_routed_router_ingress (void *node_)
{
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (node_);
    zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (node);
    if (!node || !runtime) {
        errno = EFAULT;
        return -1;
    }
    zlink::spot_data_plane_forwarder_t::pump_socket_commands (
      runtime->execution.data_plane_state.routed_router);
    runtime->execution.data_plane_state.routed_router->socket_msg_dispatch_drain_pending ();
    if (zlink::spot_reqrep_internal::drain_runtime_routed_router_ingress_queue (runtime) != 0) {
        return -1;
    }
    if (runtime->execution.data_plane_state.routed_router->socket_msg_dispatch_active ())
        return 0;
    return zlink_spot_process_routed_router (
      node_, runtime->execution.data_plane_state.routed_router);
}

extern "C" ZLINK_INTERNAL_EXPORT int zlink_spot_try_process_routed_router_parts (void *node_,
                                                                                 zlink_msg_t *parts_,
                                                                                 size_t part_count_,
                                                                                 int *processed_out_)
{
    if (processed_out_)
        *processed_out_ = 0;
    if (!node_ || !parts_ || part_count_ == 0 || !processed_out_) {
        errno = EFAULT;
        return -1;
    }

    zlink::spot_reqrep_internal::parsed_spot_envelope_t envelope;
    if (!zlink::spot_reqrep_internal::parse_spot_routed_envelope (parts_, part_count_,
                                                                  &envelope)) {
        errno = 0;
        return 0;
    }

    std::vector<zlink_msg_t> combined;
    combined.reserve (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        combined.push_back (zlink_msg_t ());
        zlink_msg_init (&combined.back ());
        if (zlink_msg_move (&combined.back (), &parts_[i]) != 0) {
            zlink::request_reply::close_built_parts (&combined);
            return -1;
        }
    }

    *processed_out_ = 1;
    return zlink::spot_reqrep_internal::process_routed_router_combined_for_data_plane (
      static_cast<zlink::spot_node_t *> (node_), &combined);
}
