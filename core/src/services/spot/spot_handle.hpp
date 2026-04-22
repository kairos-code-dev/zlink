/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_HANDLE_HPP_INCLUDED__
#define __ZLINK_SPOT_HANDLE_HPP_INCLUDED__

#include "utils/err.hpp"
#include "services/common/service_public_api.hpp"
#include "services/common/service_mode_state.hpp"
#include "services/spot/spot_defaults.hpp"

#include <memory>

namespace zlink
{
class spot_node_t;
class spot_pub_t;
class spot_sub_t;
}

struct spot_handle_t
{
    spot_handle_t () :
        tag (0x1e6700dc),
        node (NULL),
        pub (NULL),
        sub (NULL),
        handler (NULL),
        handler_userdata (NULL)
    {
    }

    bool check_tag () const { return tag == 0x1e6700dc; }

    uint32_t tag;
    zlink::service_public_api_guard_t public_api;
    zlink::spot_node_t *node;
    zlink::spot_pub_t *pub;
    zlink::spot_sub_t *sub;
    zlink_subscribe_handler_fn handler;
    void *handler_userdata;
    zlink::spot_node_pub_defaults_t pending_pub_defaults;
    zlink::spot_node_sub_defaults_t pending_sub_defaults;
    zlink::service_mode_state_t mode_state;
    std::shared_ptr<void> request_reply_state;
};

#endif
