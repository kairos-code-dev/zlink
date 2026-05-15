/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_subject_access.hpp"

#include <string.h>
#include "api/service/service_handle_internal.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

zlink::spot_pub_t *as_spot_pub_side_handle (void *handle_)
{
    if (!handle_ || !is_registered_spot_pub_side_handle (handle_))
        return NULL;
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (handle_);
    return pub->check_tag () ? pub : NULL;
}

zlink::spot_sub_t *as_spot_sub_side_handle (void *handle_)
{
    if (!handle_ || !is_registered_spot_sub_side_handle (handle_))
        return NULL;
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (handle_);
    return sub->check_tag () ? sub : NULL;
}

zlink::spot_node_t *as_spot_node_handle (void *handle_)
{
    if (!handle_ || !is_registered_spot_node_handle (handle_))
        return NULL;
    zlink::spot_node_t *node = static_cast<zlink::spot_node_t *> (handle_);
    return node->check_tag () ? node : NULL;
}

spot_handle_t *as_spot_handle (void *spot_)
{
    if (!spot_) {
        errno = EFAULT;
        return NULL;
    }

    spot_handle_t *spot = static_cast<spot_handle_t *> (spot_);
    if (!spot->check_tag ()) {
        errno = EFAULT;
        return NULL;
    }
    return spot;
}
