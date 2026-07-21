namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Returns the mesh's descriptors whose owners are currently live. Each call
/// reflects the current location state. Diagnostics use
/// <see cref="IZLinkLocationRuntimeQuery"/>.
/// </summary>
public interface IZLinkMeshNodeLocationResolver
{
    ValueTask<IReadOnlyList<ZLinkMeshNodeDescriptor>> ListLiveMeshNodesAsync(
        string meshName,
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

    public abstract string MeshName { get; }

    public abstract RoutingId SpotRid { get; }
}

public interface IZLinkSpotHandleResolver
{
    ValueTask<SpotHandle?> ResolveSpotHandleAsync(
        string meshName,
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Resolves an actor id to an opaque handle for its current spot. The framework
/// keeps the handle's location snapshot current and applies the same safe request
/// refresh rule as a handle resolved directly by spot id.
/// </summary>
public interface IZLinkActorSpotHandleResolver
{
    ValueTask<SpotHandle?> ResolveActorSpotHandleAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default);
}
