/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"


namespace zlink
{

namespace detail
{
struct service_model_access_t;
} // namespace detail

/// @brief How a discovery service automatically wires connections between peers.
enum class auto_connect_type : int
{
    invalid = 0,     ///< No auto-connect topology (unset).
    route_mesh = 1,  ///< A mesh of ROUTER connections between peers.
    client_server = 2, ///< A client-server star: clients connect to servers.
    dealer_mesh = 3,   ///< A mesh of DEALER connections between peers.
    fanout = 4,        ///< A publish/subscribe fan-out from publishers to subscribers.
    spot_mesh = 5      ///< A mesh of spot connections between peers.
};

/// @brief The messaging role a service plays in the topology.
enum class service_role : int
{
    invalid = 0, ///< No role (unset).
    spot = 2,    ///< A spot.
    router = 3,  ///< A ROUTER endpoint.
    dealer = 4,  ///< A DEALER endpoint.
    pub = 5,     ///< A publisher.
    sub = 6      ///< A subscriber.
};

/// @brief The kind of service a topology entry represents.
enum class service_kind : int
{
    discovery = 1, ///< A discovery service.
    spot_sub = 3,  ///< The subscribe side of a spot.
    spot_pub = 4,  ///< The publish side of a spot.
    socket = 5     ///< A plain socket.
};

using auto_connect_type_t = auto_connect_type;
using service_role_t = service_role;
using service_kind_t = service_kind;
} // namespace zlink
