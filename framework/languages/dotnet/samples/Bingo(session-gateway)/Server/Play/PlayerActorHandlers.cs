using Bingo.SessionGateway.Shared.Configuration;
using Bingo.SessionGateway.Shared.Contracts;

namespace Bingo.SessionGateway.Play;

internal sealed class MatchBingoActorHandler
    : IZLinkEntrySpotActorRequestHandler<PlayerActor, MatchBingoReq, MatchBingoRes>
{
    public async ValueTask<MatchBingoRes> HandleAsync(
        PlayerActor actor,
        MatchBingoReq message,
        CancellationToken cancellationToken)
    {
        var matched = await actor.Context.RequestChannel(
                SampleNames.ApiChannel,
                new MatchBingoApiReq(actor.ActorId, actor.DisplayName, message.Mode))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<MatchBingoApiRes>(cancellationToken)
            .ConfigureAwait(false);

        var roomRid = RoutingId.FromString(matched.RoomId);
        var joined = await actor.Context.JoinSpot(
                roomRid,
                new BingoRoomJoinReq(matched.RoomId, actor.ActorId, actor.DisplayName))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<BingoRoomJoinRes>(cancellationToken)
            .ConfigureAwait(false);

        return new MatchBingoRes(matched.RoomId, joined.State);
    }
}

internal sealed class StartBingoGameHandler
    : IZLinkSpotActorRequestHandler<BingoRoomSpot, PlayerActor, StartBingoGameReq, StartBingoGameRes>
{
    public async ValueTask<StartBingoGameRes> HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        StartBingoGameReq message,
        CancellationToken cancellationToken)
    {
        return await spot.StartAsync(actor, message, cancellationToken).ConfigureAwait(false);
    }
}
