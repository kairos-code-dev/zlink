using TicTacToe.SessionActorDispatch.Play;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play;

internal sealed class TicTacToeGameJoinHandler
    : IZLinkSpotActorJoinHandler<TicTacToeGameSpot, PlayerActor, JoinMatchReq, JoinMatchSpotResult>
{
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
