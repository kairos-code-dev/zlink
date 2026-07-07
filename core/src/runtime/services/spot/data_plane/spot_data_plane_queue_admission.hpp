/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SPOT_DATA_PLANE_QUEUE_ADMISSION_HPP_INCLUDED
#define ZLINK_SPOT_DATA_PLANE_QUEUE_ADMISSION_HPP_INCLUDED

#include "services/spot/data_plane/spot_data_plane_runtime_state.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include <limits.h>

namespace zlink
{
static const size_t spot_data_plane_default_queue_message_unit_bytes = 64 * 1024;

struct spot_data_plane_queue_admission_plan_t
{
    spot_data_plane_queue_admission_plan_t () :
        unlimited (false),
        message_limit (1),
        byte_limit (spot_data_plane_default_queue_message_unit_bytes),
        resume_message_limit (1),
        resume_byte_limit (spot_data_plane_default_queue_message_unit_bytes / 2)
    {
    }

    bool unlimited;
    size_t message_limit;
    size_t byte_limit;
    size_t resume_message_limit;
    size_t resume_byte_limit;
};

inline size_t spot_data_plane_publish_message_unit_bytes (spot_runtime_t *runtime_,
                                                          size_t entry_bytes_)
{
    if (runtime_ && runtime_->owner) {
        const spot_node_pub_defaults_t defaults = runtime_->owner->load_pub_defaults ();
        if (defaults.auto_hwm_msg_unit_bytes.enabled && defaults.auto_hwm_msg_unit_bytes.value > 0)
            return static_cast<size_t> (defaults.auto_hwm_msg_unit_bytes.value);
    }
    return entry_bytes_ > 0 ? entry_bytes_ : 1;
}

inline spot_data_plane_queue_admission_plan_t
spot_data_plane_make_queue_admission_plan (int slots_, size_t message_unit_)
{
    spot_data_plane_queue_admission_plan_t plan;
    if (slots_ == 0) {
        plan.unlimited = true;
        plan.message_limit = 0;
        plan.byte_limit = 0;
        plan.resume_message_limit = 0;
        plan.resume_byte_limit = 0;
        return plan;
    }
    const size_t slots = static_cast<size_t> (slots_ > 0 ? slots_ : 1);
    const size_t unit = message_unit_ > 0 ? message_unit_ : 1;
    plan.unlimited = false;
    plan.message_limit = slots;
    plan.byte_limit = slots > SIZE_MAX / unit ? SIZE_MAX : slots * unit;
    plan.resume_message_limit = slots / 2;
    plan.resume_byte_limit = plan.byte_limit / 2;
    return plan;
}

inline bool spot_data_plane_publish_ingress_has_room (
  const spot_data_plane_runtime_state_t::publish_ingress_queue_t &queue_,
  const spot_data_plane_queue_admission_plan_t &plan_,
  size_t message_bytes_)
{
    if (plan_.unlimited)
        return true;
    if (queue_.messages.size () >= plan_.message_limit)
        return false;
    if (queue_.messages.empty ())
        return true;
    if (message_bytes_ > plan_.byte_limit)
        return false;
    return queue_.queued_bytes <= plan_.byte_limit - message_bytes_;
}

inline bool spot_data_plane_publish_ingress_can_resume (
  const spot_data_plane_runtime_state_t::publish_ingress_queue_t &queue_,
  const spot_data_plane_queue_admission_plan_t &plan_)
{
    if (plan_.unlimited)
        return true;
    return queue_.messages.size () <= plan_.resume_message_limit
           && queue_.queued_bytes <= plan_.resume_byte_limit;
}
}

#endif
