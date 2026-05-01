/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <atomic>
#include <string>
#include <vector>

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_message_parts_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_runtime.hpp"
#include "sockets/socket_base.hpp"
#include "core/recv_internal.hpp"

namespace
{
bool spot_direct_route_debug_enabled ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE") != NULL;
}

int process_route_combined_message (void *node_,
                                    zlink::socket_base_t *socket,
                                    std::vector<zlink_msg_t> &combined)
{
    if (spot_direct_route_debug_enabled ()) {
        std::fprintf (stderr,
                      "[spot-direct] internal-router recv parts=%zu socket=%d\n",
                      combined.size (),
                      socket->socket_id ());
    }

    if (!combined.empty ()) {
        zlink::msg_t *kind_msg =
          reinterpret_cast<zlink::msg_t *> (&combined[0]);
        if (kind_msg->check ()
            && zlink::spot_control_protocol::is_peer_pub_route_topic (
              static_cast<const char *> (kind_msg->data ()),
              kind_msg->size ())) {
            zlink::spot_node_t *node =
              static_cast<zlink::spot_node_t *> (node_);
            zlink::spot_runtime_t *runtime =
              zlink::spot_node_access_t::runtime (node);
            if (!runtime || combined.size () < 2) {
                zlink::request_reply::close_built_parts (&combined);
                errno = EPROTO;
                return -1;
            }

            zlink::msg_t *topic_msg =
              reinterpret_cast<zlink::msg_t *> (&combined[1]);
            std::string topic (
              static_cast<const char *> (topic_msg->data ()),
              topic_msg->size ());
            zlink::spot_owned_msg_parts_t payload;
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
                  stderr,
                  "[spot-direct] ingress peer-pub topic=%s rc=%d errno=%d targets=%zu\n",
                  topic.c_str (),
                  rc,
                  errno,
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

    if (zlink::spot_reqrep_internal::should_process_spot_routed_locally (
          static_cast<zlink::spot_node_t *> (node_), spot_envelope)) {
        rc = zlink::spot_reqrep_internal::
          process_parsed_route_combined_for_local_delivery (&combined,
                                                            spot_envelope);
    } else {
        if (spot_direct_route_debug_enabled ()) {
            std::string local_node_rid;
            (void) zlink::spot_reqrep_internal::resolve_spot_node_routing_id (
              static_cast<zlink::spot_node_t *> (node_), &local_node_rid);
            std::fprintf (
              stderr,
              "[spot-direct] drop non-local routed envelope on external router node=%s dest_node=%s dest_endpoint=%s\n",
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
    return 1;
}

int recv_combined_external_router_message (zlink::socket_base_t *socket_,
                                      std::vector<zlink_msg_t> *out_)
{
    if (!socket_ || !out_) {
        errno = EFAULT;
        return -1;
    }

    out_->clear ();

    zlink_msg_t first;
    zlink_msg_init (&first);
    zlink_routing_id_t source_rid;
    memset (&source_rid, 0, sizeof (source_rid));
    if (zlink::recv_msg_routed_socket (socket_, &first, &source_rid,
                                       ZLINK_DONTWAIT)
        != 0) {
        zlink_msg_close (&first);
        return -1;
    }

    out_->push_back (first);
    while (!out_->empty ()
           && zlink::msg_frame_has_more (out_->back ())) {
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

extern "C" int zlink_spot_process_internal_router (void *node_, void *socket_)
{
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (socket_);
    if (!socket) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::vector<zlink_msg_t> combined;
        if (zlink::spot_reqrep_internal::recv_combined_router_message (
              socket, &combined)
            != 0) {
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
                          "[spot-direct] internal-router recv eagain socket=%d peer=%s\n",
                          socket->socket_id (),
                          remote_endpoints.front ().c_str ());
                    }
                }
            }
            if (spot_direct_route_debug_enabled () && errno != EAGAIN) {
                std::fprintf (stderr,
                              "[spot-direct] internal-router recv failed errno=%d socket=%d\n",
                              errno,
                              socket->socket_id ());
            }
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        const int processed = process_route_combined_message (node_, socket, combined);
        if (processed < 0)
            return -1;
    }
}

extern "C" int zlink_spot_process_external_router (void *node_, void *socket_)
{
    zlink::socket_base_t *socket =
      static_cast<zlink::socket_base_t *> (socket_);
    if (!socket) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::vector<zlink_msg_t> combined;
        if (recv_combined_external_router_message (socket, &combined) != 0) {
            if (errno == EAGAIN)
                return 0;
            return -1;
        }

        if (process_route_combined_message (node_, socket, combined) < 0)
            return -1;
    }
}
