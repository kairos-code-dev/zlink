using ObservabilityOps.Server.Play.Infrastructure;
using ObservabilityOps.Server.Play.Spots;
using ObservabilityOps.Server.Play.Support;
using ObservabilityOps.Server.Support;
using ObservabilityOps.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace ObservabilityOps.Server.Play.Handlers;

internal sealed class EnsurePlayerHandler(IZLinkActorManager actors)
    : IZLinkSpotRequestHandler<PlayEntrySpot, EnsurePlayerReq, EnsurePlayerRes>
{
    public async ValueTask<EnsurePlayerRes> HandleAsync(PlayEntrySpot spot, EnsurePlayerReq request,
        CancellationToken cancellationToken)
    {
        _ = spot;
        var actor = (await actors.GetOrCreate(request.ActorId, ObservabilityNames.PlayerActorType)
            .Request(request).Async(cancellationToken)) switch
        {
            ZLinkActorCreateResult.Existing value => value.Actor,
            ZLinkActorCreateResult.Created value => value.Actor,
            _ => throw new InvalidOperationException("Actor creation was rejected.")
        };
        return new EnsurePlayerRes(actor.ActorId, actor.NodeRid.ToString(), actor.Generation);
    }
}

internal sealed class PlayBoundedOperationHandler(BoundedOperationGate gate)
    : IZLinkSpotRequestHandler<PlayEntrySpot, PlayBoundedOperationReq, PlayBoundedOperationRes>
{
    public async ValueTask<PlayBoundedOperationRes> HandleAsync(
        PlayEntrySpot spot,
        PlayBoundedOperationReq request,
        CancellationToken cancellationToken)
    {
        await gate.EnterAsync(cancellationToken);
        return new PlayBoundedOperationRes(request.Marker, spot.Context.NodeRid.ToString());
    }
}

internal sealed class JoinRoomHandler
    : IZLinkEntrySpotActorRequestHandler<PlayEntrySpot, PlayerActor, JoinRoomReq, JoinRoomRes>
{
    public async ValueTask<JoinRoomRes> HandleAsync(PlayEntrySpot spot, PlayerActor actor,
        ZLinkSpotActorRequestContext context, JoinRoomReq request, CancellationToken cancellationToken)
    {
        _ = spot;
        _ = context;
        return await PlayerRoomJoin.JoinAsync(actor, request, cancellationToken);
    }
}

internal sealed class MoveRoomHandler
    : IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, JoinRoomReq, JoinRoomRes>
{
    public async ValueTask<JoinRoomRes> HandleAsync(RoomSpot spot, PlayerActor actor,
        ZLinkSpotActorRequestContext context, JoinRoomReq request, CancellationToken cancellationToken)
    {
        _ = spot;
        _ = context;
        return await PlayerRoomJoin.JoinAsync(actor, request, cancellationToken);
    }
}

internal static class PlayerRoomJoin
{
    internal static async ValueTask<JoinRoomRes> JoinAsync(
        PlayerActor actor,
        JoinRoomReq request,
        CancellationToken cancellationToken)
    {
        var joined = await actor.Context.JoinSpot(RoutingId.From(request.RoomRid), request)
            .Async<JoinRoomRes>(cancellationToken);
        return joined switch
        {
            ZLinkActorJoinResult<JoinRoomRes>.Accepted accepted => accepted.Reply,
            ZLinkActorJoinResult<JoinRoomRes>.Rejected rejected => rejected.Reply,
            _ => throw new InvalidOperationException("Unknown room join result.")
        };
    }
}

internal sealed class ReturnToLobbyHandler
    : IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, ReturnToLobbyReq, ReturnToLobbyRes>
{
    public async ValueTask<ReturnToLobbyRes> HandleAsync(RoomSpot spot, PlayerActor actor,
        ZLinkSpotActorRequestContext context, ReturnToLobbyReq request, CancellationToken cancellationToken)
    {
        _ = context;
        var joined = await actor.Context.JoinEntrySpot(spot.Context.NodeRid, request)
            .Async(cancellationToken);
        if (joined is not ZLinkActorJoinResult.Accepted)
            throw new InvalidOperationException("Actor could not return to its Entry Spot.");
        actor.Player.ReturnToLobby();
        return new ReturnToLobbyRes(actor.ActorId, spot.Context.NodeRid.ToString(), request.Marker);
    }
}

internal sealed class GameActionHandler(EvidenceStore evidence)
    : IZLinkSpotActorRequestHandler<RoomSpot, PlayerActor, GameActionReq, GameActionRes>
{
    public async ValueTask<GameActionRes> HandleAsync(RoomSpot spot, PlayerActor actor,
        ZLinkSpotActorRequestContext context, GameActionReq request, CancellationToken cancellationToken)
    {
        _ = context;
        if (request.WorkMilliseconds > 0)
            await Task.Delay(TimeSpan.FromMilliseconds(request.WorkMilliseconds), cancellationToken);
        evidence.Add($"game-action|actor={actor.ActorId}|room={spot.Context.SpotRid}|marker={request.Marker}");
        return new GameActionRes(actor.ActorId, spot.Context.SpotRid.ToString(),
            spot.Context.NodeRid.ToString(), request.Marker);
    }
}
