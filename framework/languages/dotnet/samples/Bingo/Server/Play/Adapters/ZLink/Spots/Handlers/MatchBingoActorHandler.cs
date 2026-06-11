using Systems.Zlink;
using Systems.Zlink.Codecs.Protobuf;
using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Adapters.ZLink.Actors;
using Bingo.Server.Play.Adapters.ZLink.Spots;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.Adapters.ZLink.Spots.Handlers;

internal sealed class MatchBingoActorHandler(ILogger<MatchBingoActorHandler> logger)
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
        var matched = await entrySpot.Context.Outbound.RequestToChannel(
                SampleNames.ApiChannel,
                new MatchBingoApiReq
                {
                    ActorId = actor.ActorId,
                    DisplayName = actor.DisplayName,
                    Mode = message.Mode,
                })
            .Async<MatchBingoApiRes>(cancellationToken)
            ;
        logger.LogInformation("match: room allocated. actor={ActorId}, room={RoomId}", actor.ActorId, matched.RoomId);

        var roomRid = RoutingId.FromHex(matched.RoomId);
        var joined = await actor.Context.JoinSpot(
                roomRid,
                new BingoRoomJoinReq
                {
                    RoomId = matched.RoomId,
                    ActorId = actor.ActorId,
                    DisplayName = actor.DisplayName,
                }.ToProto())
            .Async(cancellationToken)
            ;
        logger.LogInformation("match: actor joined room. actor={ActorId}, room={RoomId}", actor.ActorId, matched.RoomId);

        return new MatchBingoRes
        {
            RoomId = matched.RoomId,
            State = joined.Reply.FromProto<BingoRoomJoinRes>().State,
        };
    }
}
