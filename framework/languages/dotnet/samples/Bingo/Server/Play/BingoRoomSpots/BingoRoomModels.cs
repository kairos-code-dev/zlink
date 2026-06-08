using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Play.Actors;
using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.BingoRoomSpots;

internal static class BingoRoomStatus
{
    public const string WaitingForPlayers = "WaitingForPlayers";
    public const string Running = "Running";
    public const string Finished = "Finished";
}

internal sealed record BingoRoomSettings(
    string RoomName,
    string Mode,
    int RequiredPlayers,
    int MaxDrawNumber)
{
    public static BingoRoomSettings Create(string mode, int roomSeq)
    {
        if (!string.Equals(mode, "two-player", StringComparison.Ordinal))
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

internal sealed record BingoRoomEvent(
    BingoRoomEventKind Kind,
    PlayerActor Recipient,
    BingoRoomState State,
    string? JoinedActorId = null,
    string? JoinedDisplayName = null,
    int Seat = -1,
    bool IsHost = false,
    int DrawnNumber = 0);

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
