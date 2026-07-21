using ObservabilityOps.Server.Workflow.Support;
using ObservabilityOps.Shared;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace ObservabilityOps.Server.Workflow.Spots;

internal sealed class WorkflowSpot(
    IZLinkSpotContext context,
    WorkflowStateStore states,
    WorkflowEvidenceStore evidence) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;
    public int Version { get; private set; }
    public string State { get; private set; } = "created";
    public async ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _ = request.Decode<CreateWorkflowReq>();
        (Version, State) = await states.LoadAsync(Context.SpotRid.ToString());
        evidence.Add($"workflow-created|rid={Context.SpotRid}|kind=owner|version={Version}|node={Context.NodeRid}");
        return ZLinkSpotCreateResponse.Accept();
    }

    public async ValueTask<AdvanceWorkflowRes> AdvanceAsync(AdvanceWorkflowReq request,
        CancellationToken cancellationToken)
    {
        Version++;
        State = request.Marker;
        await states.SaveAsync(Context.SpotRid.ToString(), Version, State);
        evidence.Add($"workflow-advanced|rid={Context.SpotRid}|version={Version}|marker={request.Marker}|node={Context.NodeRid}");
        return new AdvanceWorkflowRes(Context.SpotRid.ToString(), Context.NodeRid.ToString(), Version, State);
    }

    public async ValueTask<PublishProjectionRes> PublishAsync(PublishProjectionReq request,
        CancellationToken cancellationToken)
    {
        await Context.Outbound.Publish(ObservabilityNames.WorkflowMesh, "observability.projection",
                new ProjectionUpdatedEvent(Context.SpotRid.ToString(), Version, request.Marker))
            .SubmitAsync(cancellationToken);
        evidence.Add($"projection-published|rid={Context.SpotRid}|version={Version}|marker={request.Marker}");
        return new PublishProjectionRes(Context.SpotRid.ToString(), Version);
    }
}
