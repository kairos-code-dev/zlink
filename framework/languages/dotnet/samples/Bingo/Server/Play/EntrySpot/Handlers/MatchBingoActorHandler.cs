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
        MatchBingoReq message,
        CancellationToken cancellationToken)
    {
        var matched = await entrySpot.Context.RequestChannel(
                SampleNames.ApiChannel,
                new MatchBingoApiReq(actor.ActorId, actor.DisplayName, message.Mode))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<MatchBingoApiRes>(cancellationToken)
            ;

        var roomRid = RoutingId.FromString(matched.RoomId);
        var joined = await actor.Context.JoinSpot(
                roomRid,
                new BingoRoomJoinReq(matched.RoomId, actor.ActorId, actor.DisplayName))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<BingoRoomJoinRes>(cancellationToken)
            ;

        return new MatchBingoRes(matched.RoomId, joined.State);
    }
}
