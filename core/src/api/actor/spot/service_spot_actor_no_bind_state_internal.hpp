/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

namespace zlink
{
namespace spot_actor_api_internal
{

struct no_bind_pending_key_t
{
    std::string target_node_rid;
    std::string actor_id;
    uint64_t generation;
    std::string caller_endpoint_rid;
    uint64_t request_id;

    bool operator< (const no_bind_pending_key_t &other_) const
    {
        if (request_id != other_.request_id)
            return request_id < other_.request_id;
        if (generation != other_.generation)
            return generation < other_.generation;
        if (target_node_rid != other_.target_node_rid)
            return target_node_rid < other_.target_node_rid;
        if (actor_id != other_.actor_id)
            return actor_id < other_.actor_id;
        return caller_endpoint_rid < other_.caller_endpoint_rid;
    }
};

struct no_bind_pending_reply_t
{
    no_bind_pending_reply_t () : handler (NULL), userdata (NULL) {}

    zlink_reply_handler_fn handler;
    void *userdata;
    std::shared_ptr<zlink::request_timeout::task_t> timeout_task;
};

struct actor_no_bind_pending_state_t
{
    int register_pending (const no_bind_pending_key_t &key_,
                          zlink_reply_handler_fn handler_,
                          void *userdata_,
                          uint32_t timeout_ms_);
    void erase_pending (const no_bind_pending_key_t &key_);
    bool take_pending (const no_bind_pending_key_t &key_, no_bind_pending_reply_t *out_);

    std::mutex mutex;
    std::map<no_bind_pending_key_t, no_bind_pending_reply_t> replies;
};

}
}
