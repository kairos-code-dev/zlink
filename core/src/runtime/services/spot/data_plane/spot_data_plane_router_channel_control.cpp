/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_router_channel_control_internal.hpp"

#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "sockets/common/socket_base.hpp"

#include <string.h>

namespace zlink
{
namespace spot_data_plane_router_channel_control
{
namespace
{

int hex_value_local (char ch_)
{
    if (ch_ >= '0' && ch_ <= '9')
        return ch_ - '0';
    if (ch_ >= 'a' && ch_ <= 'f')
        return ch_ - 'a' + 10;
    if (ch_ >= 'A' && ch_ <= 'F')
        return ch_ - 'A' + 10;
    return -1;
}

bool parse_routing_id_hex_local (const std::string &hex_,
                                 std::string *route_id_out_)
{
    if (!route_id_out_ || hex_.empty () || (hex_.size () % 2) != 0
        || hex_.size () / 2 > 255)
        return false;

    std::string route_id;
    route_id.reserve (hex_.size () / 2);
    for (size_t i = 0; i < hex_.size (); i += 2) {
        const int hi = hex_value_local (hex_[i]);
        const int lo = hex_value_local (hex_[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        route_id.push_back (static_cast<char> ((hi << 4) | lo));
    }
    route_id_out_->swap (route_id);
    return true;
}

}

bool parse_peer_arg (const std::string &arg_,
                     std::string *channel_name_out_,
                     std::string *endpoint_out_)
{
    const size_t sep = arg_.find ('\n');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= arg_.size ())
        return false;
    if (channel_name_out_)
        channel_name_out_->assign (arg_.data (), sep);
    if (endpoint_out_)
        endpoint_out_->assign (arg_.data () + sep + 1,
                               arg_.size () - sep - 1);
    return true;
}

bool parse_peer_rid_arg (const std::string &arg_,
                         std::string *channel_name_out_,
                         std::string *route_id_out_,
                         std::string *endpoint_out_)
{
    const size_t first_sep = arg_.find ('\n');
    if (first_sep == std::string::npos || first_sep == 0)
        return false;
    const size_t second_sep = arg_.find ('\n', first_sep + 1);
    if (second_sep == std::string::npos || second_sep == first_sep + 1
        || second_sep + 1 >= arg_.size ())
        return false;

    const std::string hex =
      arg_.substr (first_sep + 1, second_sep - first_sep - 1);
    std::string route_id;
    if (!parse_routing_id_hex_local (hex, &route_id))
        return false;

    if (channel_name_out_)
        channel_name_out_->assign (arg_.data (), first_sep);
    if (route_id_out_)
        route_id_out_->swap (route_id);
    if (endpoint_out_)
        endpoint_out_->assign (arg_.data () + second_sep + 1,
                               arg_.size () - second_sep - 1);
    return true;
}

int apply_client_tls (spot_node_t *node_, spot_runtime_t *runtime_)
{
    if (!node_ || !runtime_) {
        errno = EFAULT;
        return -1;
    }
    std::string ca;
    std::string host;
    int trust = 0;
    spot_node_access_t::snapshot_tls_client_config (node_, &ca, &host, &trust);
    if (runtime_->external_router
        && spot_node_access_t::apply_tls_client (
             node_, runtime_->external_router, ca, host, trust)
             != 0) {
        return -1;
    }
    return 0;
}

int connect_peer (spot_node_t *node_,
                  spot_runtime_t *runtime_,
                  const std::string &endpoint_,
                  const std::string *route_id_)
{
    if (!node_ || !runtime_ || endpoint_.empty ()) {
        errno = EFAULT;
        return -1;
    }
    if (!runtime_->external_router) {
        errno = ENOTSUP;
        return -1;
    }

    zlink_routing_id_t node_rid;
    memset (&node_rid, 0, sizeof (node_rid));
    if (node_->node_routing_id (&node_rid) != 0 || node_rid.size == 0)
        return -1;

    const std::string route_id =
      route_id_ && !route_id_->empty () ? *route_id_ : endpoint_;

    if (runtime_->external_router->setsockopt (
          ZLINK_INTERNAL_OPT_ROUTING_ID, node_rid.data, node_rid.size)
        != 0)
        return -1;

    if (runtime_->external_router->setsockopt (
          ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID, route_id.data (),
          route_id.size ())
          != 0
        || runtime_->external_router->connect (endpoint_.c_str ()) != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        (void) runtime_->external_router->term_endpoint (endpoint_.c_str ());
        errno = saved_errno;
        return -1;
    }

    runtime_->set_external_route_id (endpoint_, route_id);
    return 0;
}

}
}
