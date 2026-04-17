/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_spot_request_reply_internal.hpp"
#include "api/request_reply_protocol_internal.hpp"

namespace zlink
{
namespace spot_reqrep_internal
{
bool pending_spot_key_t::operator< (const pending_spot_key_t &other_) const
{
    if (request_seq != other_.request_seq)
        return request_seq < other_.request_seq;
    if (source_class != other_.source_class)
        return source_class < other_.source_class;
    if (source_rid != other_.source_rid)
        return source_rid < other_.source_rid;
    return source_spot_rid < other_.source_spot_rid;
}

spot_dispatch_state_t::spot_dispatch_state_t () :
    handler (NULL),
    handler_userdata (NULL),
    runtime (NULL),
    task_id (0),
    pending_event_mask (0),
    running (false)
{
}

spot_request_reply_state_t::spot_request_reply_state_t (void *owner_) :
    owner (owner_),
    default_timeout_ms (zlink::request_reply::default_timeout_ms),
    next_request_seq (1),
    request_handler (NULL),
    request_handler_userdata (NULL)
{
}

router_spot_request_reply_state_t::router_spot_request_reply_state_t (
  void *owner_) :
    owner (owner_),
    default_timeout_ms (zlink::request_reply::default_timeout_ms),
    next_request_seq (1)
{
}

std::mutex g_spot_request_reply_index_mutex;
spot_state_identity_index_t g_spot_state_identity_index;
router_state_identity_index_t g_router_state_identity_index;
thread_local zlink_routing_id_t g_spot_recv_source_rid;
thread_local zlink_routing_id_t g_spot_recv_spot_rid;
thread_local uint8_t g_spot_recv_source_rid_storage[255];
thread_local uint8_t g_spot_recv_spot_rid_storage[255];

std::string make_spot_identity_key (const std::string &node_rid_,
                                    const std::string &spot_rid_)
{
    return node_rid_ + '\n' + spot_rid_;
}

int validate_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if ((!parts_ && part_count_ > 0) || part_count_ == 0) {
        errno = EFAULT;
        return -1;
    }

    return 0;
}

int init_buffer_frame (zlink_msg_t *msg_, const void *data_, size_t size_)
{
    if (!msg_) {
        errno = EFAULT;
        return -1;
    }
    if (zlink_msg_init_size (msg_, size_) != 0)
        return -1;
    if (size_ > 0 && data_)
        memcpy (zlink_msg_data (msg_), data_, size_);
    return 0;
}
}
}
