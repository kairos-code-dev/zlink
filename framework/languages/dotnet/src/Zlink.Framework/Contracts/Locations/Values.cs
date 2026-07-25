namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Channel auto-connect uses a subset with the same numeric values; DealerMesh
/// remains location-only because the channel registration API does not expose it.
/// </summary>
public enum ZLinkLocationAutoConnectType
{
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    DealerMesh = 3,
    Fanout = 4,
    SpotMesh = 5
}

/// <summary>
/// Numeric values are part of the serialized location contract and must remain
/// stable. Value 1 is reserved and is not a valid role.
/// </summary>
public enum ZLinkLocationRole : ushort
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

public enum ZLinkRouteKind
{
    Invalid = 0,
    ActorSession = 1,
    SpotName = 2,
    FrameworkRoute = 3
}

public enum ZLinkLocationKind
{
    Invalid = 0,
    MeshNode = 1,
    Spot = 2,
    Actor = 3
}
