using Bingo.Server.Play.Domain.Bingo;

namespace Bingo.Server.Play.Application.RoomAllocation;

internal sealed record BingoMatchReservation(string RoomId);

internal sealed record BingoRoomAllocation(
    string RoomId,
    BingoRoomSettings? NewRoomSettings);

internal interface IBingoMatchQueue
{
    ValueTask<BingoMatchReservation> ReserveAsync(
        string mode,
        string actorId,
        string newRoomId,
        int requiredPlayers,
        CancellationToken cancellationToken);
}
