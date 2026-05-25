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
    int MaxDrawNumber,
    TimeSpan DrawPeriod)
{
    public static BingoRoomSettings Create(string mode, int roomSeq)
    {
        if (!string.Equals(mode, "four-player", StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Unsupported bingo mode. mode={mode}");
        }

        return new BingoRoomSettings(
            $"Bingo Room {roomSeq:000}",
            mode,
            RequiredPlayers: 4,
            MaxDrawNumber: 75,
            DrawPeriod: SampleTimings.DrawPeriod);
    }
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
    PlayerActor Recipient,
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
