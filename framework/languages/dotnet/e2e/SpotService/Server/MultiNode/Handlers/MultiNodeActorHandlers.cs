using SpotService.Server.MultiNode.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace SpotService.Server.MultiNode.Handlers;

[ZLinkSpotActorRequestHandler("SpotOnlyJoinReq")]
internal sealed class SpotOnlyJoinHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SpotOnlyJoinReq, SpotOnlyJoinRes>
{
    public async ValueTask<SpotOnlyJoinRes> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        SpotOnlyJoinReq request,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        if (!string.Equals(actor.ActorId, request.ActorId, StringComparison.Ordinal))
            throw new InvalidOperationException("Spot-only join request actor does not match dispatched actor.");

        var joined = await actor.Context.JoinSpot(
                RoutingId.From(request.TargetSpotRid),
                ZLinkMessage.Empty)
            .Async(cancellationToken)
            .ConfigureAwait(false);
        evidence.Add(
            $"spot-only-actor-join|rid={evidence.Rid}|actor={actor.ActorId}"
            + $"|target={request.TargetSpotRid}|accepted={joined is ZLinkActorJoinResult.Accepted}|marker={request.Marker}");
        return new SpotOnlyJoinRes(
            request.TargetSpotRid,
            actor.ActorId,
            joined is ZLinkActorJoinResult.Accepted,
            request.Marker);
    }
}

[ZLinkSpotRequestHandler("StateReq")]
internal sealed class SpotOnlyStateReqHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<SpotOnlyUserSpot, StateReq, StateRes>
{
    public ValueTask<StateRes> HandleAsync(
        SpotOnlyUserSpot spot,
        StateReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var value = request.Operation == "add" ? spot.Add(request.Delta) : 0;
        evidence.Add($"spot-state-request|rid={evidence.Rid}|spot={spot.Context.SpotId}|value={value}");
        return ValueTask.FromResult(new StateRes(
            spot.Context.SpotId.ToString(),
            spot.Context.NodeRid.ToString(),
            value));
    }
}

[ZLinkSpotPacketHandler("StateMsg")]
internal sealed class SpotOnlyStateMsgHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<SpotOnlyUserSpot, StateMsg>
{
    public ValueTask HandleAsync(
        SpotOnlyUserSpot spot,
        StateMsg message,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-state-command|rid={evidence.Rid}|spot={spot.Context.SpotId}|marker={message.Marker}");
        return ValueTask.CompletedTask;
    }
}
