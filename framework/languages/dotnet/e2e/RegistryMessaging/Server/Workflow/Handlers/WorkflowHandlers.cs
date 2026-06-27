using RegistryMessaging.Server.Workflow.Infrastructure;
using RegistryMessaging.Shared;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;

namespace RegistryMessaging.Server.Workflow.Handlers;

internal sealed class WorkflowRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<WorkflowRequest, WorkflowReply>
{
    public ValueTask<WorkflowReply> HandleAsync(
        WorkflowRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"workflow-request|rid={evidence.Rid}|value={request.Value}|packet={context.PacketName}");
        return ValueTask.FromResult(new WorkflowReply($"workflow:{request.Value}", evidence.Rid));
    }
}

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence)
    : IZLinkMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Outcome != ZLinkMessageFlowOutcome.Error)
        {
            return ValueTask.CompletedTask;
        }

        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|kind={flow.MessageKind}"
            + $"|reason={flow.ErrorReason}"
            + $"|action={flow.ErrorAction}"
            + $"|packet={flow.PacketName ?? "<null>"}"
            + $"|channel={flow.ChannelName ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}
