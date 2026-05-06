/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_subject_access.hpp"

#include "api/service_handle_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "api/socket_message_api_internal.hpp"

#include <string>

int spot_subject_publish (void *subject_,
                          const char *topic_id_,
                          zlink_msg_t *parts_,
                          size_t part_count_,
                          zlink_send_flags_t flags_)
{
    if (!topic_id_) {
        errno = EINVAL;
        return -1;
    }

    if (zlink::spot_pub_t *pub = as_spot_pub_side_handle (subject_))
        return pub->publish (topic_id_, parts_, part_count_, flags_);

    if (spot_handle_t *spot = as_spot_handle (subject_)) {
        if (zlink::spot_node_access_t::is_shutting_down (spot->node)) {
            errno = ESHUTDOWN;
            return -1;
        }
        zlink::service_public_api_scope_t admission (spot->public_api);
        if (!admission.acquired ())
            return -1;
        std::string service_name =
          zlink::spot_node_access_t::summary_service_name (spot->node);
        zlink::socket_base_t *service_pub =
          zlink::spot_node_access_t::service_pub_socket (spot->node,
                                                         service_name.c_str ());
        zlink::socket_base_t *node_pub = NULL;
        zlink::spot_runtime_t *runtime =
          zlink::spot_node_access_t::runtime (spot->node);
        zlink_spot_node_status_t status;
        memset (&status, 0, sizeof (status));
        const bool node_has_transport_surface =
          zlink::spot_node_access_t::status_snapshot (spot->node, &status) == 0
          && (status.local_endpoint[0] != '\0'
              || status.configured_peer_count != 0
              || status.active_peer_count != 0
              || status.connected_peer_count != 0);
        const bool use_transport = service_pub || node_has_transport_surface;
        if (use_transport && !service_pub) {
            if (!runtime
                || runtime->ensure_sender_socket (
                     zlink::spot_runtime_sender_pub_ingress, &node_pub)
                     != 0) {
                const int publish_errno = errno;
                zlink_multipart_close (parts_, part_count_);
                errno = publish_errno;
                return -1;
            }
        }
        if (service_pub
            && zlink_socket_publish_internal (service_pub, topic_id_, parts_,
                                              part_count_, flags_)
                 != 0) {
            const int publish_errno = errno;
            errno = publish_errno;
            return -1;
        }
        if (!service_pub && node_pub) {
            zlink_msg_t empty_part;
            zlink_msg_t *send_parts = parts_;
            size_t send_part_count = part_count_;
            if (part_count_ == 0) {
                zlink_msg_init (&empty_part);
                if (zlink_msg_init_size (&empty_part, 0) != 0) {
                    const int publish_errno = errno;
                    return -1;
                }
                send_parts = &empty_part;
                send_part_count = 1;
            }
            const int publish_rc = zlink_socket_publish_internal (
              node_pub, topic_id_, send_parts, send_part_count, flags_);
            const int publish_errno = errno;
            if (publish_rc != 0) {
                errno = publish_errno;
                return -1;
            }
        }
        if (!use_transport) {
            zlink_routing_id_t source_rid;
            memset (&source_rid, 0, sizeof (source_rid));
            (void) spot->node->node_routing_id (&source_rid);
            const int fanout_rc = spot->node->fanout_local_publish (
              service_name.c_str (), &source_rid, topic_id_, parts_,
              part_count_);
            const int fanout_errno = errno;
            zlink_multipart_close (parts_, part_count_);
            if (fanout_rc != 0) {
                errno = fanout_errno;
                return -1;
            }
        }
        return 0;
    }

    if (is_registered_spot_node_handle (subject_)) {
        errno = ENOTSUP;
        return -1;
    }

    errno = EFAULT;
    return -1;
}
