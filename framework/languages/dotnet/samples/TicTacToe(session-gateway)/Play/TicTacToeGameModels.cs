namespace TicTacToe.SessionActorDispatch.Play;

internal enum TicTacToeMark
{
    X,
    O,
}

internal enum TicTacToeGameStatus
{
    WaitingForActors,
    InProgress,
    Won,
    Draw,
}

internal sealed record ActorSlot(string ActorId, TicTacToeMark Mark);

internal sealed record CreateMatchRoomResult(string MatchId);

internal sealed record TicTacToeGameSnapshot(
    string MatchId,
    string Board,
    TicTacToeGameStatus Status,
    string TurnActorId,
    string? WinnerActorId,
    string? XActorId,
    string? OActorId,
    string? LastMoveActorId,
    int? LastMoveCell);

internal abstract record TicTacToeGameEvent(
    string RecipientActorId,
    TicTacToeGameSnapshot Snapshot);

internal sealed record OpponentJoinedGameEvent(
    string RecipientActorId,
    string JoinedActorId,
    TicTacToeMark Mark,
    TicTacToeGameSnapshot Snapshot)
    : TicTacToeGameEvent(RecipientActorId, Snapshot);

internal sealed record TurnChangedGameEvent(
    string RecipientActorId,
    TicTacToeGameSnapshot Snapshot)
    : TicTacToeGameEvent(RecipientActorId, Snapshot);

internal sealed record GameEndedGameEvent(
    string RecipientActorId,
    TicTacToeGameSnapshot Snapshot)
    : TicTacToeGameEvent(RecipientActorId, Snapshot);

internal sealed record JoinResult(
    string MatchId,
    string ActorId,
    TicTacToeMark Mark,
    TicTacToeGameSnapshot Snapshot,
    IReadOnlyList<TicTacToeGameEvent> Events);

internal sealed record MoveResult(
    TicTacToeGameSnapshot Snapshot,
    IReadOnlyList<TicTacToeGameEvent> Events);
