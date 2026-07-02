namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Raw location store registration collected by the options builder. The
/// five store roles must share one physical store, so registration is
/// all-or-nothing: either every role has an implementation type or the
/// in-memory store backs them all.
/// </summary>
internal sealed class ZLinkLocationRegistration
{
    public Type? PeerStoreType { get; set; }

    public Type? SpotStoreType { get; set; }

    public Type? ActorStoreType { get; set; }

    public Type? RouteStoreType { get; set; }

    public Type? OwnerLeaseStoreType { get; set; }

    public bool UseInMemoryStores { get; set; }

    public ZLinkLocationOptions Options { get; } = new();

    public bool HasAnyStoreType =>
        PeerStoreType is not null
        || SpotStoreType is not null
        || ActorStoreType is not null
        || RouteStoreType is not null
        || OwnerLeaseStoreType is not null;

    public bool HasAllStoreTypes =>
        PeerStoreType is not null
        && SpotStoreType is not null
        && ActorStoreType is not null
        && RouteStoreType is not null
        && OwnerLeaseStoreType is not null;

    public bool Enabled => UseInMemoryStores || HasAnyStoreType;
}
