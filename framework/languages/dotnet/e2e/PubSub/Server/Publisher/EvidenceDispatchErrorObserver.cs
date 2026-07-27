using Zlink.Framework.Contracts.Dispatch;

namespace PubSub.Server.Publisher;

public sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence)
    : IZLinkRuntimeMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkRuntimeMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Outcome != "failed") return ValueTask.CompletedTask;

        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|kind={flow.MessageKind}"
            + $"|reason={flow.Reason}"
            + $"|action={flow.Action}"
            + $"|packet={flow.PacketName ?? "<null>"}"
            + $"|channel={flow.ChannelName ?? "<null>"}"
            + $"|topic={flow.Topic ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}
