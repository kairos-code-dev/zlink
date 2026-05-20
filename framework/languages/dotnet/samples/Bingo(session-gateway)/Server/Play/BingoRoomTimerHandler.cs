namespace Bingo.SessionGateway.Play;

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
