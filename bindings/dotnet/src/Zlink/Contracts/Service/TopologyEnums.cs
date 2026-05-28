// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

public enum AutoConnectType
{
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    DealerMesh = 3,
    Fanout = 4,
    SpotMesh = 5
}

public enum ServiceKind
{
    Discovery = 1,
    SpotSub = 3,
    SpotPub = 4,
    Socket = 5
}

public enum ServiceRole
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

public enum TopologySource
{
    Manual = 1,
    Discovery = 2,
    Registry = 3
}

public enum TopologyState
{
    Discovered = 1,
    Connecting = 2,
    Ready = 3,
    Lost = 4,
    Error = 5,
    Stopped = 6
}
