/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"


namespace zlink
{

namespace detail
{
struct service_model_access_t;
} // namespace detail

enum class auto_connect_type : int
{
    invalid = 0,
    route_mesh = 1,
    client_server = 2,
    dealer_mesh = 3,
    fanout = 4,
    spot_mesh = 5
};

enum class service_role : int
{
    invalid = 0,
    spot = 2,
    router = 3,
    dealer = 4,
    pub = 5,
    sub = 6
};

enum class service_kind : int
{
    discovery = 1,
    spot_sub = 3,
    spot_pub = 4,
    socket = 5
};

using auto_connect_type_t = auto_connect_type;
using service_role_t = service_role;
using service_kind_t = service_kind;
} // namespace zlink
