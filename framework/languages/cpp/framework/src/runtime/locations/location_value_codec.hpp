/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/locations/values.hpp>

#include <stdexcept>
#include <string>
#include <string_view>

namespace zlink::framework::runtime
{

class location_value_codec_t
{
  public:
    static std::string to_canonical_string (location_auto_connect_type_t type)
    {
        switch (type) {
            case location_auto_connect_type_t::route_mesh:
                return "route-mesh";
            case location_auto_connect_type_t::client_server:
                return "client-server";
            case location_auto_connect_type_t::dealer_mesh:
                return "dealer-mesh";
            case location_auto_connect_type_t::fanout:
                return "fanout";
            case location_auto_connect_type_t::spot_mesh:
                return "spot-mesh";
            default:
                throw std::invalid_argument ("unknown location auto-connect type");
        }
    }

    static std::string to_canonical_string (location_role_t role)
    {
        switch (role) {
            case location_role_t::spot:
                return "spot";
            case location_role_t::router:
                return "router";
            case location_role_t::dealer:
                return "dealer";
            case location_role_t::pub:
                return "pub";
            case location_role_t::sub:
                return "sub";
            default:
                throw std::invalid_argument ("unknown location role");
        }
    }

    static bool try_parse_auto_connect_type (std::string_view value,
                                             location_auto_connect_type_t &type) noexcept
    {
        if (value == "route-mesh") {
            type = location_auto_connect_type_t::route_mesh;
        } else if (value == "client-server") {
            type = location_auto_connect_type_t::client_server;
        } else if (value == "dealer-mesh") {
            type = location_auto_connect_type_t::dealer_mesh;
        } else if (value == "fanout") {
            type = location_auto_connect_type_t::fanout;
        } else if (value == "spot-mesh") {
            type = location_auto_connect_type_t::spot_mesh;
        } else {
            type = location_auto_connect_type_t::invalid;
            return false;
        }
        return true;
    }

    static bool try_parse_role (std::string_view value, location_role_t &role) noexcept
    {
        if (value == "spot") {
            role = location_role_t::spot;
        } else if (value == "router") {
            role = location_role_t::router;
        } else if (value == "dealer") {
            role = location_role_t::dealer;
        } else if (value == "pub") {
            role = location_role_t::pub;
        } else if (value == "sub") {
            role = location_role_t::sub;
        } else {
            role = location_role_t::invalid;
            return false;
        }
        return true;
    }
};

} // namespace zlink::framework::runtime
