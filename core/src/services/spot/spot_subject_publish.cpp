/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_subject_access.hpp"

#include "api/service_handle_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_node_access.hpp"
#include "services/spot/spot_pub.hpp"

#include <string>
#include <vector>

namespace
{
void close_copied_parts (std::vector<zlink_msg_t> *parts_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < parts_->size (); ++i)
        (void) zlink_msg_close (&(*parts_)[i]);
    parts_->clear ();
}

int copy_parts_for_logical_fanout (zlink_msg_t *parts_,
                                   size_t part_count_,
                                   std::vector<zlink_msg_t> *out_)
{
    if (!out_ || (part_count_ > 0 && !parts_)) {
        errno = EINVAL;
        return -1;
    }
    out_->resize (part_count_);
    for (size_t i = 0; i < part_count_; ++i) {
        zlink_msg_init (&(*out_)[i]);
        if (zlink_msg_copy (&(*out_)[i], &parts_[i]) != ZLINK_CONFIG_OK) {
            close_copied_parts (out_);
            return -1;
        }
    }
    return 0;
}
}

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
        zlink::spot_pub_t *pub = ensure_spot_pub (spot);
        if (!pub) {
            errno = ENOTSUP;
            return -1;
        }
        std::vector<zlink_msg_t> fanout_parts;
        if (copy_parts_for_logical_fanout (parts_, part_count_, &fanout_parts)
            != 0)
            return -1;
        if (pub->publish (topic_id_, parts_, part_count_, flags_) != 0) {
            const int publish_errno = errno;
            close_copied_parts (&fanout_parts);
            errno = publish_errno;
            return -1;
        }
        std::string service_name =
          zlink::spot_node_access_t::summary_service_name (spot->node);
        zlink_routing_id_t source_rid;
        memset (&source_rid, 0, sizeof (source_rid));
        (void) spot->node->node_routing_id (&source_rid);
        const int fanout_rc = spot->node->fanout_local_publish (
          service_name.c_str (), &source_rid, topic_id_,
          fanout_parts.empty () ? NULL : fanout_parts.data (),
          fanout_parts.size ());
        const int fanout_errno = errno;
        close_copied_parts (&fanout_parts);
        if (fanout_rc != 0) {
            errno = fanout_errno;
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
