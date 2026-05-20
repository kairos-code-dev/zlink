using Bingo.SessionGateway.Shared.Contracts;

namespace Bingo.SessionGateway.Play;

internal static class BingoRoomStatus
{
    public const string WaitingForPlayers = "WaitingForPlayers";
    public const string Running = "Running";
    public const string Finished = "Finished";
}

internal enum BingoRoomEventKind
{
    PlayerJoined,
    GameStarted,
    NumberDrawn,
    State,
    GameEnded,
}

internal sealed record BingoRoomEvent(
    BingoRoomEventKind Kind,
    string RecipientActorId,
    BingoRoomState State,
    string? JoinedActorId = null,
    string? JoinedDisplayName = null,
    int Seat = -1,
    bool IsHost = false,
    int DrawnNumber = 0);

internal sealed record BingoRoomJoinResult(
    BingoRoomState State,
    IReadOnlyList<BingoRoomEvent> Events);

internal sealed record BingoRoomStartResult(
    BingoRoomState State,
    IReadOnlyList<BingoRoomEvent> Events);

internal sealed record BingoRoomDrawResult(
    BingoRoomState State,
    IReadOnlyList<BingoRoomEvent> Events);
