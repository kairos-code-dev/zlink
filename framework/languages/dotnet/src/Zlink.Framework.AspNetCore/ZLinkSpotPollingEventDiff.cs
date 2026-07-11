namespace Zlink.Framework.AspNetCore;

internal sealed class ZLinkSpotPollingEventDiff(string sourceName)
{
    private ZLinkSpotMonitoringSnapshot? _previous;

    public void DispatchChanges(
        ZLinkSpotMonitoringSnapshot current,
        DateTimeOffset timestamp,
        Action<ZLinkSpotEvent> dispatch)
    {
        if (_previous is null || _previous.Status != current.Status)
            dispatch(new ZLinkSpotEvent.StatusChanged(sourceName, timestamp, current.Status));

        if (_previous is null || !_previous.Peers.SequenceEqual(current.Peers))
            dispatch(new ZLinkSpotEvent.PeersChanged(sourceName, timestamp, current.Peers));

        if (_previous is null || !_previous.Subjects.SequenceEqual(current.Subjects))
            dispatch(new ZLinkSpotEvent.SubjectsChanged(sourceName, timestamp, current.Subjects));

        _previous = current;
    }
}
