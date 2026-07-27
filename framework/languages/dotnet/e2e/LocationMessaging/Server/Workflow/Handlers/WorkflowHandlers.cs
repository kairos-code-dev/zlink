using LocationMessaging.Server.Workflow.Infrastructure;
using LocationMessaging.Shared;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;

namespace LocationMessaging.Server.Workflow.Handlers;

internal sealed class WorkflowRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<WorkflowReq, WorkflowRes>
{
    public ValueTask<WorkflowRes> HandleAsync(
        WorkflowReq request,
        IZLinkMessageContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"workflow-request|rid={evidence.Rid}|value={request.Value}|packet={context.PacketName}");
        return ValueTask.FromResult(new WorkflowRes($"workflow:{request.Value}", evidence.Rid));
    }
}

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence)
    : IZLinkRuntimeMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkRuntimeMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Outcome != "failed")
        {
            return ValueTask.CompletedTask;
        }

        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|kind={flow.MessageKind}"
            + $"|reason={flow.Reason}"
            + $"|action={flow.Action}"
            + $"|packet={flow.PacketName ?? "<null>"}"
            + $"|channel={flow.ChannelName ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}
