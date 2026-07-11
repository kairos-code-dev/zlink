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
/// Messaging lookup handle for a spot. Callers resolve once and keep this
/// opaque handle. The framework updates its internal address from location
/// changes; a request refreshes and retries once only when the target is
/// known not to have handled the first attempt. One-way sends are never
/// retried. Lifecycle flows that need generations read location rows through
/// the store/runtime surfaces instead.
/// </summary>
public abstract class SpotHandle
{
    internal SpotHandle()
    {
    }

    public abstract RoutingId SpotRid { get; }
}

public interface IZLinkSpotHandleResolver
{
    ValueTask<SpotHandle?> ResolveSpotHandleAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Messaging lookup: actor id to the full address of the spot that contains it
/// (the entry spot address for ENTRY_SPOT actors, the user spot address for
/// USER_SPOT actors).
/// </summary>
public interface IZLinkActorSpotHandleResolver
{
    ValueTask<SpotHandle?> ResolveActorSpotHandleAsync(
        string actorId,
        CancellationToken cancellationToken = default);
}
