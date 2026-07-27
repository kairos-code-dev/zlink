using Zlink.Framework.Contracts.Dispatch;

namespace StoreFailure.Server.Provider.Handlers;

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence, FaultState fault)
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
            + $"|channel={flow.ChannelName ?? "<null>"}");
        if (fault.Mode == "observer-throws") throw new InvalidOperationException("dispatch observer failure");

        return ValueTask.CompletedTask;
    }
}
