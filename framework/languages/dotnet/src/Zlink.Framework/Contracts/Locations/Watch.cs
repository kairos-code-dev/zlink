namespace Zlink.Framework.Contracts.Locations;

public enum ZLinkLocationChangeScopeKind
{
    MeshNode = 1,
    Spot = 2,
    Actor = 3,
    OwnerLease = 4
}

public readonly record struct ZLinkLocationChangeStampScope(
    ZLinkLocationChangeScopeKind Kind,
    string? MeshName);
