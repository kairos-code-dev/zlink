using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class BingoRoomTimerHandler : IZLinkSpotTimerHandler<BingoRoomSpot>
{
    public ValueTask HandleAsync(
        BingoRoomSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        _ = tick;
        return spot.TickAsync(cancellationToken);
    }
}
