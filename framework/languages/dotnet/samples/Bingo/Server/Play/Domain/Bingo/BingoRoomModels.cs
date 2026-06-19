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
    int MaxDrawNumber,
    string Purpose,
    string? ObservedRoomId)
{
    public const string GamePurpose = "Game";
    public const string ObserverPurpose = "Observer";

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
            MaxDrawNumber: 15,
            GamePurpose,
            null);
    }

    public static BingoRoomSettings CreateObserver(string observedRoomId, string localNodeRid)
    {
        if (string.IsNullOrWhiteSpace(observedRoomId))
        {
            throw new InvalidOperationException("Observed room id is required.");
        }

        return new BingoRoomSettings(
            $"Bingo Observer {localNodeRid}",
            BingoSampleModes.TwoPlayer,
            RequiredPlayers: 0,
            MaxDrawNumber: 0,
            ObserverPurpose,
            observedRoomId);
    }

    public bool IsObserver => string.Equals(Purpose, ObserverPurpose, StringComparison.Ordinal);
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
    bool ShouldStopDrawTimer = false);
