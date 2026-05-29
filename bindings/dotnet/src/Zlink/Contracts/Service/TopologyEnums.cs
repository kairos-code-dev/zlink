// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
/// Defines auto connect type values.
/// </summary>
public enum AutoConnectType
{
    /// <summary>
    /// Represents the Invalid value.
    /// </summary>
    Invalid = 0,
    /// <summary>
    /// Represents the RouteMesh value.
    /// </summary>
    RouteMesh = 1,
    /// <summary>
    /// Represents the ClientServer value.
    /// </summary>
    ClientServer = 2,
    /// <summary>
    /// Represents the DealerMesh value.
    /// </summary>
    DealerMesh = 3,
    /// <summary>
    /// Represents the Fanout value.
    /// </summary>
    Fanout = 4,
    /// <summary>
    /// Represents the SpotMesh value.
    /// </summary>
    SpotMesh = 5
}

/// <summary>
/// Defines service kind values.
/// </summary>
public enum ServiceKind
{
    /// <summary>
    /// Represents the Discovery value.
    /// </summary>
    Discovery = 1,
    /// <summary>
    /// Represents the SpotSub value.
    /// </summary>
    SpotSub = 3,
    /// <summary>
    /// Represents the SpotPub value.
    /// </summary>
    SpotPub = 4,
    /// <summary>
    /// Represents the Socket value.
    /// </summary>
    Socket = 5
}

/// <summary>
/// Defines service role values.
/// </summary>
public enum ServiceRole
{
    /// <summary>
    /// Represents the Invalid value.
    /// </summary>
    Invalid = 0,
    /// <summary>
    /// Represents the Spot value.
    /// </summary>
    Spot = 2,
    /// <summary>
    /// Represents the Router value.
    /// </summary>
    Router = 3,
    /// <summary>
    /// Represents the Dealer value.
    /// </summary>
    Dealer = 4,
    /// <summary>
    /// Represents the Pub value.
    /// </summary>
    Pub = 5,
    /// <summary>
    /// Represents the Sub value.
    /// </summary>
    Sub = 6
}

/// <summary>
/// Defines topology source values.
/// </summary>
public enum TopologySource
{
    /// <summary>
    /// Represents the Manual value.
    /// </summary>
    Manual = 1,
    /// <summary>
    /// Represents the Discovery value.
    /// </summary>
    Discovery = 2,
    /// <summary>
    /// Represents the Registry value.
    /// </summary>
    Registry = 3
}

/// <summary>
/// Defines topology state values.
/// </summary>
public enum TopologyState
{
    /// <summary>
    /// Represents the Discovered value.
    /// </summary>
    Discovered = 1,
    /// <summary>
    /// Represents the Connecting value.
    /// </summary>
    Connecting = 2,
    /// <summary>
    /// Represents the Ready value.
    /// </summary>
    Ready = 3,
    /// <summary>
    /// Represents the Lost value.
    /// </summary>
    Lost = 4,
    /// <summary>
    /// Represents the Error value.
    /// </summary>
    Error = 5,
    /// <summary>
    /// Represents the Stopped value.
    /// </summary>
    Stopped = 6
}
