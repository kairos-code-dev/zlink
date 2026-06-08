using TicTacToe.Shared.Contracts;

namespace TicTacToe.Server.Play.Domain.TicTacToe;

internal sealed class TicTacToeMatch(string roomId, TimeSpan turnTimeout)
{
    private const string MarkX = "X";
    private const string MarkO = "O";
    private const string WaitingForPlayers = "WaitingForPlayers";
    private const string InProgress = "InProgress";
    private const string Won = "Won";
    private const string Draw = "Draw";
    private const string TurnTimedOut = "TurnTimedOut";

    private readonly Dictionary<string, string> _players = new(StringComparer.Ordinal);
    private readonly TicTacToeBoard _board = new();
    private string _nextTurn = MarkX;
    private string _status = WaitingForPlayers;
    private string? _winner;
    private string? _lastMoveActorId;
    private int? _lastMoveCell;
    private DateTimeOffset? _turnDeadline;

    public TicTacToeJoinChange JoinPlayer(string actorId, DateTimeOffset now)
    {
        if (_players.TryGetValue(actorId, out var existingMark))
        {
            return new TicTacToeJoinChange(Snapshot(), existingMark, false);
        }

        var mark = _players.Count switch
        {
            0 => MarkX,
            1 => MarkO,
            _ => throw new InvalidOperationException("Tic-tac-toe game already has two players.")
        };

        _players.Add(actorId, mark);
        if (_status == WaitingForPlayers && _players.Count == 2)
        {
            _status = InProgress;
            ResetTurnDeadline(now);
        }

        return new TicTacToeJoinChange(Snapshot(), mark, true);
    }

    public TicTacToeMoveChange PlaceMark(string actorId, int cell, DateTimeOffset now)
    {
        if (!_players.TryGetValue(actorId, out var mark))
        {
            throw new InvalidOperationException("Player has not joined this game.");
        }

        if (_status != InProgress)
        {
            throw new InvalidOperationException($"Game is not in progress. status={_status}");
        }

        if (!string.Equals(mark, _nextTurn, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"It is {_nextTurn}'s turn.");
        }

        _board.Place(mark, cell);
        _lastMoveActorId = actorId;
        _lastMoveCell = cell;
        AdvanceAfterMove(actorId, mark, now);
        return new TicTacToeMoveChange(Snapshot());
    }

    public TicTacToeTickChange Tick(DateTimeOffset now)
    {
        if (_status != InProgress
            || _turnDeadline is not { } deadline
            || now < deadline)
        {
            return new TicTacToeTickChange(Snapshot(), false);
        }

        var timedOut = _players.FirstOrDefault(player => player.Value == _nextTurn).Key;
        var winner = _players.FirstOrDefault(player => player.Value != _nextTurn).Key;

        _status = TurnTimedOut;
        _winner = string.IsNullOrEmpty(winner) ? null : winner;
        _nextTurn = string.Empty;
        _lastMoveActorId = string.IsNullOrEmpty(timedOut) ? null : timedOut;
        _lastMoveCell = null;
        _turnDeadline = null;
        return new TicTacToeTickChange(Snapshot(), true);
    }

    public GameState Snapshot()
    {
        var x = _players.FirstOrDefault(static player => player.Value == MarkX).Key;
        var o = _players.FirstOrDefault(static player => player.Value == MarkO).Key;
        return new GameState(
            roomId,
            _board.Snapshot(),
            _status,
            _winner,
            _nextTurn,
            string.IsNullOrEmpty(x) ? null : x,
            string.IsNullOrEmpty(o) ? null : o,
            _lastMoveActorId,
            _lastMoveCell);
    }

    private void AdvanceAfterMove(string actorId, string mark, DateTimeOffset now)
    {
        if (_board.HasWon(mark))
        {
            _status = Won;
            _winner = actorId;
            _nextTurn = string.Empty;
            _turnDeadline = null;
            return;
        }

        if (_board.IsFull)
        {
            _status = Draw;
            _winner = null;
            _nextTurn = string.Empty;
            _turnDeadline = null;
            return;
        }

        _nextTurn = mark == MarkX ? MarkO : MarkX;
        ResetTurnDeadline(now);
    }

    private void ResetTurnDeadline(DateTimeOffset now)
    {
        _turnDeadline = now.Add(turnTimeout);
    }

}

internal sealed record TicTacToeJoinChange(
    GameState State,
    string Mark,
    bool IsNewPlayer);

internal sealed record TicTacToeMoveChange(GameState State);

internal sealed record TicTacToeTickChange(
    GameState State,
    bool HasChanged);
