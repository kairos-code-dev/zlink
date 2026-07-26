using Bingo.Server.Play.Domain.Bingo;

namespace Bingo.Server.Play.Application.RoomAllocation;

internal sealed class BingoRoomAllocator(IBingoMatchQueue matchQueue)
{
    private int _roomSeq;

    public async ValueTask<BingoRoomAllocation> AllocateAsync(
        string mode,
        string actorId,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorId))
            throw new InvalidOperationException("Actor id is required for room allocation.");

        var settings = BingoRoomSettings.Create(mode, Interlocked.Increment(ref _roomSeq));
        var roomId = $"bingo-room-{Guid.NewGuid():N}";
        var reservation = await matchQueue.ReserveAsync(
            mode,
            actorId,
            roomId,
            settings.RequiredPlayers,
            cancellationToken);

        return new BingoRoomAllocation(
            reservation.RoomId,
            string.Equals(reservation.RoomId, roomId, StringComparison.Ordinal) ? settings : null);
    }
}
