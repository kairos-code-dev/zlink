namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Peer list read surface for auto connect. Every read reaches the store
/// and joins owner liveness; there is no resolver cache. Diagnostics use
/// <see cref="IZLinkLocationRuntimeQuery"/> instead.
/// </summary>
public interface IZLinkPeerLocationResolver
{
    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListLivePeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Messaging lookup: spot rid to its full address. Callers resolve once,
/// hold the address for the spot's lifecycle, and re-resolve on failure.
/// Lifecycle flows that need generations read location rows through the
/// store/runtime surfaces instead.
/// </summary>
public interface IZLinkSpotRefResolver
{
    /// <summary>Null when no live row exists (unknown spot or expired owner lease).</summary>
    ValueTask<SpotRef?> ResolveSpotRefAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Messaging lookup: actor id to the full address of the spot it lives on
/// (the entry spot address for ENTRY_SPOT actors, the user spot address for
/// USER_SPOT actors).
/// </summary>
public interface IZLinkActorAddressResolver
{
    ValueTask<SpotRef?> ResolveActorSpotRefAsync(
        string actorId,
        CancellationToken cancellationToken = default);
}
