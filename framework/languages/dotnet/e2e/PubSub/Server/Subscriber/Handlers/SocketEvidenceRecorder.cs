using Zlink.Framework.Contracts.Eventing;

namespace PubSub.Server.Subscriber.Handlers;

internal sealed class SocketEvidenceRecorder(EvidenceStore evidence)
    : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(
        ZLinkSocketEvent @event,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            "socket"
            + $"|source={@event.SourceName}"
            + $"|event={@event.Event}"
            + $"|at={@event.Timestamp:O}"
            + $"|remote={@event.RemoteAddr}");
        return ValueTask.CompletedTask;
    }
}
