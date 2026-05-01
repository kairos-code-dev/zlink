using TicTacToe.SessionActorDispatch.Contracts;
using Zlink.Framework.Spots;

namespace TicTacToe.SessionActorDispatch.Play;

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
