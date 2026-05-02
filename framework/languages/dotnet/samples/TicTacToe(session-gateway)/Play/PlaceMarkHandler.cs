using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionGateway.Play;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class PlaceMarkHandler(
    GameNotificationPublisher notifications)
    : IZLinkActorRequestHandler<PlayerActor, PlaceMarkReq, PlaceMarkRes>
{
    public async ValueTask<PlaceMarkRes> HandleAsync(
        PlayerActor actor,
        PlaceMarkReq request,
        CancellationToken cancellationToken)
    {
        var game = actor.Context.GetSpot<TicTacToeGameSpot>();
        var result = game.PlaceMark(actor.ActorId, request.Cell);
        await notifications.PublishAsync(result.Events, cancellationToken)
            .ConfigureAwait(false);
        return new PlaceMarkRes(result.Snapshot.ToContract());
    }
}
