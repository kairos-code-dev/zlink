/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "services/spot/common/spot_message_parts_internal.hpp"

namespace zlink
{
namespace spot_actor_api_internal
{

struct lifecycle_event_t
{
    lifecycle_event_t () : kind (ZLINK_SPOT_ACTOR_LIFECYCLE_JOINED)
    {
        memset (&info, 0, sizeof (info));
    }

    ~lifecycle_event_t () { zlink::spot_clear_msg_parts (&request_parts); }

    lifecycle_event_t (lifecycle_event_t &&other_) noexcept :
        kind (other_.kind), info (other_.info)
    {
        request_parts.swap (other_.request_parts);
    }

    lifecycle_event_t &operator= (lifecycle_event_t &&other_) noexcept
    {
        if (this == &other_)
            return *this;
        zlink::spot_clear_msg_parts (&request_parts);
        kind = other_.kind;
        info = other_.info;
        request_parts.swap (other_.request_parts);
        return *this;
    }

    lifecycle_event_t (const lifecycle_event_t &) = delete;
    lifecycle_event_t &operator= (const lifecycle_event_t &) = delete;

    zlink_spot_actor_lifecycle_event_kind_t kind;
    zlink_spot_actor_lifecycle_info_t info;
    zlink::spot_owned_msg_parts_t request_parts;
};

struct actor_lifecycle_state_t
{
    void clear (spot_logical_state_t *key_);
    void enqueue (spot_logical_state_t *key_, lifecycle_event_t &&event_);
    bool pop (spot_logical_state_t *key_, lifecycle_event_t *event_out_);

    std::map<spot_logical_state_t *, std::deque<lifecycle_event_t>> queues;
};

}
}
