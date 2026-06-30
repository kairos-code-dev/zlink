using Zlink.Framework.Contracts.Dispatch;

namespace ResilienceLifecycle.Server.Provider.Handlers;

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence, FaultState fault)
    : IZLinkMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Outcome != ZLinkMessageFlowOutcome.Error) return ValueTask.CompletedTask;

        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|kind={flow.MessageKind}"
            + $"|reason={flow.ErrorReason}"
            + $"|action={flow.ErrorAction}"
            + $"|packet={flow.PacketName ?? "<null>"}"
            + $"|channel={flow.ChannelName ?? "<null>"}");
        if (fault.Mode == "observer-throws") throw new InvalidOperationException("dispatch observer failure");

        return ValueTask.CompletedTask;
    }
}