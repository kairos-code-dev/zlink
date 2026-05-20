using TicTacToe.SessionGateway.Play;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class PlaceMarkHandler(
    GameNotificationPublisher notifications)
    : IZLinkSpotActorRequestHandler<TicTacToeGameSpot, PlayerActor, PlaceMarkReq, PlaceMarkRes>
{
    public async ValueTask<PlaceMarkRes> HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        PlaceMarkReq request,
        CancellationToken cancellationToken)
    {
        var result = spot.PlaceMark(actor.ActorId, request.Cell);
        await notifications.PublishAsync(result.Events, cancellationToken)
            .ConfigureAwait(false);
        return new PlaceMarkRes(result.Snapshot.ToContract());
    }
}
