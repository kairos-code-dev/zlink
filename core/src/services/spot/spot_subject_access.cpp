/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_subject_access.hpp"

#include <string.h>
#include "api/service_handle_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

zlink::spot_pub_t *as_spot_pub_side_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (handle_);
    return pub->check_tag () ? pub : NULL;
}

zlink::spot_sub_t *as_spot_sub_side_handle (void *handle_)
{
    if (!handle_)
        return NULL;
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (handle_);
    return sub->check_tag () ? sub : NULL;
}

zlink::spot_node_t *as_spot_node_handle (void *handle_)
{
    if (!handle_)
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

zlink::spot_pub_t *ensure_spot_pub (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->node) {
        errno = EFAULT;
        return NULL;
    }

    if (spot_->pub)
        return spot_->pub;

    zlink::spot_pub_t *pub = spot_->node->create_spot_pub ();
    if (!pub)
        return NULL;

    if (zlink::apply_spot_pub_defaults (pub, spot_->pending_pub_defaults) != 0) {
        const int err = errno;
        (void) pub->destroy ();
        delete pub;
        errno = err;
        return NULL;
    }

    spot_->pub = pub;
    return spot_->pub;
}

zlink::spot_sub_t *ensure_spot_sub (spot_handle_t *spot_)
{
    if (!spot_ || !spot_->node) {
        errno = EFAULT;
        return NULL;
    }

    if (spot_->sub)
        return spot_->sub;

    zlink::spot_sub_t *sub = spot_->node->create_spot_sub ();
    if (!sub)
        return NULL;

    if (spot_->handler
        && sub->set_direct_handler (&spot_subject_composite_sub_handler_adapter,
                                    spot_)
             != 0) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    if (zlink::apply_spot_sub_defaults (sub, spot_->pending_sub_defaults) != 0) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    spot_->sub = sub;
    return spot_->sub;
}
