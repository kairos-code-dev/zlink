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

int spot_pub_t::set_option (int option_,
                            const void *optval_,
                            size_t optvallen_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->set_pub_option (option_, optval_, optvallen_);
}

int spot_pub_t::set_routing_id (const void *data_, size_t size_)
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->set_pub_routing_id (data_, size_);
}

int spot_pub_t::routing_id (zlink_routing_id_t *out_) const
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    return _node->pub_routing_id (out_);
}

int spot_pub_t::peers (zlink_peer_info_t *peers_, size_t *count_) const
{
    if (!_node) {
        errno = EFAULT;
        return -1;
    }
    if (!count_) {
        errno = EINVAL;
        return -1;
    }
    void *socket = _node->pub_socket_for_poller ();
    if (!socket) {
        errno = ENOTSUP;
        return -1;
    }
    return zlink_socket_peers (socket, peers_, count_);
}

void *spot_pub_t::monitor_open (int events_)
{
    if (!_node) {
        errno = EFAULT;
        return NULL;
    }
    return _node->pub_monitor_open (events_);
}

void *spot_pub_t::poller_socket ()
{
    if (!_node) {
        errno = EFAULT;
        return NULL;
    }
    return _node->pub_socket_for_poller ();
}

int spot_pub_t::destroy ()
{
    if (_node)
        _node->remove_spot_pub (this);
    _node = NULL;
    return 0;
}
}
