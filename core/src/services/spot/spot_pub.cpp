/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_node.hpp"

#include "utils/err.hpp"

namespace zlink
{
static const uint32_t spot_pub_tag_value = 0x1e6700db;

spot_pub_t::spot_pub_t (spot_node_t *node_) : _node (node_), _tag (spot_pub_tag_value)
{
}

spot_pub_t::~spot_pub_t ()
{
    _tag = 0xdeadbeef;
}

bool spot_pub_t::check_tag () const
{
    return _tag == spot_pub_tag_value;
}

int spot_pub_t::publish (const char *topic_,
                         zlink_msg_t *parts_,
                         size_t part_count_,
                         int flags_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->publish (topic_, parts_, part_count_, flags_);
}

int spot_pub_t::set_socket_option (int option_,
                                   const void *optval_,
                                   size_t optvallen_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->set_socket_option (ZLINK_SPOT_NODE_SOCKET_PUB, option_,
                                     optval_, optvallen_);
}

int spot_pub_t::destroy ()
{
    if (_node)
        _node->remove_spot_pub (this);
    _node = NULL;
    return 0;
}
}
