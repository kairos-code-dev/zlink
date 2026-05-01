using TicTacToe.SessionActorDispatch.Contracts;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class PlaceMarkHandler(
    TicTacToeGameService games,
    GameNotificationPublisher notifications)
    : IZLinkActorRequestHandler<PlaceMarkReq, PlaceMarkRes>
{
    public async ValueTask<PlaceMarkRes> HandleAsync(
        PlaceMarkReq request,
        ZLinkActorRequestContext context,
        CancellationToken cancellationToken)
    {
        var result = games.PlaceMark(request.MatchId, context.ActorId, request.Cell);
        await notifications.PublishAsync(context.SessionProxy, result.Events, cancellationToken)
            .ConfigureAwait(false);
        return new PlaceMarkRes(result.Snapshot.ToContract());
    }
}
