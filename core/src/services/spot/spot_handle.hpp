/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_HANDLE_HPP_INCLUDED__
#define __ZLINK_SPOT_HANDLE_HPP_INCLUDED__

#include "utils/err.hpp"
#include "utils/mutex.hpp"
#include "core/signaler.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_mode_state.hpp"
#include "services/spot/spot_defaults.hpp"

#include <string.h>
#include <deque>
#include <memory>
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
}

struct spot_logical_pubsub_message_t
{
    zlink_routing_id_t source_rid;
    std::string service_name;
    std::string topic_id;
    std::vector<std::string> parts;
};

struct spot_logical_state_t
{
    spot_logical_state_t () :
        node (NULL),
        entry (false),
        rid_locked (false),
        pub (NULL),
        sub (NULL),
        subscribe_signal_armed (false)
    {
        memset (&routing_id, 0, sizeof (routing_id));
    }

    ~spot_logical_state_t () {}

    zlink::spot_node_t *node;
    zlink_routing_id_t routing_id;
    bool entry;
    bool rid_locked;
    zlink::spot_pub_t *pub;
    zlink::spot_sub_t *sub;
    zlink::mutex_t pubsub_sync;
    std::set<std::string> subscription_topics;
    std::set<std::string> subscription_patterns;
    std::deque<std::shared_ptr<spot_logical_pubsub_message_t> >
      subscribe_queue;
    zlink::signaler_t subscribe_signaler;
    bool subscribe_signal_armed;
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
    std::shared_ptr<zlink::spot_reqrep_internal::spot_request_reply_state_t>
      request_reply_state;
};

#endif
