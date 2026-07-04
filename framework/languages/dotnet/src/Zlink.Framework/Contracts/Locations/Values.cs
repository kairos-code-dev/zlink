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
/// Matches the core discovery uint16_t service_role values. Value 1 is the
/// reserved slot for the removed gateway role; numeric values are serialized
/// on the wire and in Redis row JSON, so they must not change.
/// Store keys use the internal canonical string codec.
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
    Peer = 1,
    Spot = 2,
    Actor = 3,
    Route = 4
}
