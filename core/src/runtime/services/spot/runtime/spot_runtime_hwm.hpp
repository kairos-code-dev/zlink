/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_RUNTIME_HWM_HPP_INCLUDED__
#define __ZLINK_SPOT_RUNTIME_HWM_HPP_INCLUDED__

#include "zlink.h"

namespace zlink
{
struct spot_node_runtime_tuning_t
{
    spot_node_runtime_tuning_t () :
        router_profile (ZLINK_AUTO_HWM_PROFILE_BALANCED),
        router_hwm_override (0),
        pubsub_profile (ZLINK_AUTO_HWM_PROFILE_BALANCED),
        pubsub_hwm_override (0),
        dispatch_workers_min (0),
        dispatch_workers_max (0)
    {
    }

    zlink_auto_hwm_profile_t router_profile;
    int router_hwm_override;
    zlink_auto_hwm_profile_t pubsub_profile;
    int pubsub_hwm_override;
    int dispatch_workers_min;
    int dispatch_workers_max;
};

inline int spot_node_admission_hwm_for_profile (zlink_auto_hwm_profile_t profile_)
{
    switch (profile_) {
        case ZLINK_AUTO_HWM_PROFILE_COMPACT:
            return 64;
        case ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY:
            return 128;
        case ZLINK_AUTO_HWM_PROFILE_THROUGHPUT:
            return 512;
        case ZLINK_AUTO_HWM_PROFILE_BALANCED:
        default:
            return 256;
    }
}

inline bool spot_node_router_hwm_overridden (const spot_node_runtime_tuning_t &config_)
{
    return config_.router_hwm_override > 0;
}

inline bool spot_node_pubsub_hwm_overridden (const spot_node_runtime_tuning_t &config_)
{
    return config_.pubsub_hwm_override > 0;
}

inline bool spot_node_valid_hwm_profile (int profile_)
{
    return profile_ == ZLINK_AUTO_HWM_PROFILE_COMPACT
           || profile_ == ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY
           || profile_ == ZLINK_AUTO_HWM_PROFILE_BALANCED
           || profile_ == ZLINK_AUTO_HWM_PROFILE_THROUGHPUT;
}

inline int spot_node_router_admission_hwm (const spot_node_runtime_tuning_t &config_)
{
    return config_.router_hwm_override > 0
             ? config_.router_hwm_override
             : spot_node_admission_hwm_for_profile (config_.router_profile);
}

inline int spot_node_pubsub_admission_hwm (const spot_node_runtime_tuning_t &config_)
{
    return config_.pubsub_hwm_override > 0
             ? config_.pubsub_hwm_override
             : spot_node_admission_hwm_for_profile (config_.pubsub_profile);
}
}

#endif
