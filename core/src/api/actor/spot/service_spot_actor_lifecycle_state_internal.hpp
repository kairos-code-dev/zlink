/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

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

    zlink_spot_actor_lifecycle_event_kind_t kind;
    zlink_spot_actor_lifecycle_info_t info;
};

struct actor_lifecycle_state_t
{
    void clear (spot_logical_state_t *key_);
    void enqueue (spot_logical_state_t *key_, const lifecycle_event_t &event_);
    bool pop (spot_logical_state_t *key_, lifecycle_event_t *event_out_);

    std::map<spot_logical_state_t *, std::deque<lifecycle_event_t> > queues;
};

}
}
