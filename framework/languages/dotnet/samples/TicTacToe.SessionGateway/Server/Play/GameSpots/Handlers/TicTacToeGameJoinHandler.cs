using TicTacToe.SessionGateway.Server.Play.Actors;
using TicTacToe.SessionGateway.Server.Play.GameSpots;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.SessionGateway.Server.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameJoinHandler
{
    [ZLinkSpotActorJoin]
    public ValueTask<JoinMatchSpotResult> HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        _ = request;
        return spot.JoinAsync(actor, cancellationToken);
    }
}
