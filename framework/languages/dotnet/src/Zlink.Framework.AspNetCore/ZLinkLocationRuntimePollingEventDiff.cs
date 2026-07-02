namespace Zlink.Framework.AspNetCore;

/// <summary>
/// Turns location runtime polling snapshots into monitoring events: the
/// first snapshot publishes everything, later snapshots publish only the
/// sections whose content changed (draft 20.5 location-runtime source).
/// </summary>
internal sealed class ZLinkLocationRuntimePollingEventDiff(string sourceName)
{
    private ZLinkLocationRuntimePollingSnapshot? _previous;

    public void DispatchChanges(
        ZLinkLocationRuntimePollingSnapshot current,
        DateTimeOffset timestamp,
        Action<ZLinkLocationRuntimeEvent> dispatch)
    {
        if (_previous is null || _previous.Status != current.Status)
            dispatch(new ZLinkLocationRuntimeEvent(
                sourceName,
                timestamp,
                ZLinkLocationRuntimeEventKind.StatusChanged,
                current.Status,
                null,
                null));

        // A healthy store is the baseline: the first snapshot only reports
        // an outage, and later snapshots report health transitions.
        var previousHealthy = _previous?.Status.StoreHealthy ?? true;
        if (previousHealthy && !current.Status.StoreHealthy)
            dispatch(new ZLinkLocationRuntimeEvent(
                sourceName,
                timestamp,
                ZLinkLocationRuntimeEventKind.StoreUnavailable,
                current.Status,
                null,
                null));
        else if (!previousHealthy && current.Status.StoreHealthy)
            dispatch(new ZLinkLocationRuntimeEvent(
                sourceName,
                timestamp,
                ZLinkLocationRuntimeEventKind.StoreRecovered,
                current.Status,
                null,
                null));

        if (_previous is null || !_previous.Topology.SequenceEqual(current.Topology))
            dispatch(new ZLinkLocationRuntimeEvent(
                sourceName,
                timestamp,
                ZLinkLocationRuntimeEventKind.TopologyChanged,
                null,
                current.Topology,
                null));

        if (_previous is null || !_previous.ServiceSummary.SequenceEqual(current.ServiceSummary))
            dispatch(new ZLinkLocationRuntimeEvent(
                sourceName,
                timestamp,
                ZLinkLocationRuntimeEventKind.ServiceSummaryChanged,
                null,
                null,
                current.ServiceSummary));

        _previous = current;
    }
}
