namespace TicTacToe.Server.Play.Games;

sealed class TicTacToeGameTimerHandler : IZLinkSpotTimerHandler<TicTacToeGame>
{
    public ValueTask HandleAsync(
        TicTacToeGame spot,
        CancellationToken cancellationToken)
    {
        return spot.TickAsync(cancellationToken);
    }
}
