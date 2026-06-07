/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

namespace zlink
{
namespace spot_actor_api_internal
{

const uint32_t actor_handle_tag = 0xacc70001u;

struct queued_actor_part_t
{
    queued_actor_part_t () : owns (false), part_flag (ZLINK_PART_FINAL)
    {
        memset (&info, 0, sizeof (info));
        memset (&part, 0, sizeof (part));
    }

    ~queued_actor_part_t ()
    {
        if (owns)
            (void) zlink_msg_close (&part);
    }

    queued_actor_part_t (queued_actor_part_t &&other_) noexcept :
        owns (other_.owns), info (other_.info), part_flag (other_.part_flag)
    {
        part = other_.part;
        other_.owns = false;
        memset (&other_.part, 0, sizeof (other_.part));
    }

    queued_actor_part_t &operator= (queued_actor_part_t &&other_) noexcept
    {
        if (this == &other_)
            return *this;
        if (owns)
            (void) zlink_msg_close (&part);
        owns = other_.owns;
        info = other_.info;
        part_flag = other_.part_flag;
        part = other_.part;
        other_.owns = false;
        memset (&other_.part, 0, sizeof (other_.part));
        return *this;
    }

    queued_actor_part_t (const queued_actor_part_t &) = delete;
    queued_actor_part_t &operator= (const queued_actor_part_t &) = delete;

    bool owns;
    zlink_actor_recv_info_t info;
    zlink_msg_t part;
    zlink_part_flag_t part_flag;
};

struct actor_handle_t
{
    actor_handle_t () :
        tag (actor_handle_tag),
        node (NULL),
        generation (0),
        join_epoch (0),
        bound_session_node (NULL),
        bound_stream (NULL),
        last_changed_ms (0),
        pending_remote_join (false)
    {
        memset (&node_rid, 0, sizeof (node_rid));
        memset (&ref_cache, 0, sizeof (ref_cache));
        memset (&bound_session_node_rid, 0, sizeof (bound_session_node_rid));
        memset (&bound_session_rid, 0, sizeof (bound_session_rid));
    }

    bool check_tag () const { return tag == actor_handle_tag; }

    uint32_t tag;
    zlink::spot_node_t *node;
    zlink_routing_id_t node_rid;
    zlink_actor_ref_t ref_cache;
    std::string actor_id;
    uint64_t generation;
    uint64_t join_epoch;
    std::shared_ptr<spot_logical_state_t> joined_spot_state;
    zlink::spot_node_t *bound_session_node;
    zlink_routing_id_t bound_session_node_rid;
    void *bound_stream;
    zlink_routing_id_t bound_session_rid;
    uint64_t last_changed_ms;
    bool pending_remote_join;
    std::deque<queued_actor_part_t> queue;
};

}
}
