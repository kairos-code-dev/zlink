/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "utils/err.hpp"
#include "api/monitor_api_internal.hpp"

#include "services/gateway/gateway.hpp"
#include "services/spot/spot_internal_receiver.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"

int gateway_monitor_snapshot_provider (void *subject_,
                                       zlink_monitor_snapshot_t *out_)
{
    zlink::gateway_t *gateway = static_cast<zlink::gateway_t *> (subject_);
    if (!gateway || !out_) {
        errno = EINVAL;
        return -1;
    }
    return gateway->fill_monitor_snapshot (out_);
}

int spot_pub_monitor_snapshot_provider (void *subject_,
                                        zlink_monitor_snapshot_t *out_)
{
    zlink::spot_pub_t *pub = static_cast<zlink::spot_pub_t *> (subject_);
    if (!pub || !out_) {
        errno = EINVAL;
        return -1;
    }
    return pub->fill_monitor_snapshot (out_);
}

int spot_sub_monitor_snapshot_provider (void *subject_,
                                        zlink_monitor_snapshot_t *out_)
{
    zlink::spot_sub_t *sub = static_cast<zlink::spot_sub_t *> (subject_);
    if (!sub || !out_) {
        errno = EINVAL;
        return -1;
    }
    return sub->fill_monitor_snapshot (out_);
}

int spot_internal_receiver_monitor_snapshot_provider (
  void *subject_,
  zlink_monitor_snapshot_t *out_)
{
    zlink::spot_internal_receiver_t *receiver =
      static_cast<zlink::spot_internal_receiver_t *> (subject_);
    if (!receiver || !out_) {
        errno = EINVAL;
        return -1;
    }
    return receiver->fill_monitor_snapshot (out_);
}
