using Bingo.Server.Play.Domain.Bingo;

namespace Bingo.Server.Play.Application.RoomAllocation;

internal sealed record BingoMatchReservation(
    string RoomId,
    string OwnerPlayNodeRid,
    BingoRoomSettings? LocalRoomSettings);

internal interface IBingoMatchQueue
{
    ValueTask<BingoMatchReservation> ReserveAsync(
        string mode,
        string actorId,
        string preferredOwnerNodeRid,
        string newRoomId,
        int requiredPlayers,
        CancellationToken cancellationToken);
}