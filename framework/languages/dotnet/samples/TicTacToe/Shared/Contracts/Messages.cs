namespace TicTacToe.Shared.Contracts;

public sealed record CreateGameHttpReq(string? GameName);

public sealed record CreateGameHttpRes(string RoomId, string PlayEndpoint, string GameName);

public sealed record CreateGameReq(string GameName);

public sealed record CreateGameRes(string RoomId, string PlayEndpoint, string GameName);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(string ActorId);

public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(string ActorId);

public sealed record TicTacToeGameJoinReq(string RoomId, string ActorId);

public sealed record TicTacToeGameJoinRes(GameState State);

public sealed record JoinGameReq(string RoomId);

public sealed record JoinGameRes(GameState State);

public sealed record PlaceMarkReq(int Cell);

public sealed record PlaceMarkRes(GameState State);

public sealed record PlayerJoinedNotify(string RoomId, string ActorId, string Mark, GameState State);

public sealed record GameStateNotify(GameState State);

public sealed record GameState(
    string RoomId,
    string Board,
    string Status,
    string? Winner,
    string NextTurn,
    string? XActorId,
    string? OActorId,
    string? LastMoveActorId,
    int? LastMoveCell);
