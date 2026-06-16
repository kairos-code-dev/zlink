using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Domain.Bingo;

internal static class BingoRoomStatus
{
    public const string WaitingForPlayers = BingoRoomStatuses.WaitingForPlayers;
    public const string Running = BingoRoomStatuses.Running;
    public const string Finished = BingoRoomStatuses.Finished;
}

internal sealed record BingoRoomSettings(
    string RoomName,
    string Mode,
    int RequiredPlayers,
    int MaxDrawNumber)
{
    public static BingoRoomSettings Create(string mode, int roomSeq)
    {
        if (!string.Equals(mode, BingoSampleModes.TwoPlayer, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Unsupported bingo mode. mode={mode}");
        }

        return new BingoRoomSettings(
            $"Bingo Room {roomSeq:000}",
            mode,
            RequiredPlayers: 2,
            MaxDrawNumber: 15);
    }
}

internal enum BingoRoomEventKind
{
    PlayerJoined,
    GameStarted,
    NumberDrawn,
    GameEnded,
}

internal sealed record BingoGameEvent(
    BingoRoomEventKind Kind,
    string RecipientActorId,
    BingoRoomState State,
    string? JoinedActorId = null,
    string? JoinedDisplayName = null,
    int Seat = -1,
    bool IsHost = false,
    int DrawnNumber = 0);

internal sealed record BingoGameChange(
    BingoRoomState State,
    IReadOnlyList<BingoGameEvent> Events,
    bool ShouldStartDrawTimer = false,
    bool ShouldStopDrawTimer = false);
