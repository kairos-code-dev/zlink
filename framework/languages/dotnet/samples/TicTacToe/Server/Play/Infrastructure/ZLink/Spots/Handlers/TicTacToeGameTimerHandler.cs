using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Spots.Handlers;

sealed class TicTacToeGameTimerHandler : IZLinkSpotTimerHandler<TicTacToeGame>
{
    public ValueTask HandleAsync(
        TicTacToeGame spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        _ = tick;
        return spot.TickAsync(cancellationToken);
    }
}
