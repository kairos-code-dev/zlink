/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "services/common/service_public_api.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include <new>

namespace
{
const uint32_t spot_node_publisher_tag = 0x5b700001u;

struct spot_node_publisher_t
{
    uint32_t tag;
    zlink::spot_node_t *node;
    bool closed;
};

spot_node_publisher_t *as_publisher (void *publisher_)
{
    spot_node_publisher_t *publisher = static_cast<spot_node_publisher_t *> (publisher_);
    if (!publisher || publisher->tag != spot_node_publisher_tag) {
        errno = EFAULT;
        return NULL;
    }
    if (publisher->closed) {
        errno = ESHUTDOWN;
        return NULL;
    }
    return publisher;
}
}

void *zlink_spot_node_publisher_new (void *spot_node_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle (spot_node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }
    if (!zlink::spot_node_access_t::pubsub_enabled (node)) {
        errno = ENOTSUP;
        return NULL;
    }
    spot_node_publisher_t *publisher = new (std::nothrow) spot_node_publisher_t ();
    if (!publisher) {
        errno = ENOMEM;
        return NULL;
    }
    publisher->tag = spot_node_publisher_tag;
    publisher->node = node;
    publisher->closed = false;
    return publisher;
}

int zlink_spot_node_publisher_publish (void *publisher_,
                                       const char *topic_,
                                       zlink_msg_t *parts_,
                                       size_t part_count_,
                                       zlink_send_flags_t flags_)
{
    spot_node_publisher_t *publisher = as_publisher (publisher_);
    if (!publisher)
        return -1;
    if (!topic_ || !*topic_ || (!parts_ && part_count_ != 0)) {
        errno = EINVAL;
        return -1;
    }
    if (zlink::spot_node_access_t::is_shutting_down (publisher->node)) {
        errno = ESHUTDOWN;
        return -1;
    }
    zlink::service_public_api_scope_t admission (publisher->node->public_api_guard ());
    if (!admission.acquired ())
        return -1;
    zlink::spot_runtime_t *runtime = zlink::spot_node_access_t::runtime (publisher->node);
    return zlink::spot_data_plane_forwarder_t::enqueue_publish_ingress (runtime, topic_, parts_,
                                                                        part_count_, flags_, -1);
}

int zlink_spot_node_publisher_close (void *publisher_)
{
    spot_node_publisher_t *publisher = static_cast<spot_node_publisher_t *> (publisher_);
    if (!publisher || publisher->tag != spot_node_publisher_tag) {
        errno = EFAULT;
        return -1;
    }
    publisher->closed = true;
    publisher->tag = 0;
    delete publisher;
    return 0;
}
