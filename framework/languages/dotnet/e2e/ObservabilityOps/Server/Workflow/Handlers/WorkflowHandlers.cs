using ObservabilityOps.Server.Workflow.Spots;
using ObservabilityOps.Server.Workflow.Support;
using ObservabilityOps.Shared;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace ObservabilityOps.Server.Workflow.Handlers;

internal sealed class AdvanceWorkflowHandler
    : IZLinkSpotRequestHandler<WorkflowSpot, AdvanceWorkflowReq, AdvanceWorkflowRes>
{
    public ValueTask<AdvanceWorkflowRes> HandleAsync(WorkflowSpot spot, AdvanceWorkflowReq request,
        CancellationToken cancellationToken) => spot.AdvanceAsync(request, cancellationToken);
}

internal sealed class ReadWorkflowHandler
    : IZLinkSpotRequestHandler<WorkflowSpot, ReadWorkflowReq, ReadWorkflowRes>
{
    public ValueTask<ReadWorkflowRes> HandleAsync(WorkflowSpot spot, ReadWorkflowReq request,
        CancellationToken cancellationToken)
    {
        _ = request;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new ReadWorkflowRes(
            spot.Context.SpotRid.ToString(), spot.Context.NodeRid.ToString(), spot.Version, spot.State));
    }
}

internal sealed class WorkflowSignalHandler(WorkflowEvidenceStore evidence)
    : IZLinkSpotPacketHandler<WorkflowSpot, WorkflowSignalReq>
{
    public ValueTask HandleAsync(WorkflowSpot spot, WorkflowSignalReq message,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"workflow-signal|rid={spot.Context.SpotRid}|marker={message.Marker}"
                     + $"|node={spot.Context.NodeRid}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class PublishProjectionHandler
    : IZLinkSpotRequestHandler<WorkflowSpot, PublishProjectionReq, PublishProjectionRes>
{
    public ValueTask<PublishProjectionRes> HandleAsync(WorkflowSpot spot, PublishProjectionReq request,
        CancellationToken cancellationToken) => spot.PublishAsync(request, cancellationToken);
}

internal sealed class ProjectionUpdatedHandler(WorkflowEvidenceStore evidence)
    : IZLinkSpotSubscriptionHandler<ProjectionSpot, ProjectionUpdatedEvent>
{
    public ValueTask HandleAsync(ProjectionSpot spot, ProjectionUpdatedEvent message,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"projection-received|subscriber={spot.Context.SpotRid}|rid={message.WorkflowRid}"
                     + $"|version={message.Version}|marker={message.Marker}|node={spot.Context.NodeRid}");
        return ValueTask.CompletedTask;
    }
}
