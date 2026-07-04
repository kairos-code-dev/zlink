namespace Zlink.Framework.Contracts.Channels;

/// <summary>
/// Channel auto-connect modes are a subset of
/// <see cref="Zlink.Framework.Contracts.Locations.ZLinkLocationAutoConnectType"/>.
/// Values that exist in both enums must keep the same numeric values. Value 3
/// is intentionally left unused because the channel registration API does not
/// expose the location-layer DealerMesh mode.
/// </summary>
public enum ZLinkAutoConnectType
{
    /// <summary>Matches ZLinkLocationAutoConnectType.Invalid.</summary>
    Invalid = 0,
    /// <summary>Matches ZLinkLocationAutoConnectType.RouteMesh.</summary>
    RouteMesh = 1,
    /// <summary>Matches ZLinkLocationAutoConnectType.ClientServer.</summary>
    ClientServer = 2,
    /// <summary>Matches ZLinkLocationAutoConnectType.Fanout.</summary>
    Fanout = 4,
    /// <summary>Matches ZLinkLocationAutoConnectType.SpotMesh.</summary>
    SpotMesh = 5
}
