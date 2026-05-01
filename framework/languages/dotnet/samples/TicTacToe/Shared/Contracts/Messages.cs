namespace TicTacToe.Shared.Contracts;

public sealed record CreateGameHttpReq(string? GameName);

public sealed record CreateGameHttpRes(string GameId, string PlayEndpoint, string GameName);

public sealed record CreateGameReq(string GameName);

public sealed record CreateGameRes(string GameId, string PlayEndpoint, string GameName);

public sealed record AuthenticatePlayerReq(string AccessToken);

public sealed record AuthenticatePlayerRes(string PlayerId);

public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(string PlayerId);

public sealed record TicTacToeGameJoinReq(string GameId, string PlayerId);

public sealed record TicTacToeGameJoinRes(GameState State);

public sealed record JoinGameReq(string GameId);

public sealed record JoinGameRes(GameState State);

public sealed record PlaceMarkReq(int Cell);

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
