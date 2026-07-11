using RuntimeMonitoring.Server.Service.Support;
using Zlink.Framework.Contracts.Eventing;

namespace RuntimeMonitoring.Server.Service.Handlers;

internal sealed class SocketEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"monitor-socket|source={@event.SourceName}|kind={@event.Event}"
            + $"|remote={@event.RemoteAddr}|routing={@event.RoutingId}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ThrowingSocketEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"monitor-throw|source={@event.SourceName}|kind={@event.Event}");
        throw new InvalidOperationException("monitoring dispatch failure for e2e");
    }
}

internal sealed class LocationRuntimeEventRecorder(EvidenceStore evidence)
    : IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>
{
    public ValueTask HandleAsync(ZLinkLocationRuntimeEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var topologyCount = @event is ZLinkLocationRuntimeEvent.TopologyChanged topology
            ? topology.Topology.Count
            : -1;
        var summaryCount = @event is ZLinkLocationRuntimeEvent.ServiceSummaryChanged summary
            ? summary.ServiceSummary.Count
            : -1;
        evidence.Add(
            $"monitor-location-runtime|source={@event.SourceName}|kind={@event.GetType().Name}"
            + $"|topology={topologyCount}|summary={summaryCount}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class SpotEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
{
    public ValueTask HandleAsync(ZLinkSpotEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var peerCount = @event is ZLinkSpotEvent.PeersChanged peers ? peers.Peers.Count : -1;
        var subjectCount = @event is ZLinkSpotEvent.SubjectsChanged subjects ? subjects.Subjects.Count : -1;
        var timerName = @event switch
        {
            ZLinkSpotEvent.TimerHandlerFailed failed => failed.Diagnostic.TimerName,
            ZLinkSpotEvent.TimerStoppedAfterUnhandledException stopped => stopped.Diagnostic.TimerName,
            _ => "<null>"
        };
        evidence.Add(
            $"monitor-spot|source={@event.SourceName}|kind={@event.GetType().Name}"
            + $"|peers={peerCount}|subjects={subjectCount}"
            + $"|timer={timerName}");
        return ValueTask.CompletedTask;
    }
}
