namespace Zlink.Framework.Runtime.Host;

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
        Message request,
        CancellationToken cancellationToken)
    {
        return spots.CreateAsync(getState(), spotType, request, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        Type spotType,
        RoutingId spotRid,
        Message request,
        CancellationToken cancellationToken)
    {
        return spots.GetOrCreateAsync(getState(), spotType, spotRid, request, cancellationToken);
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

    public ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return spots.CloseAsync(getState(), spotRid, cancellationToken);
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot(string spotNodeName)
    {
        return spots.GetMonitoringSnapshot(getState(), spotNodeName);
    }
}
