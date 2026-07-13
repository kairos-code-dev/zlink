using Bingo.Server.Configuration;
using Bingo.Server.Play.Infrastructure.ZLink.Actors;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Infrastructure.ZLink.Spots.EntrySpot.Handlers;

internal sealed class MatchBingoActorHandler(
    ILogger<MatchBingoActorHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<BingoEntrySpot, PlayerActor, MatchBingoReq, MatchBingoRes>
{
    public async ValueTask<MatchBingoRes> HandleAsync(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        MatchBingoReq message,
        CancellationToken cancellationToken)
    {
        logger.LogInformation("match: actor request. actor={ActorId}, mode={Mode}", actor.ActorId, message.Mode);
        var apiRequest = new MatchBingoApiReq
        {
            ActorId = actor.ActorId,
            DisplayName = actor.DisplayName,
            ActorNodeRid = entrySpot.Context.NodeRid.ToString(),
            Mode = message.Mode
        };
        var matched = await entrySpot.Context.Outbound
            .RequestToChannel(SampleNames.ApiChannel, apiRequest)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async<MatchBingoApiRes>(cancellationToken);
        logger.LogInformation("match: room allocated. actor={ActorId}, room={RoomId}", actor.ActorId, matched.RoomId);

        var roomRid = RoutingId.From(matched.RoomId);
        var joined = await actor.Context.JoinSpot(
                roomRid,
                new BingoRoomJoinReq
                {
                    RoomId = matched.RoomId,
                    ActorId = actor.ActorId,
                    DisplayName = actor.DisplayName,
                    ObserveOnly = false
                })
            .Async<BingoRoomJoinRes>(cancellationToken);
        logger.LogInformation("match: actor joined room. actor={ActorId}, room={RoomId}", actor.ActorId,
            matched.RoomId);
        var joinedState = joined switch
        {
            ZLinkActorJoinResult<BingoRoomJoinRes>.Accepted accepted => accepted.Reply.State,
            ZLinkActorJoinResult<BingoRoomJoinRes>.Rejected rejected => rejected.Reply.State,
            _ => throw new InvalidOperationException("Unknown actor join result.")
        };
        // The actor may have moved to a remote room, so this source handler does
        // not access the actor again after JoinSpot completes.

        return new MatchBingoRes
        {
            RoomId = matched.RoomId,
            State = joinedState,
            RoomOwnerNodeRid = matched.RoomOwnerNodeRid
        };
    }
}
