using Bingo.Server.Play.Domain.Bingo;

namespace Bingo.Server.Play.Application.RoomAllocation;

internal sealed class BingoRoomAllocator(IBingoMatchQueue matchQueue)
{
    private int _roomSeq;

    public async ValueTask<BingoMatchReservation> AllocateAsync(
        string mode,
        string actorId,
        string preferredOwnerNodeRid,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Actor id is required for room allocation.");
        }

        if (string.IsNullOrWhiteSpace(preferredOwnerNodeRid))
        {
            throw new InvalidOperationException("Preferred owner node rid is required for room allocation.");
        }

        var settings = BingoRoomSettings.Create(mode, Interlocked.Increment(ref _roomSeq));
        var roomId = $"bingo-room-{Guid.NewGuid():N}";
        var reservation = await matchQueue.ReserveAsync(
            mode,
            actorId,
            preferredOwnerNodeRid,
            roomId,
            settings.RequiredPlayers,
            cancellationToken);

        if (string.Equals(reservation.OwnerPlayNodeRid, preferredOwnerNodeRid, StringComparison.Ordinal)
            && string.Equals(reservation.RoomId, roomId, StringComparison.Ordinal))
        {
            return reservation with { LocalRoomSettings = settings };
        }

        return reservation;
    }
}
