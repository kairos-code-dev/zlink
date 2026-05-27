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

namespace Bingo.Server.Play.EntrySpot.Handlers;

internal sealed class MatchBingoActorHandler
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
        var matched = await entrySpot.Context.RequestChannel(
                SampleNames.ApiChannel,
                new MatchBingoApiReq(actor.ActorId, actor.DisplayName, message.Mode))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<MatchBingoApiRes>(cancellationToken)
            ;

        var roomRid = RoutingId.From(matched.RoomId);
        var joined = await actor.Context.JoinSpot(
                roomRid,
                new BingoRoomJoinReq(matched.RoomId, actor.ActorId, actor.DisplayName))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<BingoRoomJoinRes>(cancellationToken)
            ;

        return new MatchBingoRes(matched.RoomId, joined.Reply.State);
    }
}
