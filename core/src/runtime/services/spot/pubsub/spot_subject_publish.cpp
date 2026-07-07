/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_subject_access.hpp"

#include "api/service/service_handle_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/data_plane/spot_data_plane_forwarding_internal.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

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
        zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (spot->node);
        const int publish_rc = zlink::spot_data_plane_forwarder_t::enqueue_publish_ingress (
          runtime, topic_id_, parts_, part_count_, static_cast<zlink_send_flags_t> (flags_),
          resolve_spot_send_timeout_ms (spot));
        const int publish_errno = errno;
        if (publish_rc != 0) {
            errno = publish_errno;
            return -1;
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
