using Bingo.Server.Play.Adapters.ZLink.Spots;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;

namespace Bingo.Server.Play.Adapters.ZLink.Spots.Handlers;

internal sealed class BingoRoomDrawTimerHandler : IZLinkSpotTimerHandler<BingoRoom>
{
    public async ValueTask HandleAsync(
        BingoRoom spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        _ = tick;
        var change = spot.DrawNextNumber();
        await spot.PublishAsync(change, cancellationToken);
        if (change.ShouldStopDrawTimer)
        {
            spot.StopDrawTimerAfterTick();
            await spot.LeaveFinishedActorsAsync(cancellationToken);
        }
    }
}
