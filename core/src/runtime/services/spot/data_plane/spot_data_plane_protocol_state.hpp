/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SPOT_DATA_PLANE_PROTOCOL_STATE_HPP_INCLUDED
#define ZLINK_SPOT_DATA_PLANE_PROTOCOL_STATE_HPP_INCLUDED

#include <map>
#include <set>
#include <string>

namespace zlink
{
struct spot_data_plane_protocol_state_t
{
    std::map<std::string, std::string> peer_ctrl_endpoints;
    std::set<std::string> outbound_subscription_filters;
    std::map<std::string, std::set<std::string>> peer_ready_filters;
    std::map<std::string, std::map<std::string, std::set<std::string>>> outbound_ready_filters;
};
}

#endif
