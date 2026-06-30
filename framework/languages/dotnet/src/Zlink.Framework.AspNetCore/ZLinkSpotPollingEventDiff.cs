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
            dispatch(new ZLinkSpotEvent(
                sourceName,
                timestamp,
                ZLinkSpotEventKind.StatusChanged,
                current.Status,
                null,
                null));

        if (_previous is null || !_previous.Peers.SequenceEqual(current.Peers))
            dispatch(new ZLinkSpotEvent(
                sourceName,
                timestamp,
                ZLinkSpotEventKind.PeersChanged,
                null,
                current.Peers,
                null));

        if (_previous is null || !_previous.Subjects.SequenceEqual(current.Subjects))
            dispatch(new ZLinkSpotEvent(
                sourceName,
                timestamp,
                ZLinkSpotEventKind.SubjectsChanged,
                null,
                null,
                current.Subjects));

        _previous = current;
    }
}