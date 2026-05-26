namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkFrameworkSpotFacade(
    ZLinkSpotRuntimeManager spots,
    Func<ZLinkFrameworkRuntimeState> getState)
{
    public ZLinkSpotPublisherBundle GetPublisherBundle(string channelName)
    {
        return spots.GetPublisherBundle(getState(), channelName);
    }

    public ValueTask<ZLinkSpotCreateResult> CreateAsync(
        Type spotType,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        return spots.CreateAsync(getState(), spotType, createParts, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        Type spotType,
        RoutingId spotRid,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        return spots.GetOrCreateAsync(getState(), spotType, spotRid, createParts, cancellationToken);
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return spots.GetAsync(getState(), spotRid, cancellationToken);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        return spots.ListAsync(getState(), cancellationToken);
    }

    public ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return spots.RemoveAsync(getState(), spotRid, cancellationToken);
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot(string spotNodeName)
    {
        return spots.GetMonitoringSnapshot(getState(), spotNodeName);
    }
}
