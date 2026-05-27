using TicTacToe.SessionGateway.Server.Play.GameSpots;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play.GameSpots.Handlers;

internal sealed class PlaceMarkHandler(
    GameNotificationPublisher notifications)
{
    [ZLinkSpotActorRequest]
    public async ValueTask<PlaceMarkRes> HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        PlaceMarkReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        var result = spot.PlaceMark(actor.ActorId, request.Cell);
        await notifications.PublishAsync(result.Events, cancellationToken)
            ;
        return new PlaceMarkRes(result.Snapshot.ToContract());
    }
}
