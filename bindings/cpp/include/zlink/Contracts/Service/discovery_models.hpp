/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Messaging/message.hpp"


#include <cstdint>
#include <utility>

namespace zlink
{

namespace detail
{
struct service_model_access_t;
} // namespace detail

/// @brief How a discovery service automatically wires connections between peers.
enum class auto_connect_type : int
{
    invalid = 0,       ///< No auto-connect topology (unset).
    route_mesh = 1,    ///< A mesh of ROUTER connections between peers.
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

/// @brief The owner-scoped discovery route kind.
enum class route_kind : uint32_t
{
    invalid = 0,      ///< No route kind (unset).
    actor = 1,        ///< A route keyed by actor id.
    spot_name = 2,    ///< A route keyed by spot name.
    actor_session = 3 ///< A route keyed by actor session.
};

using route_kind_t = route_kind;

/// @brief A resolved custom route entry: the owning node routing id and a value payload.
struct discovery_route_t
{
    discovery_route_t (routing_id_t owner_routing_id_, message_t value_) :
        owner_routing_id (std::move (owner_routing_id_)), value (std::move (value_))
    {
    }

    routing_id_t owner_routing_id;
    message_t value;
};
} // namespace zlink
