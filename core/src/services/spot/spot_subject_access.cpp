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

    const zlink::spot_node_t::pub_defaults_t &defaults =
      spot_->pending_pub_defaults;
    if ((defaults.sndhwm.enabled
         && pub->set_option (ZLINK_SPOT_PUB_OPT_SNDHWM, &defaults.sndhwm.value,
                             defaults.sndhwm.size)
              != 0)
        || (defaults.sndtimeo.enabled
            && pub->set_option (ZLINK_SPOT_PUB_OPT_SNDTIMEO,
                                &defaults.sndtimeo.value,
                                defaults.sndtimeo.size)
                 != 0)
        || (defaults.linger.enabled
            && pub->set_option (ZLINK_SPOT_PUB_OPT_LINGER,
                                &defaults.linger.value, defaults.linger.size)
                 != 0)
        || (defaults.nodrop.enabled
            && pub->set_option (ZLINK_SPOT_PUB_OPT_NODROP,
                                &defaults.nodrop.value, defaults.nodrop.size)
                 != 0)
        || (defaults.sndbuf.enabled
            && pub->set_option (ZLINK_SPOT_PUB_OPT_SNDBUF,
                                &defaults.sndbuf.value, defaults.sndbuf.size)
                 != 0)
        || (defaults.rcvbuf.enabled
            && pub->set_option (ZLINK_SPOT_PUB_OPT_RCVBUF,
                                &defaults.rcvbuf.value, defaults.rcvbuf.size)
                 != 0)
        || (defaults.auto_hwm_msg_unit_bytes.enabled
            && pub->set_option (ZLINK_SPOT_PUB_OPT_AUTO_HWM_MSG_UNIT_BYTES,
                                &defaults.auto_hwm_msg_unit_bytes.value,
                                defaults.auto_hwm_msg_unit_bytes.size)
                 != 0)) {
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

    const zlink::spot_node_t::sub_defaults_t &defaults =
      spot_->pending_sub_defaults;
    if ((defaults.rcvhwm.enabled
         && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVHWM, &defaults.rcvhwm.value,
                             defaults.rcvhwm.size)
              != 0)
        || (defaults.linger.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_LINGER,
                                &defaults.linger.value, defaults.linger.size)
                 != 0)
        || (defaults.sndbuf.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_SNDBUF,
                                &defaults.sndbuf.value, defaults.sndbuf.size)
                 != 0)
        || (defaults.rcvbuf.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVBUF,
                                &defaults.rcvbuf.value, defaults.rcvbuf.size)
                 != 0)
        || (defaults.rcvtimeo.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_RCVTIMEO,
                                &defaults.rcvtimeo.value,
                                defaults.rcvtimeo.size)
                 != 0)
        || (defaults.auto_hwm_msg_unit_bytes.enabled
            && sub->set_option (ZLINK_SPOT_SUB_OPT_AUTO_HWM_MSG_UNIT_BYTES,
                                &defaults.auto_hwm_msg_unit_bytes.value,
                                defaults.auto_hwm_msg_unit_bytes.size)
                 != 0)) {
        const int err = errno;
        (void) sub->destroy ();
        delete sub;
        errno = err;
        return NULL;
    }

    spot_->sub = sub;
    return spot_->sub;
}
