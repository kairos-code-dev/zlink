// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// How a discovery service automatically wires connections between peers.
/// </summary>
public enum AutoConnectType
{
    /// <summary>
    /// No auto-connect topology (unset).
    /// </summary>
    Invalid = 0,
    /// <summary>
    /// A mesh of routed (ROUTER) connections between peers.
    /// </summary>
    RouteMesh = 1,
    /// <summary>
    /// A client-server star: clients connect to servers.
    /// </summary>
    ClientServer = 2,
    /// <summary>
    /// A mesh of DEALER connections between peers.
    /// </summary>
    DealerMesh = 3,
    /// <summary>
    /// A publish/subscribe fan-out from publishers to subscribers.
    /// </summary>
    Fanout = 4,
    /// <summary>
    /// A mesh of spot connections between peers.
    /// </summary>
    SpotMesh = 5
}

/// <summary>
/// The kind of service a topology entry represents.
/// </summary>
public enum ServiceKind
{
    /// <summary>
    /// A discovery service.
    /// </summary>
    Discovery = 1,
    /// <summary>
    /// The subscribe side of a spot.
    /// </summary>
    SpotSub = 3,
    /// <summary>
    /// The publish side of a spot.
    /// </summary>
    SpotPub = 4,
    /// <summary>
    /// A plain socket.
    /// </summary>
    Socket = 5
}

/// <summary>
/// The messaging role a service plays in the topology.
/// </summary>
public enum ServiceRole
{
    /// <summary>
    /// No role (unset).
    /// </summary>
    Invalid = 0,
    /// <summary>
    /// A spot.
    /// </summary>
    Spot = 2,
    /// <summary>
    /// A ROUTER endpoint.
    /// </summary>
    Router = 3,
    /// <summary>
    /// A DEALER endpoint.
    /// </summary>
    Dealer = 4,
    /// <summary>
    /// A publisher.
    /// </summary>
    Pub = 5,
    /// <summary>
    /// A subscriber.
    /// </summary>
    Sub = 6
}

/// <summary>
/// Where a topology entry was learned from.
/// </summary>
public enum TopologySource
{
    /// <summary>
    /// Added manually by the application.
    /// </summary>
    Manual = 1,
    /// <summary>
    /// Learned from a discovery service.
    /// </summary>
    Discovery = 2,
    /// <summary>
    /// Learned from a service registry.
    /// </summary>
    Registry = 3
}

/// <summary>
/// The lifecycle state of a topology connection.
/// </summary>
public enum TopologyState
{
    /// <summary>
    /// The peer was discovered but not yet connected.
    /// </summary>
    Discovered = 1,
    /// <summary>
    /// A connection to the peer is being established.
    /// </summary>
    Connecting = 2,
    /// <summary>
    /// The peer is connected and usable.
    /// </summary>
    Ready = 3,
    /// <summary>
    /// The connection to the peer was lost.
    /// </summary>
    Lost = 4,
    /// <summary>
    /// The connection failed with an error.
    /// </summary>
    Error = 5,
    /// <summary>
    /// The connection was stopped.
    /// </summary>
    Stopped = 6
}
