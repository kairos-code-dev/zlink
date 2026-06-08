using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class BingoRoomDrawTimerHandler : IZLinkSpotTimerHandler<BingoRoomSpot>
{
    public async ValueTask HandleAsync(
        BingoRoomSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        _ = tick;
        var change = spot.DrawNextNumber();
        await spot.PublishAsync(change, cancellationToken);
        if (change.ShouldStopDrawTimer)
        {
            spot.StopDrawTimerAfterTick();
        }
    }
}
