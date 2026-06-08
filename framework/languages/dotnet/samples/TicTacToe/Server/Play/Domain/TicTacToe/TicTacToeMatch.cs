using TicTacToe.Shared.Contracts;

namespace TicTacToe.Server.Play.Domain.TicTacToe;

internal sealed class TicTacToeMatch(string gameId, TimeSpan turnTimeout)
{
    private readonly Dictionary<string, string> _players = new(StringComparer.Ordinal);
    private readonly char[] _board = Enumerable.Repeat('.', 9).ToArray();
    private string _nextTurn = "X";
    private string _status = "WaitingForPlayers";
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
            0 => "X",
            1 => "O",
            _ => throw new InvalidOperationException("Tic-tac-toe game already has two players.")
        };

        _players.Add(actorId, mark);
        if (_status == "WaitingForPlayers" && _players.Count == 2)
        {
            _status = "InProgress";
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

        if (_status != "InProgress")
        {
            throw new InvalidOperationException($"Game is not in progress. status={_status}");
        }

        if (!string.Equals(mark, _nextTurn, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"It is {_nextTurn}'s turn.");
        }

        if ((uint)cell >= _board.Length)
        {
            throw new ArgumentOutOfRangeException(nameof(cell), "Cell must be between 0 and 8.");
        }

        if (_board[cell] != '.')
        {
            throw new InvalidOperationException($"Cell {cell} is already occupied.");
        }

        _board[cell] = mark[0];
        _lastMoveActorId = actorId;
        _lastMoveCell = cell;
        AdvanceAfterMove(actorId, mark, now);
        return new TicTacToeMoveChange(Snapshot());
    }

    public TicTacToeTickChange Tick(DateTimeOffset now)
    {
        if (_status != "InProgress"
            || _turnDeadline is not { } deadline
            || now < deadline)
        {
            return new TicTacToeTickChange(Snapshot(), false);
        }

        var timedOut = _players.FirstOrDefault(player => player.Value == _nextTurn).Key;
        var winner = _players.FirstOrDefault(player => player.Value != _nextTurn).Key;

        _status = "TurnTimedOut";
        _winner = string.IsNullOrEmpty(winner) ? null : winner;
        _nextTurn = string.Empty;
        _lastMoveActorId = string.IsNullOrEmpty(timedOut) ? null : timedOut;
        _lastMoveCell = null;
        _turnDeadline = null;
        return new TicTacToeTickChange(Snapshot(), true);
    }

    public GameState Snapshot()
    {
        var x = _players.FirstOrDefault(static player => player.Value == "X").Key;
        var o = _players.FirstOrDefault(static player => player.Value == "O").Key;
        return new GameState(
            gameId,
            new string(_board),
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
        if (HasWon(mark[0]))
        {
            _status = "Won";
            _winner = actorId;
            _nextTurn = string.Empty;
            _turnDeadline = null;
            return;
        }

        if (_board.All(static cell => cell != '.'))
        {
            _status = "Draw";
            _winner = null;
            _nextTurn = string.Empty;
            _turnDeadline = null;
            return;
        }

        _nextTurn = mark == "X" ? "O" : "X";
        ResetTurnDeadline(now);
    }

    private void ResetTurnDeadline(DateTimeOffset now)
    {
        _turnDeadline = now.Add(turnTimeout);
    }

    private bool HasWon(char mark)
    {
        ReadOnlySpan<int> lines =
        [
            0, 1, 2,
            3, 4, 5,
            6, 7, 8,
            0, 3, 6,
            1, 4, 7,
            2, 5, 8,
            0, 4, 8,
            2, 4, 6
        ];

        for (var i = 0; i < lines.Length; i += 3)
        {
            if (_board[lines[i]] == mark
                && _board[lines[i + 1]] == mark
                && _board[lines[i + 2]] == mark)
            {
                return true;
            }
        }

        return false;
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
