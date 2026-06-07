/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_HANDLE_HPP_INCLUDED__
#define __ZLINK_SPOT_HANDLE_HPP_INCLUDED__

#include "utils/err.hpp"
#include "utils/mutex.hpp"
#include "core/signaler.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_mode_state.hpp"
#include "services/spot/common/spot_defaults.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"

#include <atomic>
#include <string.h>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace zlink
{
class spot_node_t;
class spot_pub_t;
class spot_sub_t;
namespace spot_reqrep_internal
{
struct spot_request_reply_state_t;
}
namespace part_helper_internal
{
struct handle_state_t;
}
}

struct spot_logical_pubsub_message_t
{
    ~spot_logical_pubsub_message_t () { zlink::spot_clear_msg_parts (&parts); }

    zlink_routing_id_t source_rid;
    std::string topic_id;
    zlink::spot_owned_msg_parts_t parts;
};

struct spot_logical_state_t
{
    spot_logical_state_t () :
        node (NULL),
        stable_id (0),
        entry (false),
        rid_locked (false),
        subscribe_signal_armed (false),
        send_ready_signal_armed (false),
        send_ready_handler (NULL),
        send_ready_subject (NULL),
        send_ready_userdata (NULL)
    {
        memset (&routing_id, 0, sizeof (routing_id));
    }

    ~spot_logical_state_t () {}

    zlink::spot_node_t *node;
    uint64_t stable_id;
    zlink_routing_id_t routing_id;
    bool entry;
    bool rid_locked;
    zlink::mutex_t pubsub_sync;
    std::set<std::string> subscription_topics;
    std::set<std::string> subscription_patterns;
    std::deque<std::shared_ptr<spot_logical_pubsub_message_t>> subscribe_queue;
    zlink::signaler_t subscribe_signaler;
    bool subscribe_signal_armed;
    zlink::signaler_t send_ready_signaler;
    bool send_ready_signal_armed;
    std::atomic<zlink_send_ready_handler_fn> send_ready_handler;
    std::atomic<void *> send_ready_subject;
    std::atomic<void *> send_ready_userdata;
    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t> request_reply_state;
};

struct spot_handle_t
{
    spot_handle_t () :
        tag (0x1e6700dc),
        node (NULL),
        mode (ZLINK_SPOT_NODE_MODE_ALL),
        handler (NULL),
        handler_userdata (NULL)
    {
    }

    bool check_tag () const { return tag == 0x1e6700dc; }

    uint32_t tag;
    zlink::service_public_api_guard_t public_api;
    zlink::spot_node_t *node;
    zlink_spot_node_mode_t mode;
    std::shared_ptr<spot_logical_state_t> logical_state;
    zlink_routing_id_t spot_routing_id;
    zlink_subscribe_handler_fn handler;
    void *handler_userdata;
    zlink::spot_node_pub_defaults_t pending_pub_defaults;
    zlink::spot_node_sub_defaults_t pending_sub_defaults;
    zlink::service_mode_state_t mode_state;
    std::mutex part_helper_mutex;
    std::shared_ptr<zlink::part_helper_internal::handle_state_t> part_helper_state;
};

#endif
