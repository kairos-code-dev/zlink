namespace TicTacToe.SessionGateway.Contracts;

public sealed record AuthenticateReq(string PlayerId);

public sealed record AuthenticateRes(string PlayerId);

public sealed record CreateGameReq(string PreferredGameId);

public sealed record CreateGameRes(string GameId);

public sealed record ResolveGameReq(string GameId);

public sealed record ResolveGameRes(string GameId, string PlayNodeRid);

public sealed record JoinGameReq(string GameId);

public sealed record JoinGameRes(GameState State);

public sealed record PlaceMarkReq(string GameId, int Cell);

public sealed record PlaceMarkRes(GameState State);

public sealed record PlayerJoinedNotify(string GameId, string PlayerId, string Mark, GameState State);

public sealed record GameStateNotify(GameState State);

public sealed record GameState(
    string GameId,
    string Board,
    string Status,
    string? Winner,
    string NextTurn,
    string? XPlayerId,
    string? OPlayerId,
    string? LastMovePlayerId,
    int? LastMoveCell);
