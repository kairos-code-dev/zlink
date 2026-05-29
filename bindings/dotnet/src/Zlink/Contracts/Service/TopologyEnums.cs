// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Defines auto connect type values.
/// </summary>
public enum AutoConnectType
{
    /// <summary>
    /// Indicates the invalid auto connect type.
    /// </summary>
    Invalid = 0,
    /// <summary>
    /// Indicates the route mesh auto connect type.
    /// </summary>
    RouteMesh = 1,
    /// <summary>
    /// Indicates the client server auto connect type.
    /// </summary>
    ClientServer = 2,
    /// <summary>
    /// Indicates the dealer mesh auto connect type.
    /// </summary>
    DealerMesh = 3,
    /// <summary>
    /// Indicates the fanout auto connect type.
    /// </summary>
    Fanout = 4,
    /// <summary>
    /// Indicates the spot mesh auto connect type.
    /// </summary>
    SpotMesh = 5
}

/// <summary>
/// Defines service kind values.
/// </summary>
public enum ServiceKind
{
    /// <summary>
    /// Indicates the discovery service kind.
    /// </summary>
    Discovery = 1,
    /// <summary>
    /// Indicates the spot sub service kind.
    /// </summary>
    SpotSub = 3,
    /// <summary>
    /// Indicates the spot pub service kind.
    /// </summary>
    SpotPub = 4,
    /// <summary>
    /// Indicates the socket service kind.
    /// </summary>
    Socket = 5
}

/// <summary>
/// Defines service role values.
/// </summary>
public enum ServiceRole
{
    /// <summary>
    /// Indicates the invalid service role.
    /// </summary>
    Invalid = 0,
    /// <summary>
    /// Indicates the spot service role.
    /// </summary>
    Spot = 2,
    /// <summary>
    /// Indicates the router service role.
    /// </summary>
    Router = 3,
    /// <summary>
    /// Indicates the dealer service role.
    /// </summary>
    Dealer = 4,
    /// <summary>
    /// Indicates the pub service role.
    /// </summary>
    Pub = 5,
    /// <summary>
    /// Indicates the sub service role.
    /// </summary>
    Sub = 6
}

/// <summary>
/// Defines topology source values.
/// </summary>
public enum TopologySource
{
    /// <summary>
    /// Indicates the manual topology source.
    /// </summary>
    Manual = 1,
    /// <summary>
    /// Indicates the discovery topology source.
    /// </summary>
    Discovery = 2,
    /// <summary>
    /// Indicates the registry topology source.
    /// </summary>
    Registry = 3
}

/// <summary>
/// Defines topology state values.
/// </summary>
public enum TopologyState
{
    /// <summary>
    /// Indicates the discovered topology state.
    /// </summary>
    Discovered = 1,
    /// <summary>
    /// Indicates the connecting topology state.
    /// </summary>
    Connecting = 2,
    /// <summary>
    /// Indicates the ready topology state.
    /// </summary>
    Ready = 3,
    /// <summary>
    /// Indicates the lost topology state.
    /// </summary>
    Lost = 4,
    /// <summary>
    /// Indicates the error topology state.
    /// </summary>
    Error = 5,
    /// <summary>
    /// Indicates the stopped topology state.
    /// </summary>
    Stopped = 6
}
