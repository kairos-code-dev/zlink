using SpotService.Shared;
using Zlink.Framework.Contracts.Spots;

namespace SpotService.Server.Play.Spots;

internal sealed class ScenarioInstanceSpot(
    IZLinkInstanceSpotContext context,
    EvidenceStore evidence) : IZLinkInstanceSpot
{
    public IZLinkInstanceSpotContext Context { get; } = context;

    public ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"instance-initialize|rid={evidence.Rid}|spot={Context.SpotId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnClosingAsync(
        ZLinkSpotClosingContext context,
        CancellationToken cleanupCancellationToken)
    {
        _ = context;
        cleanupCancellationToken.ThrowIfCancellationRequested();
        return ValueTask.CompletedTask;
    }
}

internal sealed class ScenarioInstanceStateHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<ScenarioInstanceSpot, StateReq, StateRes>
{
    public ValueTask<StateRes> HandleAsync(
        ScenarioInstanceSpot spot,
        StateReq request,
        CancellationToken cancellationToken)
    {
        _ = request;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"instance-request|rid={evidence.Rid}|spot={spot.Context.SpotId}");
        return ValueTask.FromResult(new StateRes(
            spot.Context.SpotId,
            spot.Context.NodeRid.ToString(),
            0));
    }
}
