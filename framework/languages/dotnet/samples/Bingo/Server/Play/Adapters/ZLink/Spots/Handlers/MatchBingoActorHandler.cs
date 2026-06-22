using Systems.Zlink;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Adapters.ZLink.Spots;
using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.Adapters.ZLink.Spots.Handlers;

[ZLinkSpotActorRequestHandler(nameof(MatchBingoReq))]
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
        _ = context;
        logger.LogInformation("match: actor request. actor={ActorId}, mode={Mode}", actor.ActorId, message.Mode);
        var apiRequest = new MatchBingoApiReq
        {
            ActorId = actor.ActorId,
            DisplayName = actor.DisplayName,
            ActorNodeRid = entrySpot.Context.NodeRid.ToString(),
            Mode = message.Mode,
        };
        MatchBingoApiRes matched;
        try
        {
            matched = await entrySpot.Context.Outbound
                .RequestToChannel(SampleNames.ApiChannel, apiRequest)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<MatchBingoApiRes>(cancellationToken);
        }
        catch (Exception ex)
        {
            logger.LogError(ex, "match: api allocation failed. actor={ActorId}", actor.ActorId);
            throw;
        }
        logger.LogInformation("match: room allocated. actor={ActorId}, room={RoomId}", actor.ActorId, matched.RoomId);

        var roomRid = RoutingId.From(matched.RoomId);
        var joined = await actor.Context.JoinSpot(
                roomRid,
                new BingoRoomJoinReq
                {
                    RoomId = matched.RoomId,
                    ActorId = actor.ActorId,
                    DisplayName = actor.DisplayName,
                    ObserveOnly = false,
                }.ToProto())
            .Async(cancellationToken)
            ;
        logger.LogInformation("match: actor joined room. actor={ActorId}, room={RoomId}", actor.ActorId, matched.RoomId);
        var joinedState = joined.Reply.FromProto<BingoRoomJoinRes>().State;
        if (joinedState.Status == BingoRoomStatuses.Running)
        {
            await actor.Context.BoundSession
                .Send(new BingoGameStartedNotify { State = joinedState })
                .Async(cancellationToken);
        }

        return new MatchBingoRes
        {
            RoomId = matched.RoomId,
            State = joinedState,
            RoomOwnerNodeRid = matched.RoomOwnerNodeRid,
        };
    }
}
