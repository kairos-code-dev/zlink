namespace TicTacToe.SessionGateway.Play;

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

internal enum TicTacToeGameEventKind
{
    OpponentJoined,
    TurnChanged,
    GameEnded,
}

internal sealed record TicTacToeGameEvent(
    TicTacToeGameEventKind Kind,
    string RecipientActorId,
    TicTacToeGameSnapshot Snapshot,
    string? JoinedActorId = null,
    TicTacToeMark? JoinedMark = null);

internal sealed record JoinMatchSpotResult(
    string MatchId,
    string ActorId,
    TicTacToeMark Mark,
    TicTacToeGameSnapshot Snapshot,
    IReadOnlyList<TicTacToeGameEvent> Events);

internal sealed record MoveResult(
    TicTacToeGameSnapshot Snapshot,
    IReadOnlyList<TicTacToeGameEvent> Events);
