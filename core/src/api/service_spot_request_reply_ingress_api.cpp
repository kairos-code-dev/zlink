/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include <atomic>
#include <string>
#include <vector>

#include "api/request_reply_protocol_internal.hpp"
#include "api/service_spot_request_reply_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "sockets/socket_base.hpp"

namespace
{
bool spot_direct_route_debug_enabled ()
{
    return std::getenv ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE") != NULL;
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
