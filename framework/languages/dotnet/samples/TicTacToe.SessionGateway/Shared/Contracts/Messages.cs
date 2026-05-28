namespace TicTacToe.SessionGateway.Shared.Contracts;

public sealed record AuthenticateReq(string ActorId);

public sealed record AuthenticateRes(string ActorId);

public sealed record AuthenticateActorReq(string ActorId);

public sealed record AuthenticateActorRes(
    bool Accepted,
    string? ActorId,
    string? Reason);

public sealed record EnsurePlayerActorReq(string ActorId);

public sealed record ActorRefSnapshot(
    byte[] NodeRid,
    string ActorId,
    ulong Generation);

public sealed record EnsurePlayerActorRes(
    string ActorId,
    string ActorType,
    ActorRefSnapshot Actor);

public sealed record CreateMatchReq(string? OwnerActorId = null);

public sealed record CreateMatchRes(
    string MatchId,
    string OwnerActorId);

public sealed record CreateMatchRoomReq();

public sealed record CreateMatchRoomRes(string MatchId);

public sealed record JoinMatchReq(string MatchId);

public sealed record JoinMatchRes(
    string MatchId,
    string ActorId,
    string Mark,
    TicTacToeState State);

public sealed record PlaceMarkReq(string MatchId, int Cell);

public sealed record PlaceMarkRes(TicTacToeState State);

public sealed record OpponentJoinedNotify(
    string MatchId,
    string OpponentActorId,
    string Mark,
    TicTacToeState State);

public sealed record TurnChangedNotify(
    string MatchId,
    string TurnActorId,
    TicTacToeState State);

public sealed record GameEndedNotify(
    string MatchId,
    string? WinnerActorId,
    bool Draw,
    TicTacToeState State);

public sealed record TicTacToeState(
    string MatchId,
    string Board,
    string Status,
    string TurnActorId,
    string? WinnerActorId,
    bool Draw,
    string? XActorId,
    string? OActorId,
    string? LastMoveActorId,
    int? LastMoveCell);
