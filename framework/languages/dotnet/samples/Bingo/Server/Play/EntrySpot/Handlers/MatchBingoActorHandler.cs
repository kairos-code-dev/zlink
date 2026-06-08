using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Play.Actors;
using Bingo.Server.Play.BingoRoomSpots;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.EntrySpot.Handlers;

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
                new MatchBingoApiReq(actor.ActorId, actor.DisplayName, message.Mode))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<MatchBingoApiRes>(cancellationToken)
            ;
        logger.LogInformation("match: room allocated. actor={ActorId}, room={RoomId}", actor.ActorId, matched.RoomId);

        var roomRid = RoutingId.FromHex(matched.RoomId);
        var joined = await actor.Context.JoinSpot(
                roomRid,
                new BingoRoomJoinReq(matched.RoomId, actor.ActorId, actor.DisplayName).Encode())
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync(cancellationToken)
            ;
        logger.LogInformation("match: actor joined room. actor={ActorId}, room={RoomId}", actor.ActorId, matched.RoomId);

        return new MatchBingoRes(matched.RoomId, joined.Reply.Decode<BingoRoomJoinRes>().State);
    }
}
