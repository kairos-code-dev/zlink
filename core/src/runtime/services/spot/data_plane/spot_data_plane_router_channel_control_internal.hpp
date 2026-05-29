/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_SPOT_DATA_PLANE_ROUTER_CHANNEL_CONTROL_INTERNAL_HPP_INCLUDED__
#define __ZLINK_SPOT_DATA_PLANE_ROUTER_CHANNEL_CONTROL_INTERNAL_HPP_INCLUDED__

#include <string>

namespace zlink
{
class spot_node_t;
struct spot_runtime_t;

namespace spot_data_plane_router_channel_control
{

bool parse_peer_arg (const std::string &arg_,
                     std::string *channel_name_out_,
                     std::string *endpoint_out_);

bool parse_peer_rid_arg (const std::string &arg_,
                         std::string *channel_name_out_,
                         std::string *route_id_out_,
                         std::string *endpoint_out_);

int apply_client_tls (spot_node_t *node_, spot_runtime_t *runtime_);

int connect_peer (spot_node_t *node_,
                  spot_runtime_t *runtime_,
                  const std::string &endpoint_,
                  const std::string *route_id_ = NULL);

}
}

#endif
