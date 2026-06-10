using MessagePack;

namespace TicTacToe.Shared.Contracts;

[MessagePackObject]
public sealed record CreateGameHttpReq([property: Key(0)] string? GameName);

[MessagePackObject]
public sealed record CreateGameHttpRes(
    [property: Key(0)] string RoomId,
    [property: Key(1)] string PlayEndpoint,
    [property: Key(2)] string GameName);

[MessagePackObject]
public sealed record CreateGameReq([property: Key(0)] string GameName);

[MessagePackObject]
public sealed record CreateGameRes(
    [property: Key(0)] string RoomId,
    [property: Key(1)] string PlayEndpoint,
    [property: Key(2)] string GameName);

[MessagePackObject]
public sealed record AuthenticatePlayerReq([property: Key(0)] string AccessToken);

[MessagePackObject]
public sealed record AuthenticatePlayerRes([property: Key(0)] string ActorId);

[MessagePackObject]
public sealed record AuthenticateReq([property: Key(0)] string AccessToken);

[MessagePackObject]
public sealed record AuthenticateRes([property: Key(0)] string ActorId);

[MessagePackObject]
public sealed record TicTacToeGameJoinReq(
    [property: Key(0)] string RoomId,
    [property: Key(1)] string ActorId);

[MessagePackObject]
public sealed record TicTacToeGameJoinRes([property: Key(0)] GameState State);

[MessagePackObject]
public sealed record JoinGameReq([property: Key(0)] string RoomId);

[MessagePackObject]
public sealed record JoinGameRes([property: Key(0)] GameState State);

[MessagePackObject]
public sealed record PlaceMarkReq([property: Key(0)] int Cell);

[MessagePackObject]
public sealed record PlaceMarkRes([property: Key(0)] GameState State);

[MessagePackObject]
public sealed record PlayerJoinedNotify(
    [property: Key(0)] string RoomId,
    [property: Key(1)] string ActorId,
    [property: Key(2)] string Mark,
    [property: Key(3)] GameState State);

[MessagePackObject]
public sealed record GameStateNotify([property: Key(0)] GameState State);

[MessagePackObject]
public sealed record GameState(
    [property: Key(0)]
    string RoomId,
    [property: Key(1)]
    string Board,
    [property: Key(2)]
    string Status,
    [property: Key(3)]
    string? Winner,
    [property: Key(4)]
    string NextTurn,
    [property: Key(5)]
    string? XActorId,
    [property: Key(6)]
    string? OActorId,
    [property: Key(7)]
    string? LastMoveActorId,
    [property: Key(8)]
    int? LastMoveCell);
