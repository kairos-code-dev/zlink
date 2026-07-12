using Zlink.Framework.Contracts.Dispatch;

namespace PubSub.Server.Subscriber.Handlers;

public sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence)
    : IZLinkMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Outcome is not (ZLinkMessageFlowOutcome.Error or ZLinkMessageFlowOutcome.Dropped))
            return ValueTask.CompletedTask;

        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|kind={flow.MessageKind}"
            + $"|reason={flow.ErrorReason}"
            + $"|action={flow.ErrorAction}"
            + $"|packet={flow.PacketName ?? "<null>"}"
            + $"|channel={flow.ChannelName ?? "<null>"}"
            + $"|topic={flow.Topic ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}
