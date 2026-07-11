namespace Zlink.Framework.AspNetCore;

/// <summary>
/// Turns location runtime polling snapshots into monitoring events: the
/// first snapshot publishes everything, later snapshots publish only the
/// sections whose content changed (draft 20.5 location-runtime source).
/// </summary>
internal sealed class ZLinkLocationRuntimePollingEventDiff(string sourceName)
{
    private ZLinkLocationRuntimePollingSnapshot? _previous;
    private bool _captureFailed;

    /// <summary>
    /// A store outage keeps the last projection (fail-static) and surfaces
    /// as a single StoreFailure event; the next successful capture
    /// reports StoreRecovered through the regular health transition.
    /// </summary>
    public void DispatchCaptureFailure(
        DateTimeOffset timestamp,
        Action<ZLinkLocationRuntimeEvent> dispatch)
    {
        var wasHealthy = !_captureFailed && (_previous?.Status.StoreHealthy ?? true);
        _captureFailed = true;
        if (!wasHealthy) return;

        dispatch(new ZLinkLocationRuntimeEvent.StoreFailure(sourceName, timestamp));
    }

    public void DispatchChanges(
        ZLinkLocationRuntimePollingSnapshot current,
        DateTimeOffset timestamp,
        Action<ZLinkLocationRuntimeEvent> dispatch)
    {
        if (_previous is null || _previous.Status != current.Status)
            dispatch(new ZLinkLocationRuntimeEvent.StatusChanged(sourceName, timestamp, current.Status));

        // A healthy store is the baseline: the first snapshot only reports
        // an outage, and later snapshots report health transitions.
        var previousHealthy = !_captureFailed && (_previous?.Status.StoreHealthy ?? true);
        _captureFailed = false;
        if (previousHealthy && !current.Status.StoreHealthy)
            dispatch(new ZLinkLocationRuntimeEvent.StoreFailure(sourceName, timestamp));
        else if (!previousHealthy && current.Status.StoreHealthy)
            dispatch(new ZLinkLocationRuntimeEvent.StoreRecovered(sourceName, timestamp));

        if (_previous is null || !_previous.Topology.SequenceEqual(current.Topology))
            dispatch(new ZLinkLocationRuntimeEvent.TopologyChanged(sourceName, timestamp, current.Topology));

        if (_previous is null || !_previous.ServiceSummary.SequenceEqual(current.ServiceSummary))
            dispatch(new ZLinkLocationRuntimeEvent.ServiceSummaryChanged(
                sourceName,
                timestamp,
                current.ServiceSummary));

        _previous = current;
    }
}
