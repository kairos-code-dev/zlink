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

        dispatch(new ZLinkLocationRuntimeEvent(
            sourceName,
            timestamp,
            ZLinkLocationRuntimeEventKind.StoreFailure,
            _previous?.Status,
            null,
            null));
    }

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
        var previousHealthy = !_captureFailed && (_previous?.Status.StoreHealthy ?? true);
        _captureFailed = false;
        if (previousHealthy && !current.Status.StoreHealthy)
            dispatch(new ZLinkLocationRuntimeEvent(
                sourceName,
                timestamp,
                ZLinkLocationRuntimeEventKind.StoreFailure,
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
