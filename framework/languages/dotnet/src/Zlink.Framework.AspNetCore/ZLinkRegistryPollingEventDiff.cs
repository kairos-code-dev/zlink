namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkRegistryPollingEventDiff(string sourceName)
{
    private ZLinkRegistryPollingSnapshot? _previous;

    public void DispatchChanges(
        ZLinkRegistryPollingSnapshot current,
        DateTimeOffset timestamp,
        Action<ZLinkRegistryEvent> dispatch)
    {
        if (_previous is null || _previous.Status != current.Status)
            dispatch(new ZLinkRegistryEvent(
                sourceName,
                timestamp,
                ZLinkRegistryEventKind.StatusChanged,
                current.Status,
                null,
                null));

        if (_previous is null || !_previous.Topology.SequenceEqual(current.Topology))
            dispatch(new ZLinkRegistryEvent(
                sourceName,
                timestamp,
                ZLinkRegistryEventKind.TopologyChanged,
                null,
                current.Topology,
                null));

        if (_previous is null || !_previous.ServiceSummary.SequenceEqual(current.ServiceSummary))
            dispatch(new ZLinkRegistryEvent(
                sourceName,
                timestamp,
                ZLinkRegistryEventKind.ServiceSummaryChanged,
                null,
                null,
                current.ServiceSummary));

        _previous = current;
    }
}