using RuntimeMonitoring.Server.Registry.Support;
using Zlink.Framework.Contracts.Eventing;
using RuntimeMonitoring.Server.Registry;

namespace RuntimeMonitoring.Server.Registry.Handlers;

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

internal sealed class RegistryEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkRegistryEvent>
{
    public ValueTask HandleAsync(ZLinkRegistryEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"monitor-registry|source={@event.SourceName}|kind={@event.Event}"
            + $"|topology={@event.Topology?.Count ?? -1}|summary={@event.ServiceSummary?.Count ?? -1}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class SpotEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
{
    public ValueTask HandleAsync(ZLinkSpotEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"monitor-spot|source={@event.SourceName}|kind={@event.Event}"
            + $"|peers={@event.Peers?.Count ?? -1}|subjects={@event.Subjects?.Count ?? -1}"
            + $"|timer={@event.TimerDiagnostic?.TimerName ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}
